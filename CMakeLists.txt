cmake_minimum_required(VERSION 3.14)

project(cc_cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Windows: Use static runtime library (/MT) for all targets
if(MSVC)
    # Replace default /MD with /MT for all build configurations
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /MT")
    set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} /MT")
    # Google Test should also use static CRT (default behavior)
    set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
endif()

# Find OpenSSL for HTTPS support
find_package(OpenSSL QUIET)
if(OPENSSL_FOUND)
    add_definitions(-DCPPHTTPLIB_OPENSSL_SUPPORT)
    message(STATUS "OpenSSL found: HTTPS enabled")
else()
    message(WARNING "OpenSSL not found: HTTPS disabled, API calls will fail")
endif()

# Fetch dependencies
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

FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG v5.0.0
)

set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(httplib json ftxui)

# Main executable
file(GLOB_RECURSE SOURCES src/*.cpp)
add_executable(ccc ${SOURCES})
target_include_directories(ccc PRIVATE src include)
target_link_libraries(ccc PRIVATE httplib::httplib nlohmann_json::nlohmann_json ftxui::screen ftxui::dom ftxui::component)

if(OPENSSL_FOUND)
    target_link_libraries(ccc PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

# Platform-specific settings
if(APPLE)
    find_library(SECURITY_FRAMEWORK Security)
    if(SECURITY_FRAMEWORK)
        target_link_libraries(ccc PRIVATE ${SECURITY_FRAMEWORK})
    endif()
endif()

# Tests
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
