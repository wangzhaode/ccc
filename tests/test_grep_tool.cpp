#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "tools/grep_tool.hpp"

namespace fs = std::filesystem;

class GrepToolTest : public ::testing::Test {
protected:
    std::string test_dir;
    GrepTool tool;

    void SetUp() override {
        test_dir = fs::temp_directory_path().string() + "/cc_test_grep";
        fs::create_directories(test_dir + "/src");

        write_file(test_dir + "/src/main.cpp",
            "#include <iostream>\nint main() {\n    std::cout << \"hello\";\n    return 0;\n}\n");
        write_file(test_dir + "/src/util.cpp",
            "#include \"util.h\"\nvoid helper() {\n    // TODO: implement\n}\n");
        write_file(test_dir + "/readme.txt",
            "This is a readme file.\nIt has multiple lines.\nEnd.\n");
    }

    void TearDown() override {
        fs::remove_all(test_dir);
    }

    void write_file(const std::string& path, const std::string& content) {
        std::ofstream f(path);
        f << content;
    }
};

TEST_F(GrepToolTest, FindPattern) {
    auto result = tool.execute({
        {"pattern", "include"},
        {"path", test_dir}
    });
    EXPECT_FALSE(result.is_error);
    EXPECT_NE(result.content.find("main.cpp"), std::string::npos);
    EXPECT_NE(result.content.find("util.cpp"), std::string::npos);
}

TEST_F(GrepToolTest, ContentMode) {
    auto result = tool.execute({
        {"pattern", "TODO"},
        {"path", test_dir},
        {"output_mode", "content"}
    });
    EXPECT_FALSE(result.is_error);
    EXPECT_NE(result.content.find("TODO: implement"), std::string::npos);
    EXPECT_NE(result.content.find(":3:"), std::string::npos); // line number
}

TEST_F(GrepToolTest, GlobFilter) {
    auto result = tool.execute({
        {"pattern", "include"},
        {"path", test_dir},
        {"glob", "**/*.cpp"}
    });
    EXPECT_FALSE(result.is_error);
    EXPECT_NE(result.content.find("main.cpp"), std::string::npos);
    EXPECT_EQ(result.content.find("readme.txt"), std::string::npos);
}

TEST_F(GrepToolTest, NoMatches) {
    auto result = tool.execute({
        {"pattern", "xyz_not_here"},
        {"path", test_dir}
    });
    EXPECT_FALSE(result.is_error);
    EXPECT_NE(result.content.find("No matches"), std::string::npos);
}

TEST_F(GrepToolTest, InvalidRegex) {
    auto result = tool.execute({
        {"pattern", "[invalid"},
        {"path", test_dir}
    });
    EXPECT_TRUE(result.is_error);
    EXPECT_NE(result.content.find("Invalid regex"), std::string::npos);
}

TEST_F(GrepToolTest, SingleFile) {
    auto result = tool.execute({
        {"pattern", "hello"},
        {"path", test_dir + "/src/main.cpp"},
        {"output_mode", "content"}
    });
    EXPECT_FALSE(result.is_error);
    EXPECT_NE(result.content.find("hello"), std::string::npos);
}
