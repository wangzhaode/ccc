# 工具系统实现

## 概述

工具系统是 ccc 的核心组件，提供了 LLM 与本地环境交互的能力。它由三部分组成：`Tool` 基类定义工具接口，`ToolRegistry` 管理工具注册和执行，权限系统控制工具的执行许可。

---

## Tool 基类设计

所有工具继承自 `Tool` 抽象基类（定义在 `src/tool.h`）：

```cpp
class Tool {
public:
    virtual ~Tool() = default;
    virtual std::string name() const = 0;             // 工具名称
    virtual std::string description() const = 0;      // 工具描述（LLM 可见）
    virtual json schema() const = 0;                  // 参数 JSON Schema
    virtual PermissionLevel permission_level() const = 0;  // 权限级别
    virtual ToolResult execute(const json& params) = 0;    // 执行逻辑
};
```

### 设计决策

- **纯虚接口**：所有方法都是纯虚函数，强制每个工具实现完整接口
- **`const` 方法**：`name()`、`description()`、`schema()`、`permission_level()` 都是 `const`，工具定义不可变
- **JSON 参数**：使用 `nlohmann::json` 作为参数类型，与 API 协议中的工具调用格式一致
- **统一返回值**：所有工具返回 `ToolResult`，包含结果文本和错误标志

### ToolResult

```cpp
struct ToolResult {
    std::string content;   // 结果内容（文本）
    bool is_error = false; // 是否为错误
};
```

工具执行的所有输出（成功结果、错误信息）都通过 `ToolResult` 返回。`is_error` 标志会传递到 API 的 `tool_result` 消息中，让 LLM 知道工具是否执行成功。

---

## 工具定义（JSON Schema）

每个工具通过 `schema()` 方法返回其参数的 JSON Schema 定义。这些定义会发送给 LLM，LLM 据此生成合法的参数。

格式遵循 Anthropic 的 tool use 协议：

```json
{
    "name": "工具名称",
    "description": "工具描述",
    "input_schema": {
        "type": "object",
        "properties": {
            "param1": {
                "type": "string",
                "description": "参数说明"
            }
        },
        "required": ["param1"]
    }
}
```

`schema()` 方法只返回 `input_schema` 部分，由 `ToolRegistry::get_tool_definitions()` 组装完整定义。

---

## ToolRegistry

`ToolRegistry` 是工具的注册中心和执行入口（定义在 `src/tool.h`）：

```cpp
class ToolRegistry {
public:
    void register_tool(std::unique_ptr<Tool> tool);    // 注册工具
    Tool* get(const std::string& name) const;          // 按名称查找
    ToolResult execute(const std::string& name, const json& params); // 执行工具
    json get_tool_definitions() const;                 // 获取所有工具定义
    const std::map<std::string, std::unique_ptr<Tool>>& all() const; // 获取所有工具
private:
    std::map<std::string, std::unique_ptr<Tool>> tools_;
};
```

### 内部存储

使用 `std::map<std::string, std::unique_ptr<Tool>>` 存储工具实例：

- `std::unique_ptr` 管理工具的生命周期
- `std::map` 按名称有序存储，查找复杂度 O(log n)
- 工具名称作为键，确保唯一性

### 注册流程

在 `Agent` 构造函数中完成所有工具的注册：

```cpp
void Agent::register_tools() {
    tool_registry_.register_tool(std::make_unique<ReadTool>());
    tool_registry_.register_tool(std::make_unique<WriteTool>());
    tool_registry_.register_tool(std::make_unique<EditTool>());
    tool_registry_.register_tool(std::make_unique<BashTool>());
    tool_registry_.register_tool(std::make_unique<GlobTool>());
    tool_registry_.register_tool(std::make_unique<GrepTool>());
}
```

### 工具定义生成

`get_tool_definitions()` 遍历所有注册工具，生成 Anthropic API 所需的工具定义数组：

```cpp
json get_tool_definitions() const {
    json tools = json::array();
    for (auto& [name, tool] : tools_) {
        tools.push_back({
            {"name", tool->name()},
            {"description", tool->description()},
            {"input_schema", tool->schema()}
        });
    }
    return tools;
}
```

这个数组直接传给 `ApiClient::chat()` 作为 `tools` 参数。如果使用 OpenAI provider，`ApiClient` 内部会自动转换格式。

### 执行流程

```cpp
ToolResult execute(const std::string& name, const json& params) {
    auto* tool = get(name);
    if (!tool) {
        return {"Unknown tool: " + name, true};
    }
    return tool->execute(params);
}
```

未知工具名称直接返回错误，不会抛出异常。

---

## 工具执行的完整流程

从 LLM 返回 tool_use 到结果反馈的完整路径：

```
LLM 响应中的 tool_use content block
  │
  │  {
  │    "type": "tool_use",
  │    "id": "toolu_abc123",
  │    "name": "Read",
  │    "input": { "file_path": "/path/to/file.cpp" }
  │  }
  │
  ▼
Agent::execute_tool_call()
  │
  ├─ 1. 解析工具名称、ID、参数
  │
  ├─ 2. 权限检查
  │     tool_registry_.get(tool_name) → 获取 Tool* 指针
  │     permission_manager_.check_and_request(name, params, level)
  │     ├─ AutoAllow → 直接通过
  │     ├─ NeedsConfirm → 检查 always_allowed_ 缓存
  │     │   ├─ 已缓存 → 通过
  │     │   └─ 未缓存 → 终端提示用户确认
  │     └─ 拒绝 → 返回 is_error: true 的 tool_result
  │
  ├─ 3. 终端输出工具调用信息
  │     [Read] /path/to/file.cpp
  │     [Bash] git status
  │     [Grep] pattern_string
  │
  ├─ 4. 执行工具
  │     tool_registry_.execute(tool_name, input)
  │     → tool->execute(params)
  │     → 返回 ToolResult { content, is_error }
  │
  ├─ 5. 错误输出（如果有）
  │     终端显示红色错误信息（截断到 200 字符）
  │
  └─ 6. 构造 tool_result JSON
        {
          "type": "tool_result",
          "tool_use_id": "toolu_abc123",
          "content": "结果内容...",
          "is_error": false
        }
```

### 权限集成

权限检查在工具执行之前进行。每个工具通过 `permission_level()` 声明自己所需的权限级别：

| 权限级别 | 工具 | 行为 |
|----------|------|------|
| `AutoAllow` | Read, Glob, Grep | 无需确认，直接执行 |
| `NeedsConfirm` | Write, Edit, Bash | 需要用户在终端确认 |

权限被拒绝时，不会执行工具，而是返回 `"Permission denied by user."` 错误给 LLM，LLM 会据此调整策略。

---

## 扩展新工具

添加新工具的步骤：

1. 在 `src/tools/` 下创建 `xxx_tool.h` 和 `xxx_tool.cpp`
2. 继承 `Tool` 基类，实现所有纯虚方法
3. 在 `Agent::register_tools()` 中注册：`tool_registry_.register_tool(std::make_unique<XxxTool>())`
4. 在 `tests/` 下添加单元测试

工具的 `schema()` 定义会自动传递给 LLM，无需修改其他代码。
