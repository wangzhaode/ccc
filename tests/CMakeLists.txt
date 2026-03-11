FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

# Collect all src/*.cpp except main.cpp for linking into tests
file(GLOB_RECURSE LIB_SOURCES ${CMAKE_SOURCE_DIR}/src/*.cpp)
list(FILTER LIB_SOURCES EXCLUDE REGEX "main\\.cpp$")

function(add_cc_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE} ${LIB_SOURCES})
    target_include_directories(${TEST_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(${TEST_NAME} PRIVATE gtest_main httplib::httplib nlohmann_json::nlohmann_json ftxui::screen ftxui::dom ftxui::component)
    if(OPENSSL_FOUND)
        target_link_libraries(${TEST_NAME} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    endif()
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

add_cc_test(test_edit_tool test_edit_tool.cpp)
add_cc_test(test_glob_tool test_glob_tool.cpp)
add_cc_test(test_grep_tool test_grep_tool.cpp)
