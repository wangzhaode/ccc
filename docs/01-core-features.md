# Claude Code 核心功能拆解与原理分析

## 概述

Claude Code 是一个基于大语言模型（LLM）的交互式命令行代码代理工具。它的核心思想是：**让 LLM 在一个循环中反复"思考→使用工具→观察结果"，直到完成用户的任务。** 这个模式被称为 **ReAct（Reasoning + Acting）循环**，是当前 AI Agent 的主流架构。

本文档将从架构和原理层面拆解 Claude Code 的核心功能，为 C++ 重新实现提供参考。

---

## 一、整体架构：Agent Loop（代理循环）

### 1.1 核心原理

Claude Code 的本质是一个 **工具增强的对话系统**。与普通聊天机器人不同，它可以调用本地工具（读写文件、执行命令等）来与真实环境交互。整个系统的核心是一个循环：

```
┌─────────────────────────────────────────────────┐
│                  Agent Loop                      │
│                                                  │
│   用户输入                                        │
│     ↓                                            │
│   构造消息 (system prompt + history + user msg)   │
│     ↓                                            │
│   调用 LLM API（流式返回）                         │
│     ↓                                            │
│   解析 LLM 响应                                   │
│     ├─ 纯文本 → 显示给用户，等待下一次输入           │
│     └─ 工具调用 → 执行工具 → 将结果追加到历史        │
│                    ↓                              │
│              再次调用 LLM API（继续循环）            │
│                                                  │
└─────────────────────────────────────────────────┘
```

**关键点：** LLM 每次返回的响应可能包含文本和/或工具调用。如果包含工具调用，系统执行工具后将结果反馈给 LLM，LLM 继续推理。这个循环一直进行，直到 LLM 返回纯文本（没有工具调用），此时代理认为任务完成，等待用户的下一次输入。

### 1.2 消息结构

每次调用 LLM API 时，发送的消息序列如下：

```
┌────────────────────────────────────────────┐
│ System Prompt（系统提示）                     │
│  ├─ 角色定义和行为规则                        │
│  ├─ 可用工具的描述（JSON Schema）             │
│  ├─ CLAUDE.md 内容（项目记忆）                │
│  └─ 环境信息（OS、工作目录、git状态等）         │
├────────────────────────────────────────────┤
│ Message History（消息历史）                    │
│  ├─ user: "请帮我修复 login.ts 中的 bug"     │
│  ├─ assistant: [tool_use: Read login.ts]    │
│  ├─ user(tool_result): "文件内容..."         │
│  ├─ assistant: [tool_use: Edit login.ts]    │
│  ├─ user(tool_result): "编辑成功"            │
│  └─ assistant: "我已经修复了..."              │
├────────────────────────────────────────────┤
│ Current User Message（当前用户输入）           │
└────────────────────────────────────────────┘
```

**注意：** 工具调用的结果在 API 协议中以 `tool_result` 角色（属于 `user` 消息的一部分）返回，而工具调用本身是 `assistant` 消息的一部分。这是 Claude API 的 tool use 协议规定的格式。

### 1.3 API 调用方式

Claude Code 使用 **流式（streaming）API 调用**，原因有二：
1. **用户体验**：可以在 LLM 生成过程中逐字显示文本，而不是等待完整响应
2. **工具调用检测**：可以在流式返回中检测到工具调用的开始，提前做准备

流式响应的事件类型：
- `content_block_start`：开始一个新的内容块（文本块或工具调用块）
- `content_block_delta`：内容块的增量数据
- `content_block_stop`：一个内容块结束
- `message_stop`：整个消息结束

### 1.4 C++ 实现要点

```
核心数据结构：
- Message: { role, content[] }
- Content: TextBlock | ToolUseBlock | ToolResultBlock
- ToolUseBlock: { id, name, input(json) }
- ToolResultBlock: { tool_use_id, content, is_error }

核心流程：
1. 初始化：加载配置、连接 API、注册工具
2. 主循环：读取用户输入 → 调用 API → 处理响应
3. 工具循环：检测工具调用 → 执行 → 反馈结果 → 再次调用 API
```

---

## 二、Tool Use（工具使用系统）

### 2.1 原理：函数调用（Function Calling）

工具使用是 Claude Code 最核心的能力。原理是 Claude API 支持 **tool use**（也叫 function calling）：

1. 在 API 请求中，定义可用工具的名称、描述和参数 JSON Schema
2. LLM 根据用户意图，选择合适的工具并生成参数
3. 客户端（即 Claude Code）执行工具，将结果返回给 LLM

**LLM 并不真正"执行"工具，它只是生成一个 JSON 格式的工具调用请求**，实际执行由本地代码完成。

### 2.2 工具定义格式

每个工具通过 JSON Schema 定义其参数，例如 `Read` 工具：

```json
{
  "name": "Read",
  "description": "读取本地文件系统中的文件内容...",
  "input_schema": {
    "type": "object",
    "properties": {
      "file_path": {
        "type": "string",
        "description": "文件的绝对路径"
      },
      "offset": {
        "type": "number",
        "description": "起始行号"
      },
      "limit": {
        "type": "number",
        "description": "读取行数"
      }
    },
    "required": ["file_path"]
  }
}
```

LLM 看到这个定义后，在需要读取文件时会生成：

```json
{
  "type": "tool_use",
  "id": "toolu_abc123",
  "name": "Read",
  "input": {
    "file_path": "/path/to/file.ts"
  }
}
```

### 2.3 工具执行流程

```
LLM 返回 tool_use 块
  ↓
解析工具名称和参数
  ↓
权限检查（是否需要用户确认？）
  ├─ 需要确认 → 显示确认对话框 → 用户批准/拒绝
  └─ 自动允许 → 继续
  ↓
Hook 前置检查（pre-tool hooks）
  ↓
执行工具逻辑
  ├─ Read: 打开文件，读取内容，添加行号
  ├─ Write: 写入文件内容
  ├─ Edit: 查找 old_string，替换为 new_string
  ├─ Bash: fork 子进程执行 shell 命令
  ├─ Glob: 遍历目录匹配模式
  ├─ Grep: 调用 ripgrep 搜索内容
  └─ ...
  ↓
Hook 后置处理（post-tool hooks）
  ↓
构造 tool_result 消息
  ↓
追加到消息历史，继续 Agent Loop
```

### 2.4 内置工具清单

| 工具 | 作用 | 实现复杂度 |
|------|------|-----------|
| **Read** | 读取文件，支持行号偏移和限制 | 低 |
| **Write** | 创建/覆盖文件 | 低 |
| **Edit** | 精确字符串替换编辑文件 | 中 |
| **Bash** | 执行 shell 命令，支持超时和后台运行 | 高 |
| **Glob** | 文件路径模式匹配（类似 `find`） | 中 |
| **Grep** | 代码内容搜索（基于 ripgrep） | 中 |
| **WebSearch** | 网络搜索 | 低（API 调用）|
| **WebFetch** | 获取网页内容并转为 Markdown | 中 |
| **Agent** | 启动子代理执行复杂任务 | 高 |
| **AskUserQuestion** | 向用户提问获取信息 | 低 |
| **TaskCreate/Update/List** | 任务管理 | 中 |
| **LSP** | 与语言服务器协议交互 | 高 |

### 2.5 Edit 工具的设计原理

Edit 是最精巧的工具之一。它采用 **精确字符串匹配替换** 而非基于行号：

```
参数：
  - file_path: 文件路径
  - old_string: 要被替换的原文本（必须在文件中唯一存在）
  - new_string: 替换后的新文本
  - replace_all: 是否替换所有匹配项
```

**为什么不用行号？** 因为在 Agent Loop 中，文件可能在多次编辑间被修改，行号会漂移。使用字符串匹配更鲁棒——只要内容唯一就能准确定位。

**唯一性约束：** `old_string` 必须在文件中唯一匹配。如果有多个匹配，工具会报错，要求 LLM 提供更多上下文来确保唯一性。

### 2.6 Bash 工具的设计原理

Bash 工具需要特别注意安全性和资源管理：

```
关键设计点：
1. 沙箱执行：限制可访问的目录和命令
2. 超时机制：默认 2 分钟超时，可配置
3. 工作目录持久化：多次 Bash 调用之间保持 cwd
4. 后台执行：支持 run_in_background，异步获取结果
5. 输出捕获：同时捕获 stdout 和 stderr
6. 信号处理：支持超时时 SIGTERM/SIGKILL
```

### 2.7 C++ 实现要点

```cpp
// 工具注册表（工厂模式）
class ToolRegistry {
    map<string, unique_ptr<Tool>> tools;
public:
    void register_tool(string name, unique_ptr<Tool> tool);
    ToolResult execute(string name, json params);
};

// 工具基类
class Tool {
public:
    virtual string name() = 0;
    virtual json schema() = 0;             // 返回 JSON Schema
    virtual ToolResult execute(json params) = 0;
    virtual PermissionLevel permission() = 0;  // 需要的权限级别
};

// 例：Read 工具
class ReadTool : public Tool {
    ToolResult execute(json params) override {
        string path = params["file_path"];
        int offset = params.value("offset", 0);
        int limit = params.value("limit", 2000);
        // 读取文件，添加行号，返回内容
    }
};
```

---

## 三、Memory 系统（记忆管理）

### 3.1 记忆层级

Claude Code 有三个层级的记忆，每一层的持久性和作用域不同：

```
┌─────────────────────────────────────────────┐
│  Layer 1: 会话记忆 (Session Memory)           │
│  - 存储位置：内存中的消息数组                    │
│  - 生命周期：当前对话会话                        │
│  - 内容：完整的对话历史（user/assistant/tool）   │
│  - 特点：最详细，但会话结束即消失                 │
├─────────────────────────────────────────────┤
│  Layer 2: 项目记忆 (Project Memory)            │
│  - 存储位置：CLAUDE.md 文件                     │
│  - 生命周期：永久（文件系统）                     │
│  - 内容：项目规范、架构说明、编码约定              │
│  - 特点：每次启动自动加载到 system prompt         │
├─────────────────────────────────────────────┤
│  Layer 3: 用户记忆 (User Memory)               │
│  - 存储位置：~/.claude/ 目录下                   │
│  - 生命周期：永久                                │
│  - 内容：用户偏好、全局配置                       │
│  - 特点：跨项目共享                              │
└─────────────────────────────────────────────┘
```

### 3.2 CLAUDE.md 的工作原理

CLAUDE.md 是一个约定俗成的配置文件，其原理非常简单：

1. Claude Code 启动时，在当前目录和父目录中查找 `CLAUDE.md` 文件
2. 读取文件内容
3. 将内容作为 system prompt 的一部分注入到每次 API 调用中

**查找路径优先级**（从高到低）：
```
1. 项目根目录/CLAUDE.md
2. 项目根目录/.claude/CLAUDE.md
3. ~/.claude/CLAUDE.md（用户级）
```

**本质上，CLAUDE.md 就是一个自动注入的提示词文件**，它让 LLM 在每次回答时都能看到项目的上下文和规范。

### 3.3 自动记忆（Auto Memory）

Claude Code 具有自动记忆功能，当它在工作中发现值得记住的模式时，可以主动写入记忆文件：

```
记忆目录结构：
~/.claude/projects/<project-hash>/memory/
├── MEMORY.md          ← 主记忆文件，每次启动自动加载（前200行）
├── debugging.md       ← 按主题组织的详细记忆
├── patterns.md
└── architecture.md
```

**记忆写入原则：**
- 跨多次交互确认的稳定模式才写入
- 按语义主题组织，而非按时间顺序
- MEMORY.md 保持简洁（< 200行），详细内容放在子文件
- 用户明确要求记住的内容立即写入

### 3.4 C++ 实现要点

```cpp
class MemoryManager {
    string project_root;
    string user_home;

public:
    // 启动时加载所有记忆
    string load_project_memory();   // 读取 CLAUDE.md
    string load_user_memory();      // 读取 ~/.claude/CLAUDE.md
    string load_auto_memory();      // 读取 MEMORY.md

    // 构造注入 system prompt 的记忆部分
    string build_memory_prompt();

    // 自动记忆写入
    void save_memory(string topic, string content);
};
```

---

## 四、Context Management（上下文管理）

### 4.1 核心问题

LLM 有上下文窗口限制（如 Claude 的 200K tokens）。在长时间的编码会话中，消息历史会不断增长，最终超出限制。上下文管理的目标是：**在有限的 token 预算内，保留最重要的信息。**

### 4.2 Token 预算分配

```
总 Token 预算（如 200K）
├── System Prompt：~5K-20K tokens
│   ├── 基础指令：~3K
│   ├── 工具定义：~5K-10K（取决于工具数量）
│   └── CLAUDE.md：~1K-5K
├── 消息历史：~150K-180K tokens
│   ├── 近期消息（保留完整）
│   └── 早期消息（可能被压缩）
└── 响应预留：~4K-8K tokens
    └── LLM 生成回复的空间
```

### 4.3 上下文压缩策略

当消息历史接近 token 限制时，系统触发压缩：

```
压缩流程：
1. 计算当前消息历史的总 token 数
2. 如果超过阈值（如总预算的 80%），触发压缩
3. 压缩策略：
   a. 保留最近 N 条消息不变（如最近 10 条）
   b. 对早期消息进行摘要：
      - 将一组旧消息发送给 LLM，请求生成摘要
      - 用摘要替换原始消息
   c. 截断过长的工具输出（如文件内容、命令输出）
   d. 保留关键决策点（如用户确认、重要发现）
```

**压缩的挑战：** 压缩本身也需要调用 LLM，这会消耗额外的 token 和时间。因此需要平衡压缩频率和压缩质量。

### 4.4 Token 计数

准确的 token 计数对于上下文管理至关重要。Claude 使用自己的 tokenizer，不同于 GPT 系列的 tiktoken。

**简化方案：** 对于 C++ 实现，可以使用近似估算（如每 4 个字符约 1 个 token，中文每个字约 1-2 个 token），或者使用 Anthropic 提供的 token 计数 API。

### 4.5 C++ 实现要点

```cpp
class ContextManager {
    vector<Message> history;
    int max_tokens;          // 上下文窗口大小
    int reserve_tokens;      // 为响应预留的 token 数

public:
    // 添加消息前检查是否需要压缩
    void add_message(Message msg);

    // 估算当前历史的 token 数
    int estimate_tokens();

    // 执行压缩
    void compress_if_needed();

    // 构建发送给 API 的消息列表
    vector<Message> build_messages(string system_prompt);
};
```

---

## 五、Permission 系统（权限控制）

### 5.1 设计目标

Claude Code 运行在用户的本地环境，拥有执行 shell 命令、读写文件的能力。权限系统的目标是 **防止 LLM 执行用户不希望的操作**，特别是：

- 删除重要文件
- 执行危险命令
- 访问敏感数据
- 修改系统配置

### 5.2 权限级别

```
┌─────────────────────────────────────────────┐
│  Level 0: 自动允许（无需确认）                 │
│  - 读取文件（Read）                           │
│  - 搜索文件（Glob, Grep）                     │
│  - 向用户提问（AskUserQuestion）               │
├─────────────────────────────────────────────┤
│  Level 1: 需要确认（显示操作详情）              │
│  - 写入/编辑文件（Write, Edit）                │
│  - 执行 shell 命令（Bash）                    │
│  - 网络请求（WebFetch）                        │
├─────────────────────────────────────────────┤
│  Level 2: 高危操作（显著警告）                  │
│  - 删除文件                                    │
│  - 修改 git 历史                               │
│  - 访问敏感路径（.env, credentials）            │
└─────────────────────────────────────────────┘
```

### 5.3 权限检查流程

```
工具调用请求
  ↓
查询工具的权限级别
  ↓
检查用户的权限模式设置
  ├─ "trust" 模式 → 直接执行所有操作
  ├─ "auto" 模式 → Level 0 自动允许，其他需确认
  └─ "manual" 模式 → 所有操作都需确认
  ↓
如果需要确认：
  ├─ 显示操作详情（工具名、参数、预期效果）
  ├─ 用户选择：允许 / 拒绝 / 始终允许此类操作
  └─ 如果拒绝，返回错误给 LLM，LLM 调整策略
  ↓
执行或拒绝
```

### 5.4 权限缓存

用户可以选择"始终允许"某类操作（如"允许所有文件编辑"），这会被缓存：

```json
{
  "allowed_tools": ["Read", "Glob", "Grep"],
  "allowed_patterns": {
    "Bash": ["git *", "npm test", "cargo build"],
    "Edit": ["src/**"]
  }
}
```

### 5.5 C++ 实现要点

```cpp
enum class PermissionLevel { Auto, NeedsConfirm, Dangerous };

class PermissionManager {
    map<string, bool> cached_decisions;
    string mode; // "trust", "auto", "manual"

public:
    // 检查是否允许执行
    bool check_permission(string tool_name, json params);

    // 向用户请求确认
    bool request_confirmation(string tool_name, json params);

    // 缓存用户决策
    void cache_decision(string pattern, bool allowed);
};
```

---

## 六、Agent 系统（子代理）

### 6.1 设计目的

有些任务过于复杂，需要大量的工具调用和中间状态。如果在主对话中处理，会：
1. 消耗大量上下文窗口
2. 中间步骤的细节污染主对话
3. 无法并行处理独立子任务

Agent 系统通过 **启动独立的子代理** 来解决这些问题。

### 6.2 工作原理

```
主 Agent（Main Agent）
  │
  ├─ 分析任务，决定使用子代理
  │
  ├─ 调用 Agent 工具：
  │   {
  │     "name": "Agent",
  │     "input": {
  │       "prompt": "搜索所有使用了 deprecated API 的文件...",
  │       "subagent_type": "Explore",
  │       "description": "搜索废弃 API 使用"
  │     }
  │   }
  │
  ├─ 子代理启动（独立的 Agent Loop）
  │   ├─ 拥有自己的消息历史
  │   ├─ 可以使用工具（Read, Grep, Glob 等）
  │   ├─ 在独立上下文中工作
  │   └─ 完成后返回结果给主 Agent
  │
  └─ 主 Agent 接收结果，继续工作
```

### 6.3 子代理类型

| 类型 | 用途 | 可用工具 |
|------|------|---------|
| **general-purpose** | 通用复杂任务 | 所有工具 |
| **Explore** | 代码库探索和搜索 | 只读工具（Read, Grep, Glob 等） |
| **Plan** | 设计实现方案 | 只读工具 |

### 6.4 Worktree 隔离

当子代理需要修改文件时，可以在 git worktree 中工作：

```
项目目录/
├── .git/
├── src/                ← 主工作目录
├── .claude/worktrees/
│   ├── agent-1/        ← 子代理1的工作副本
│   │   ├── src/
│   │   └── ...
│   └── agent-2/        ← 子代理2的工作副本
│       ├── src/
│       └── ...
```

**Git Worktree 原理：** Git 允许从同一个仓库创建多个工作目录，每个 worktree 有独立的 HEAD 和工作区，但共享 `.git` 对象库。这使得多个代理可以并行修改代码而互不干扰。

### 6.5 C++ 实现要点

```cpp
class AgentManager {
public:
    // 启动子代理（可以异步）
    future<string> launch_agent(
        string prompt,
        string agent_type,
        bool use_worktree = false
    );

    // 子代理内部就是一个独立的 Agent Loop
    // 拥有独立的 ContextManager 和消息历史
    // 但共享 ToolRegistry（工具可能受限）
};
```

---

## 七、Hooks 系统（钩子）

### 7.1 原理

Hooks 允许用户在工具执行前后注入自定义逻辑，类似于 git hooks 的概念：

```
工具调用
  ↓
Pre-tool Hook（前置钩子）
  ├─ 可以检查参数
  ├─ 可以修改参数
  ├─ 可以阻止执行
  └─ 可以添加额外信息
  ↓
工具执行
  ↓
Post-tool Hook（后置钩子）
  ├─ 可以处理输出
  ├─ 可以触发副作用（如日志）
  └─ 可以修改返回结果
  ↓
返回结果
```

### 7.2 使用场景

- **安全审计**：记录所有 Bash 命令的执行
- **自动格式化**：在文件写入后自动运行格式化工具
- **提交检查**：在用户确认前提交前运行 lint
- **通知**：某些操作完成后发送通知

### 7.3 配置格式

```json
{
  "hooks": {
    "pre-tool": [
      {
        "tool": "Bash",
        "command": "echo 'About to run: $TOOL_INPUT'"
      }
    ],
    "post-tool": [
      {
        "tool": "Write",
        "command": "prettier --write $FILE_PATH"
      }
    ]
  }
}
```

---

## 八、Skill 系统（技能/斜杠命令）

### 8.1 原理

Skill 系统是一种 **提示词模板机制**。每个 skill 本质上是一段预定义的提示词，当用户调用时，这段提示词被注入到当前对话中，指导 LLM 执行特定任务。

```
用户输入 /commit
  ↓
查找名为 "commit" 的 skill 定义
  ↓
加载 skill 提示词模板
  ↓
将模板展开（替换变量，如当前分支名）
  ↓
注入到对话上下文中
  ↓
LLM 按照 skill 指导执行任务
  ↓
（LLM 会调用 Bash 执行 git 命令等）
```

### 8.2 Skill 定义结构

```markdown
# Skill: commit

## 触发条件
用户输入 /commit 或请求提交代码

## 执行步骤
1. 运行 git status 查看更改
2. 运行 git diff 查看详细差异
3. 运行 git log 查看近期提交风格
4. 分析更改，生成提交消息
5. 执行 git commit
6. 验证提交成功
```

**本质上，skill 就是一段详细的"操作手册"，LLM 读到后按步骤执行。**

---

## 九、MCP（Model Context Protocol）

### 9.1 原理

MCP 是一个标准化的协议，用于将外部工具和数据源接入 LLM 应用。它的核心思想是：**将工具提供者和工具消费者解耦。**

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│  Claude Code  │────│  MCP Client  │────│  MCP Server   │
│  (消费者)     │     │  (协议层)    │     │  (提供者)      │
└──────────────┘     └──────────────┘     └──────────────┘
                                               │
                                          ┌────┴────┐
                                          │ 外部系统  │
                                          │(DB/API等)│
                                          └─────────┘
```

### 9.2 MCP 协议要素

MCP 服务器可以提供三种能力：

1. **Tools（工具）**：可调用的函数，如查询数据库、调用 API
2. **Resources（资源）**：可读取的数据，如文件、数据库表
3. **Prompts（提示）**：预定义的提示词模板

### 9.3 通信方式

MCP 使用 **JSON-RPC 2.0** 协议，通过 **stdio**（标准输入输出）或 **SSE**（Server-Sent Events）通信：

```
Claude Code → MCP Server (通过 stdin):
{"jsonrpc": "2.0", "method": "tools/list", "id": 1}

MCP Server → Claude Code (通过 stdout):
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "tools": [
      {
        "name": "query_database",
        "description": "执行 SQL 查询",
        "inputSchema": { ... }
      }
    ]
  }
}
```

### 9.4 集成流程

```
Claude Code 启动
  ↓
读取 MCP 配置（.claude/settings.json）
  ↓
为每个配置的 MCP 服务器：
  ├─ 启动子进程
  ├─ 建立 stdio 通信
  ├─ 调用 tools/list 获取工具列表
  └─ 将工具注册到 ToolRegistry
  ↓
LLM 可以像使用内置工具一样使用 MCP 工具
  ↓
当 LLM 调用 MCP 工具时：
  ├─ ToolRegistry 路由到对应的 MCP Client
  ├─ MCP Client 发送 JSON-RPC 请求给 MCP Server
  ├─ MCP Server 处理并返回结果
  └─ 结果返回给 LLM
```

### 9.5 C++ 实现要点

```cpp
class MCPClient {
    process subprocess;   // 子进程管理
    int next_id = 0;

public:
    // 启动 MCP 服务器
    void connect(string command, vector<string> args);

    // 获取可用工具
    vector<ToolDefinition> list_tools();

    // 调用工具
    json call_tool(string name, json params);

private:
    // JSON-RPC 通信
    json send_request(string method, json params);
    json read_response();
};
```

---

## 十、任务管理系统（Task System）

### 10.1 原理

任务系统为复杂的多步骤工作提供结构化管理。它维护一个内存中的任务列表，支持状态追踪和依赖关系：

```
Task {
  id: string
  subject: string          // 任务标题
  description: string      // 详细描述
  status: pending | in_progress | completed
  blocks: Task[]           // 被此任务阻塞的任务
  blockedBy: Task[]        // 阻塞此任务的前置任务
}
```

### 10.2 工作流

```
用户请求复杂任务
  ↓
LLM 分析任务，拆分为子任务
  ↓
调用 TaskCreate 创建多个任务
  ↓
调用 TaskUpdate 设置依赖关系
  ↓
按依赖顺序逐个执行任务：
  ├─ TaskUpdate(status: in_progress)
  ├─ 执行具体工作（使用其他工具）
  └─ TaskUpdate(status: completed)
  ↓
所有任务完成，汇报结果
```

---

## 优先级规划

### P0（核心功能，必须实现）

1. **Agent Loop**：主循环 + API 调用 + 流式响应
2. **Tool System**：工具注册 + 工具执行 + 结果反馈
3. **基础工具**：Read, Write, Edit, Bash, Glob, Grep
4. **Permission System**：基础权限检查和确认
5. **Memory System**：CLAUDE.md 加载

### P1（重要功能，应该实现）

6. **Context Management**：上下文压缩
7. **Skill System**：斜杠命令
8. **Task System**：任务管理
9. **Auto Memory**：自动记忆

### P2（高级功能，可选实现）

10. **Agent System**：子代理 + Worktree
11. **MCP**：外部工具集成
12. **Hooks**：前后置钩子
13. **LSP**：语言服务器集成
