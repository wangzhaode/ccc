# 权限系统实现

## 概述

权限系统控制工具的执行许可，防止 LLM 在未经用户同意的情况下执行危险操作。它通过权限级别分类、终端交互确认和"始终允许"缓存三个机制协同工作。

---

## 权限级别

定义在 `src/tool.h` 中：

```cpp
enum class PermissionLevel {
    AutoAllow,      // 无需确认，直接执行
    NeedsConfirm    // 需要用户在终端确认
};
```

### 级别划分

| 权限级别 | 工具 | 理由 |
|----------|------|------|
| `AutoAllow` | Read, Glob, Grep | 只读操作，不修改文件系统，不执行外部命令 |
| `NeedsConfirm` | Write, Edit, Bash | 会修改文件或执行任意命令，可能产生不可逆影响 |

每个工具通过 `permission_level()` 方法声明自己所需的权限级别。这是工具接口的一部分，在工具注册时就确定了。

---

## PermissionManager

定义在 `src/permission.h` 和 `src/permission.cpp` 中：

```cpp
class PermissionManager {
public:
    bool check_and_request(const std::string& tool_name,
                           const json& params,
                           PermissionLevel level);
private:
    std::set<std::string> always_allowed_;
    bool prompt_user(const std::string& tool_name, const json& params);
};
```

### 检查流程

`check_and_request()` 方法的决策逻辑：

```
check_and_request(tool_name, params, level)
  │
  ├─ level == AutoAllow？
  │   └─ 是 → 返回 true（直接允许）
  │
  ├─ tool_name 在 always_allowed_ 集合中？
  │   └─ 是 → 返回 true（缓存命中）
  │
  └─ prompt_user(tool_name, params)
      └─ 显示终端确认对话 → 返回用户选择
```

---

## 终端确认 UI

当工具需要确认时，`prompt_user()` 方法在终端显示确认对话框：

### 显示格式

```
--- Permission Request ---
Tool: Bash
Command: git status

[y]es / [n]o / [a]lways allow Bash:
```

### 参数展示

根据工具类型显示不同的关键参数：

| 工具 | 显示的参数 |
|------|-----------|
| Bash | `command` 字段的完整内容 |
| Write | `file_path` 字段 |
| Edit | `file_path` 字段 + `old_string` 的前 100 字符 |
| Read | `file_path` 字段（Read 实际是 AutoAllow，不会触发） |

### 用户选择

| 输入 | 含义 |
|------|------|
| `y` | 允许本次执行 |
| `n` | 拒绝本次执行 |
| `a` | 允许本次执行，并将该工具加入"始终允许"列表 |
| 空输入 | 视为拒绝 |

输入不区分大小写（通过 `std::tolower` 转换）。

### 视觉样式

- 标题"Permission Request"使用黄色加粗（`\033[1;33m`）
- 工具名称使用白色加粗（`\033[1m`）
- 提示选项使用黄色加粗

---

## "始终允许"缓存

### 存储机制

```cpp
std::set<std::string> always_allowed_;
```

使用 `std::set<std::string>` 存储用户选择"始终允许"的工具名称。

### 缓存粒度

当前实现的缓存粒度是**工具级别**，即一旦用户选择"always allow Bash"，后续所有 Bash 调用都不再需要确认，无论命令内容是什么。

### 生命周期

缓存存储在 `PermissionManager` 实例的内存中，生命周期与 `Agent` 对象一致：
- **会话内持久**：同一次 REPL 会话中，缓存一直有效
- **会话间不持久**：程序退出后缓存消失，下次启动需要重新确认

---

## 与工具执行的集成

权限检查在 `Agent::execute_tool_call()` 中进行，位于工具执行之前：

```cpp
json Agent::execute_tool_call(const json& tool_use_block) {
    // 1. 解析参数
    std::string tool_name = tool_use_block["name"];
    json input = tool_use_block.value("input", json::object());

    // 2. 获取工具指针
    auto* tool = tool_registry_.get(tool_name);

    // 3. 权限检查
    if (tool) {
        if (!permission_manager_.check_and_request(
                tool_name, input, tool->permission_level())) {
            // 权限被拒绝 → 返回错误给 LLM
            return {
                {"type", "tool_result"},
                {"tool_use_id", tool_id},
                {"content", "Permission denied by user."},
                {"is_error", true}
            };
        }
    }

    // 4. 执行工具
    ToolResult result = tool_registry_.execute(tool_name, input);
    // ...
}
```

### 拒绝时的行为

权限被拒绝时：
1. 工具**不会执行**
2. 返回 `is_error: true` 的 `"Permission denied by user."` 结果
3. LLM 收到错误后通常会：
   - 解释为什么需要执行该操作
   - 尝试用其他方式达成目标
   - 询问用户是否有替代方案

---

## 设计权衡

### 当前简化

相比 Claude Code 的完整权限系统，当前实现做了以下简化：

1. **无模式切换**：没有 trust/auto/manual 三种模式，固定为按工具权限级别决定
2. **无命令模式匹配**：Bash 工具的"始终允许"是对所有命令生效，没有按命令模式（如 `git *`）细分
3. **无路径模式匹配**：Edit/Write 的"始终允许"是全局的，没有按文件路径模式限制
4. **无持久化存储**：缓存仅在内存中，不写入配置文件

### 安全性分析

- **只读工具安全**：Read/Glob/Grep 只读取文件系统信息，不产生副作用
- **写入工具可控**：Write/Edit 修改文件前需要确认，用户可以看到目标文件
- **Bash 风险最高**：可执行任意命令，当前依赖用户审查命令内容
- **"始终允许"的风险**：选择 always allow Bash 后，后续所有命令都不再确认，存在安全隐患
