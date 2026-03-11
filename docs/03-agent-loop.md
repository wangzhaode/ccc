# Agent Loop + API Client 实现

## 概述

Agent Loop 是 cc.cpp 的核心引擎，实现了"用户输入 → LLM 推理 → 工具执行 → 继续推理"的循环。API Client 负责与 LLM 提供商通信，支持 Anthropic 和 OpenAI 两种协议。

---

## API Client 设计

### 双 Provider 架构

`ApiClient` 类从配置文件 `~/.cc/settings.json` 读取 API 配置，自动初始化对应的 API 提供商：

```cpp
enum class ApiProvider {
    Anthropic,  // 使用 x-api-key 认证
    OpenAI      // 使用 Bearer token 认证
};
```

配置优先级：`~/.cc/settings.json` > 环境变量 > 默认值。

配置文件格式：

```json
{
  "provider": "openai",
  "api_key": "your-key",
  "base_url": "https://api.example.com",
  "model": "model-name"
}
```

### 统一接口

`chat()` 方法是唯一的公开调用接口，内部根据 provider 分派：

```cpp
ApiResponse chat(
    const std::string& system_prompt,
    const json& messages,
    const json& tools,
    StreamTextCallback on_text = nullptr  // 流式文本回调
);
```

返回的 `ApiResponse` 统一使用 Anthropic 风格的消息格式（content blocks），无论底层使用哪个 provider。

### Anthropic API 调用

请求格式：
- 端点：`POST /v1/messages`
- 认证头：`x-api-key`
- 版本头：`anthropic-version: 2023-06-01`
- 参数：`model`、`max_tokens`（8192）、`stream: true`、`system`、`messages`、`tools`

### OpenAI API 调用

请求格式：
- 端点：`POST /v1/chat/completions`
- 认证头：`Authorization: Bearer <key>`
- 参数：`model`、`stream: true`、`messages`（含 system message）、`tools`

### 消息格式转换

由于内部统一使用 Anthropic 格式，调用 OpenAI 时需要做格式转换：

**工具定义转换**（`tools_to_openai`）：

```
Anthropic 格式:                    OpenAI 格式:
{                                  {
  "name": "Read",                    "type": "function",
  "description": "...",              "function": {
  "input_schema": { ... }             "name": "Read",
}                                      "description": "...",
                                       "parameters": { ... }
                                     }
                                   }
```

**消息转换**（`messages_to_openai`）：

- `user` 消息中的 `tool_result` 块 → OpenAI 的 `role: "tool"` 独立消息
- `assistant` 消息中的 `tool_use` 块 → OpenAI 的 `tool_calls` 数组
- `system` prompt 从独立参数 → 插入为消息列表的第一条 `role: "system"` 消息

**响应转换**：OpenAI 的流式响应被解析后，统一转换回 Anthropic 风格的 content blocks（`text` 块和 `tool_use` 块）。

---

## SSE 流式解析

两个 provider 都使用 Server-Sent Events（SSE）格式返回流式响应。当前实现使用 httplib 的同步 POST，收到完整响应体后逐行解析 SSE 事件。

### 解析逻辑（`parse_sse_lines`）

```
逐行读取响应体
  ├─ 跳过空行
  ├─ 跳过非 "data: " 开头的行
  ├─ 跳过 "data: [DONE]"
  └─ 解析 JSON 并触发回调
```

### Anthropic SSE 事件处理

按事件类型分派：

| 事件类型 | 处理逻辑 |
|----------|----------|
| `message_start` | 提取 `input_tokens` 用量信息 |
| `content_block_start` | 创建新的 text 块或 tool_use 块 |
| `content_block_delta` | 追加文本增量（`text_delta`）或工具参数 JSON 片段（`input_json_delta`） |
| `content_block_stop` | 解析累积的工具参数 JSON 字符串为 JSON 对象 |
| `message_delta` | 提取 `stop_reason` 和 `output_tokens` |

工具调用的参数以 JSON 片段流式到达，在 `content_block_delta` 阶段累积为字符串（`current_tool_json`），在 `content_block_stop` 时统一解析。

### OpenAI SSE 事件处理

OpenAI 的流式格式不同，每个 chunk 包含 `choices[0].delta`：

- `delta.content`：文本增量
- `delta.tool_calls`：工具调用增量（按 index 区分多个并行工具调用）
- `choices[0].finish_reason`：结束原因（`"tool_calls"` → `"tool_use"`，其他 → `"end_turn"`）

### 流式文本回调

在解析 SSE 过程中，每收到文本增量就通过 `StreamTextCallback` 回调输出，实现逐字显示效果：

```cpp
response = api_client_.chat(system_prompt, messages_, tools,
    [](const std::string& text) {
        std::cout << text;
        std::cout.flush();
    }
);
```

---

## Agent Loop 实现

### 整体流程

```
process(user_input)
  │
  ├─ 将用户消息追加到 messages_ 历史
  │
  └─ agent_loop()
       │
       ├─ 构建 system prompt（build_system_prompt）
       ├─ 获取工具定义（get_tool_definitions）
       ├─ 调用 API（api_client_.chat）
       ├─ 将 assistant 消息追加到历史
       │
       ├─ 遍历 response.message["content"]
       │   ├─ 发现 tool_use 块 → execute_tool_call
       │   └─ 仅文本 → 输出完毕，返回
       │
       ├─ 如果有工具调用：
       │   ├─ 将所有 tool_result 打包为 user 消息
       │   ├─ 追加到历史
       │   └─ 继续循环（下一次迭代）
       │
       └─ 如果无工具调用：返回，等待下一次用户输入
```

### 最大迭代限制

Agent Loop 设置了 `max_iterations = 50` 的安全上限，防止 LLM 陷入无限工具调用循环。超出限制时输出警告并停止。

### 工具调用执行（`execute_tool_call`）

每个 tool_use 块的处理流程：

1. **解析参数**：从块中提取 `name`、`id`、`input`
2. **权限检查**：调用 `permission_manager_.check_and_request()`
3. **信息显示**：在终端打印工具名称和关键参数（文件路径、命令等）
4. **执行工具**：通过 `tool_registry_.execute()` 执行
5. **构造结果**：返回 `tool_result` 格式的 JSON 块

权限被拒绝时，返回 `is_error: true` 的 `"Permission denied by user."` 结果，LLM 收到后会调整策略。

### 消息历史管理

消息历史以 `json` 数组（`messages_`）存储，生命周期为整个会话：

```
messages_ 的内容演变：

第1轮：
  [0] user: "请读取 main.cpp"

  → API 调用 → 返回 tool_use

  [0] user: "请读取 main.cpp"
  [1] assistant: [tool_use: Read main.cpp]
  [2] user: [tool_result: 文件内容...]

  → API 调用 → 返回纯文本

  [0] user: "请读取 main.cpp"
  [1] assistant: [tool_use: Read main.cpp]
  [2] user: [tool_result: 文件内容...]
  [3] assistant: [text: "文件内容如下..."]

第2轮：
  [4] user: "请修改第10行..."
  ... 继续累积
```

tool_result 以 `user` 角色消息的方式追加，这是 Anthropic API 的 tool use 协议要求：工具调用结果必须作为 user 消息的一部分返回。

---

## System Prompt 构建

`build_system_prompt()` 方法组装完整的系统提示词，包含三个部分：

### 1. 角色定义

```
You are an AI coding assistant running in a terminal.
You help users with software engineering tasks by reading, writing,
and editing files, executing commands, and searching codebases.
```

### 2. 环境信息

- 当前工作目录（`std::filesystem::current_path()`）
- 操作系统平台（通过编译期宏 `__APPLE__` / `__linux__` 检测）

### 3. 工具使用指南

简要说明每个工具的用途，引导 LLM 正确选择工具。

### 4. 项目记忆

通过 `memory_manager_.build_memory_prompt()` 注入 CC.md 的内容。如果找到了 CC.md 文件，其内容会被添加到 `# Project Instructions` 部分。

---

## 入口程序（main.cpp）

程序支持两种运行模式：

### 交互模式（REPL）

无参数启动时进入交互循环：

```
print_banner()     ← 显示欢迎界面
while (true):
    显示提示符 "> "
    读取用户输入
    agent.process(input)
```

通过 Ctrl+D（EOF）退出。

### 单次执行模式

命令行参数作为输入：

```bash
./cc "请帮我查看当前目录结构"
```

所有参数用空格拼接为一条消息，执行后直接退出。
