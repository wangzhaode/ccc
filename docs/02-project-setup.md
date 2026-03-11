# 项目搭建

## 概述

cc.cpp 使用 CMake 构建系统，采用 C++17 标准，通过 FetchContent 管理第三方依赖，无需手动下载或安装外部库。

---

## 技术选型

### C++17

项目要求 C++17 标准（`CMAKE_CXX_STANDARD 17`），主要使用了以下 C++17 特性：

- **`std::filesystem`**：用于文件路径操作、目录遍历、文件存在检查等，贯穿所有工具实现
- **`std::optional` / 结构化绑定**：简化代码逻辑
- **`std::string_view`**：部分场景用于避免不必要的字符串拷贝

### 第三方依赖

通过 CMake 的 `FetchContent` 模块自动下载和管理：

| 依赖 | 版本 | 用途 |
|------|------|------|
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | v0.15.3 | HTTP 客户端，用于调用 LLM API（支持 HTTPS） |
| [nlohmann/json](https://github.com/nlohmann/json) | v3.11.3 | JSON 解析与生成，用于 API 请求/响应、工具定义 |
| [Google Test](https://github.com/google/googletest) | v1.14.0 | 单元测试框架（仅测试使用） |

### OpenSSL（可选但推荐）

cpp-httplib 的 HTTPS 支持依赖 OpenSSL。CMake 通过 `find_package(OpenSSL QUIET)` 自动检测：

- **找到 OpenSSL**：定义 `CPPHTTPLIB_OPENSSL_SUPPORT` 宏，启用 HTTPS，API 调用正常工作
- **未找到 OpenSSL**：编译会输出警告，HTTPS 不可用，API 调用将失败

macOS 上还会链接 Security framework 以支持系统证书链。

---

## CMake 配置详解

```cmake
cmake_minimum_required(VERSION 3.14)
project(cc_cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

关键配置说明：

- **CMake 3.14+**：FetchContent 模块的 `FetchContent_MakeAvailable` 需要 3.14 以上版本
- **`CMAKE_EXPORT_COMPILE_COMMANDS ON`**：生成 `compile_commands.json`，方便 IDE 和代码分析工具使用

### 依赖获取

```cmake
include(FetchContent)

FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

FetchContent_MakeAvailable(httplib json)
```

首次构建时 CMake 会自动从 GitHub 克隆这些依赖到 `build/_deps/` 目录，后续构建会使用缓存。

### 主程序目标

```cmake
file(GLOB_RECURSE SOURCES src/*.cpp)
add_executable(cc ${SOURCES})
target_include_directories(cc PRIVATE src include)
target_link_libraries(cc PRIVATE httplib::httplib nlohmann_json::nlohmann_json)
```

使用 `GLOB_RECURSE` 自动收集 `src/` 下所有 `.cpp` 文件，包括 `src/tools/` 子目录中的工具实现。

### 测试目标

```cmake
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

测试默认开启，可通过 `-DBUILD_TESTS=OFF` 关闭。测试的 CMake 配置在 `tests/CMakeLists.txt` 中，使用 Google Test 框架，并定义了一个辅助函数 `add_cc_test` 来简化测试目标的创建：

```cmake
function(add_cc_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE} ${LIB_SOURCES})
    target_include_directories(${TEST_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(${TEST_NAME} PRIVATE gtest_main httplib::httplib nlohmann_json::nlohmann_json)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()
```

测试目标会链接除 `main.cpp` 之外的所有源文件，使得测试可以直接使用各个模块的实现。

---

## 项目目录结构

```
cc.cpp/
├── CMakeLists.txt          # 顶层 CMake 配置
├── CC.md                   # 项目记忆文件（自动注入到 system prompt）
├── docs/                   # 文档目录
│   └── 01-core-features.md # 核心功能拆解
├── include/                # 公共头文件目录（当前为空，预留扩展）
├── src/                    # 源代码目录
│   ├── main.cpp            # 程序入口（REPL 循环）
│   ├── agent.h/cpp         # Agent Loop 核心逻辑
│   ├── api_client.h/cpp    # LLM API 客户端
│   ├── tool.h              # 工具基类和注册表
│   ├── memory.h/cpp        # 记忆系统（CC.md 加载）
│   ├── permission.h/cpp    # 权限管理
│   └── tools/              # 内置工具实现
│       ├── read_tool.h/cpp
│       ├── write_tool.h/cpp
│       ├── edit_tool.h/cpp
│       ├── bash_tool.h/cpp
│       ├── glob_tool.h/cpp
│       └── grep_tool.h/cpp
├── tests/                  # 测试目录
│   ├── CMakeLists.txt      # 测试 CMake 配置
│   ├── test_edit_tool.cpp
│   ├── test_glob_tool.cpp
│   └── test_grep_tool.cpp
└── build/                  # 构建输出目录（gitignore）
```

### 设计原则

- **头文件与实现分离**：每个模块有对应的 `.h` 和 `.cpp` 文件
- **工具独立目录**：所有工具实现放在 `src/tools/` 子目录，便于扩展
- **`include/` 预留**：用于将来提取公共接口，当前所有头文件在 `src/` 下

---

## 构建指令

### 基本构建

```bash
cmake -B build
cmake --build build
```

首次构建会下载依赖，可能需要几分钟。后续构建会使用缓存，速度更快。

### 运行

```bash
# 交互模式（REPL）
./build/cc

# 单次执行模式
./build/cc "请帮我查看当前目录的文件"
```

### 运行测试

```bash
cd build && ctest
```

或指定单个测试：

```bash
cd build && ctest -R test_edit_tool -V
```

### 清理重建

```bash
rm -rf build
cmake -B build && cmake --build build
```

### 配置文件

运行前需要在 `~/.cc/settings.json` 中配置 API 接入信息：

```json
{
  "provider": "openai",
  "api_key": "your-api-key",
  "base_url": "https://api.example.com",
  "model": "model-name"
}
```

配置项说明：

| 字段 | 说明 | 默认值 |
|------|------|--------|
| `provider` | API 协议类型，`"anthropic"` 或 `"openai"` | `"anthropic"` |
| `api_key` | API 密钥 | 无 |
| `base_url` | API 服务地址 | 取决于 provider |
| `model` | 使用的模型名称 | 取决于 provider |

也支持通过环境变量覆盖（`API_PROVIDER`、`ANTHROPIC_API_KEY`、`OPENAI_API_KEY`、`API_BASE_URL`、`MODEL`），但推荐使用配置文件。
