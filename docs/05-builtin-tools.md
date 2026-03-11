# 内置工具实现

## 概述

cc.cpp 当前实现了 6 个内置工具，覆盖了文件读写、命令执行和代码搜索的基本需求。所有工具均继承自 `Tool` 基类，实现统一的接口。

---

## 1. Read 工具

**文件**：`src/tools/read_tool.h`、`src/tools/read_tool.cpp`

**功能**：读取本地文件内容，支持行号偏移和行数限制，输出带行号的格式。

**权限级别**：`AutoAllow`（无需确认）

### 参数

| 参数 | 类型 | 必需 | 默认值 | 说明 |
|------|------|------|--------|------|
| `file_path` | string | 是 | - | 文件的绝对路径 |
| `offset` | number | 否 | 1 | 起始行号（1-based） |
| `limit` | number | 否 | 2000 | 最大读取行数 |

### 实现细节

1. **文件验证**：检查文件是否存在、是否为目录、是否可打开
2. **逐行读取**：使用 `std::getline` 逐行读取，跳过 offset 之前的行
3. **行号格式**：输出格式为 `     行号\t内容`，模拟 `cat -n` 的显示风格
4. **长行截断**：超过 2000 字符的行会被截断并添加 `... (truncated)` 标记
5. **空文件处理**：如果没有输出任何行（空文件或 offset 超出范围），返回提示信息

### 输出示例

```
     1	#include <iostream>
     2	#include <string>
     3
     4	int main() {
     5	    std::cout << "hello" << std::endl;
     6	}
```

---

## 2. Write 工具

**文件**：`src/tools/write_tool.h`、`src/tools/write_tool.cpp`

**功能**：将内容写入文件，支持自动创建父目录。如果文件已存在则覆盖。

**权限级别**：`NeedsConfirm`（需要用户确认）

### 参数

| 参数 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `file_path` | string | 是 | 文件的绝对路径 |
| `content` | string | 是 | 要写入的文件内容 |

### 实现细节

1. **自动创建目录**：使用 `std::filesystem::create_directories` 递归创建不存在的父目录
2. **错误处理**：目录创建失败、文件打开失败、写入失败均返回错误
3. **完全覆盖**：使用 `std::ofstream` 默认模式打开文件，会覆盖已有内容

### 设计决策

- 没有实现"追加模式"，因为 Claude Code 的 Write 工具定义为完全覆盖语义
- 自动创建目录是重要特性，避免 LLM 需要先执行 `mkdir -p` 再写入

---

## 3. Edit 工具

**文件**：`src/tools/edit_tool.h`、`src/tools/edit_tool.cpp`

**功能**：通过精确字符串匹配替换来编辑文件。这是 Claude Code 最核心的编辑工具。

**权限级别**：`NeedsConfirm`（需要用户确认）

### 参数

| 参数 | 类型 | 必需 | 默认值 | 说明 |
|------|------|------|--------|------|
| `file_path` | string | 是 | - | 文件的绝对路径 |
| `old_string` | string | 是 | - | 要替换的原文本 |
| `new_string` | string | 是 | - | 替换后的新文本 |
| `replace_all` | boolean | 否 | false | 是否替换所有匹配项 |

### 实现细节

1. **唯一性检查**：扫描文件内容，计算 `old_string` 出现的次数
   - 0 次：返回错误 "old_string not found in file"
   - 1 次：正常替换
   - 多次且 `replace_all` 为 false：返回错误，提示出现次数，要求提供更多上下文
   - 多次且 `replace_all` 为 true：替换所有匹配项

2. **相同字符串检查**：`old_string == new_string` 时直接返回错误，避免无意义操作

3. **替换实现**：使用 `std::string::find` + `std::string::replace` 进行原地替换

4. **原子性**：先读取完整文件内容到内存，替换后再写回。不存在中间状态。

### 设计原理

使用字符串匹配而非行号的原因：
- 在 Agent Loop 中文件可能被多次编辑，行号会漂移
- 字符串匹配更鲁棒，只要内容唯一就能准确定位
- 唯一性约束迫使 LLM 提供足够的上下文，减少误编辑

### 单元测试

`tests/test_edit_tool.cpp` 覆盖了以下场景：
- 基本替换
- 非唯一字符串报错
- `replace_all` 全部替换
- 字符串未找到
- 文件不存在
- 相同字符串报错

---

## 4. Bash 工具

**文件**：`src/tools/bash_tool.h`、`src/tools/bash_tool.cpp`

**功能**：执行 bash 命令，捕获 stdout 和 stderr 输出，支持超时机制。

**权限级别**：`NeedsConfirm`（需要用户确认）

### 参数

| 参数 | 类型 | 必需 | 默认值 | 说明 |
|------|------|------|--------|------|
| `command` | string | 是 | - | 要执行的 bash 命令 |
| `timeout` | number | 否 | 120000 | 超时时间（毫秒） |

### 实现细节

采用 `fork` + `exec` 模型，而非简单的 `popen`，以获得更精细的控制：

#### 进程创建

1. **创建管道**：`pipe(pipefd)` 创建用于捕获输出的管道
2. **Fork 子进程**：
   - 子进程：重定向 stdout 和 stderr 到管道写端，通过 `execl` 执行 `/bin/bash -c <command>`
   - 父进程：关闭管道写端，从读端读取输出

#### 超时机制

使用 `select()` 实现非阻塞读取和超时控制：

```
记录开始时间
循环：
  计算剩余超时时间
  select(管道fd, 剩余超时) → 等待数据或超时
  ├─ 有数据 → read() 并累积到 output
  ├─ 超时 → 设置 timed_out 标志，跳出循环
  └─ 错误 → 跳出循环
```

#### 超时处理

```
超时时：
  kill(pid, SIGTERM)     ← 先发送 SIGTERM，给进程优雅退出的机会
  usleep(100000)         ← 等待 100ms
  kill(pid, SIGKILL)     ← 强制杀死
  waitpid()              ← 回收子进程
  返回错误（附带已收集的输出）
```

#### 输出限制

- 输出大小限制为 1MB，超出时截断并添加提示
- 非零退出码视为错误，返回 `is_error: true`

### 安全考量

当前实现没有沙箱限制，命令可以访问用户的全部权限。安全性完全依赖权限系统的用户确认机制。

---

## 5. Glob 工具

**文件**：`src/tools/glob_tool.h`、`src/tools/glob_tool.cpp`

**功能**：按 glob 模式匹配文件路径，支持 `*`、`?` 和 `**` 通配符。返回按修改时间倒序排列的文件列表。

**权限级别**：`AutoAllow`（无需确认）

### 参数

| 参数 | 类型 | 必需 | 默认值 | 说明 |
|------|------|------|--------|------|
| `pattern` | string | 是 | - | glob 模式（如 `**/*.cpp`） |
| `path` | string | 否 | 当前目录 | 搜索的根目录 |

### 模式匹配算法

自行实现了 glob 模式匹配（`match_pattern` 静态方法），不依赖外部库：

#### 第一层：路径段匹配

将模式和路径按 `/` 分割为段，逐段匹配。对 `**` 使用递归回溯：

```
match(pattern_idx, path_idx):
  如果模式段是 "**":
    尝试匹配 0 个、1 个、2 个... 直到所有剩余路径段
  否则:
    用 match_segment 匹配当前段
```

#### 第二层：单段匹配

单段匹配支持 `*`（任意字符序列）和 `?`（单个字符）：

```
使用双指针 + 回溯算法：
  普通字符 / ? → 逐个匹配
  * → 记录回溯点，尝试匹配 0 个字符起
  不匹配 → 回溯到上一个 * 的位置，多消耗一个字符
```

### 实现细节

1. **目录遍历**：使用 `std::filesystem::recursive_directory_iterator`，设置 `skip_permission_denied` 避免权限错误中断
2. **相对路径计算**：用 `std::filesystem::relative` 将绝对路径转为相对于搜索目录的路径，然后与模式匹配
3. **排序**：按文件修改时间倒序排列（最新文件优先）
4. **仅匹配常规文件**：跳过目录、符号链接等

### 单元测试

`tests/test_glob_tool.cpp` 测试了模式匹配算法的各种情况。`match_pattern` 方法被声明为 `public static`，方便直接测试。

---

## 6. Grep 工具

**文件**：`src/tools/grep_tool.h`、`src/tools/grep_tool.cpp`

**功能**：使用正则表达式搜索文件内容，支持 glob 过滤和两种输出模式。

**权限级别**：`AutoAllow`（无需确认）

### 参数

| 参数 | 类型 | 必需 | 默认值 | 说明 |
|------|------|------|--------|------|
| `pattern` | string | 是 | - | 正则表达式模式 |
| `path` | string | 否 | 当前目录 | 搜索路径（文件或目录） |
| `glob` | string | 否 | 无 | 文件过滤的 glob 模式 |
| `output_mode` | string | 否 | `"files_with_matches"` | 输出模式 |

### 输出模式

- **`files_with_matches`**（默认）：只输出匹配文件的路径，每个文件最多匹配一次
- **`content`**：输出匹配行的详细信息，格式为 `文件路径:行号: 内容`

### 实现细节

1. **正则引擎**：使用 `std::regex`（ECMAScript 模式 + optimize 标志）
2. **文件收集**：
   - 如果 path 是文件 → 直接搜索该文件
   - 如果 path 是目录 → 递归遍历所有文件
3. **过滤规则**：
   - 跳过隐藏目录（路径中含 `/.` 或以 `.` 开头）
   - 如果指定了 glob 参数，使用 `GlobTool::match_pattern` 过滤文件
4. **结果限制**：最多返回 500 个匹配，超出后截断
5. **错误容忍**：对引起 `regex_error` 的行（如过长的行）直接跳过，不中断搜索

### 与 GlobTool 的协作

GrepTool 复用了 `GlobTool::match_pattern` 静态方法来实现文件 glob 过滤，避免了代码重复。这也是 `match_pattern` 被声明为 `public static` 的原因之一。

### 单元测试

`tests/test_grep_tool.cpp` 测试了正则搜索、glob 过滤和输出模式等功能。

---

## 工具对照表

| 工具 | 文件 | 权限 | 主要用途 |
|------|------|------|----------|
| Read | `src/tools/read_tool.*` | AutoAllow | 读取文件内容 |
| Write | `src/tools/write_tool.*` | NeedsConfirm | 创建/覆盖文件 |
| Edit | `src/tools/edit_tool.*` | NeedsConfirm | 精确字符串替换编辑 |
| Bash | `src/tools/bash_tool.*` | NeedsConfirm | 执行 shell 命令 |
| Glob | `src/tools/glob_tool.*` | AutoAllow | 文件模式匹配搜索 |
| Grep | `src/tools/grep_tool.*` | AutoAllow | 文件内容正则搜索 |
