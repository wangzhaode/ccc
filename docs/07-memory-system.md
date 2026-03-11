# 记忆系统实现

## 概述

记忆系统负责加载 CC.md 文件并将其内容注入到 LLM 的 system prompt 中。通过这个机制，LLM 在每次对话中都能获取项目的上下文信息、编码规范和工作指引。

cc.cpp 使用自己的配置目录 `~/.cc/`，与 Claude Code 的 `~/.claude/` 互不干扰。

---

## CC.md 的作用

CC.md 本质上是一个**自动注入的提示词文件**。用户在文件中写下项目相关的说明和规则，系统启动时自动读取并作为 system prompt 的一部分发送给 LLM。

典型内容包括：
- 项目描述和技术栈
- 编码规范和约定
- 构建和测试指令
- 目录结构说明
- 特定的工作流程要求

---

## MemoryManager 设计

定义在 `src/memory.h` 和 `src/memory.cpp` 中：

```cpp
class MemoryManager {
public:
    MemoryManager();
    std::string build_memory_prompt() const;

private:
    std::string project_root_;
    std::string user_home_;

    std::string load_file_if_exists(const std::string& path) const;
    std::string find_project_root() const;
    std::vector<std::string> get_config_paths() const;
};
```

### 初始化

构造函数完成两件事：
1. **确定项目根目录**：当前实现直接使用 `std::filesystem::current_path()` 作为项目根目录
2. **获取用户主目录**：通过 `std::getenv("HOME")` 获取

---

## CC.md 加载路径

`get_config_paths()` 返回按优先级排列的 CC.md 搜索路径：

| 优先级 | 路径 | 说明 |
|--------|------|------|
| 1 | `<项目根目录>/CC.md` | 项目级配置，放在项目根目录 |
| 2 | `<项目根目录>/.cc/CC.md` | 项目级配置，放在 .cc 子目录（避免根目录杂乱） |
| 3 | `~/.cc/CC.md` | 用户级配置，跨项目共享 |

### 多路径合并

**所有存在的 CC.md 文件都会被加载**，内容依次拼接。这意味着可以同时使用项目级和用户级配置：

- 项目 CC.md：定义项目特定的规范
- 用户 CC.md：定义个人偏好和全局规则

### 文件读取

```cpp
std::string load_file_if_exists(const std::string& path) const {
    if (!fs::exists(path)) return "";
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}
```

对于不存在或无法打开的文件，静默返回空字符串，不报错。这保证了系统在没有 CC.md 的项目中也能正常运行。

---

## System Prompt 注入

`build_memory_prompt()` 方法将所有找到的 CC.md 内容合并为一段文本：

```cpp
std::string build_memory_prompt() const {
    std::string result;
    for (const auto& path : get_config_paths()) {
        std::string content = load_file_if_exists(path);
        if (!content.empty()) {
            result += "\n# CC.md (" + path + ")\n\n";
            result += content;
            result += "\n";
        }
    }
    return result;
}
```

每个文件的内容前都有一个标题行，标明了文件的完整路径，便于 LLM 区分不同来源的指令。

### 在 system prompt 中的位置

`Agent::build_system_prompt()` 中，记忆内容被放在 system prompt 的末尾：

```
System Prompt 结构：
├─ 角色定义（固定文本）
├─ 环境信息（工作目录、操作系统）
├─ 工具使用指南（固定文本）
└─ # Project Instructions
    └─ CC.md 内容（由 MemoryManager 提供）
```

只有当 `build_memory_prompt()` 返回非空内容时，才会添加 `# Project Instructions` 部分。

---

## 配置目录结构

cc.cpp 的所有配置统一存放在 `~/.cc/` 目录下：

```
~/.cc/
├── settings.json    ← API 配置（api_key, base_url, model, provider）
└── CC.md            ← 用户级记忆文件（可选）
```

项目级配置：

```
<项目根目录>/
├── CC.md            ← 项目级记忆文件
└── .cc/
    └── CC.md        ← 项目级记忆文件（隐藏目录方式）
```

---

## 项目根目录检测

当前实现使用简单策略：

```cpp
std::string find_project_root() const {
    return fs::current_path().string();
}
```

直接将当前工作目录视为项目根目录。

### 潜在改进

更完善的实现可以向上遍历目录树，查找标志性文件来确定项目根目录：
- `.git/` 目录
- `CMakeLists.txt`
- `package.json`
- `.cc/` 目录

---

## 调用时机

`build_memory_prompt()` 在每次 Agent Loop 迭代中都会被调用（通过 `build_system_prompt()`），而非仅在启动时加载一次。这意味着：

- 如果用户在会话过程中修改了 CC.md，**下次 LLM 调用就能看到更新**
- 代价是每次迭代都有文件 I/O 开销，但 CC.md 通常很小，影响可忽略

---

## 使用示例

假设项目结构如下：

```
/home/user/project/
├── CC.md             ← 内容："这是一个 Python 项目，使用 pytest 测试"
├── .cc/
│   └── CC.md         ← 内容："优先使用 type hints"
└── src/
    └── ...
```

用户主目录：

```
/home/user/
└── .cc/
    ├── settings.json  ← API 配置
    └── CC.md          ← 内容："回复使用中文"
```

那么 `build_memory_prompt()` 的输出为：

```
# CC.md (/home/user/project/CC.md)

这是一个 Python 项目，使用 pytest 测试

# CC.md (/home/user/project/.cc/CC.md)

优先使用 type hints

# CC.md (/home/user/.cc/CC.md)

回复使用中文
```

三个文件的内容全部被注入到 system prompt 中，LLM 会同时遵循所有指令。
