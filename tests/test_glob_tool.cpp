#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "tools/glob_tool.hpp"

namespace fs = std::filesystem;

class GlobToolTest : public ::testing::Test {
 protected:
  std::string test_dir;
  GlobTool tool;

  void SetUp() override {
    test_dir = fs::temp_directory_path().string() + "/cc_test_glob";
    fs::create_directories(test_dir + "/src");
    fs::create_directories(test_dir + "/include");

    touch(test_dir + "/src/main.cpp");
    touch(test_dir + "/src/util.cpp");
    touch(test_dir + "/src/util.h");
    touch(test_dir + "/include/lib.h");
    touch(test_dir + "/README.md");
  }

  void TearDown() override { fs::remove_all(test_dir); }

  void touch(const std::string& path) {
    std::ofstream f(path);
    f << "test";
  }
};

TEST_F(GlobToolTest, MatchCppFiles) {
  auto result = tool.execute({{"pattern", "**/*.cpp"}, {"path", test_dir}});
  EXPECT_FALSE(result.is_error);
  EXPECT_NE(result.content.find("main.cpp"), std::string::npos);
  EXPECT_NE(result.content.find("util.cpp"), std::string::npos);
  EXPECT_EQ(result.content.find("util.h"), std::string::npos);
}

TEST_F(GlobToolTest, MatchHeaderFiles) {
  auto result = tool.execute({{"pattern", "**/*.h"}, {"path", test_dir}});
  EXPECT_FALSE(result.is_error);
  EXPECT_NE(result.content.find("util.h"), std::string::npos);
  EXPECT_NE(result.content.find("lib.h"), std::string::npos);
}

TEST_F(GlobToolTest, MatchSpecificDir) {
  auto result = tool.execute({{"pattern", "src/*.cpp"}, {"path", test_dir}});
  EXPECT_FALSE(result.is_error);
  EXPECT_NE(result.content.find("main.cpp"), std::string::npos);
  EXPECT_EQ(result.content.find("lib.h"), std::string::npos);
}

TEST_F(GlobToolTest, NoMatches) {
  auto result = tool.execute({{"pattern", "**/*.py"}, {"path", test_dir}});
  EXPECT_FALSE(result.is_error);
  EXPECT_NE(result.content.find("No files"), std::string::npos);
}

TEST_F(GlobToolTest, PatternMatching) {
  EXPECT_TRUE(GlobTool::match_pattern("*.cpp", "main.cpp"));
  EXPECT_FALSE(GlobTool::match_pattern("*.cpp", "main.h"));
  EXPECT_TRUE(GlobTool::match_pattern("**/*.cpp", "src/main.cpp"));
  EXPECT_TRUE(GlobTool::match_pattern("**/*.cpp", "a/b/c.cpp"));
  EXPECT_TRUE(GlobTool::match_pattern("src/*.h", "src/util.h"));
  EXPECT_FALSE(GlobTool::match_pattern("src/*.h", "include/lib.h"));
}

TEST_F(GlobToolTest, InvalidDirectory) {
  auto result = tool.execute({{"pattern", "*.cpp"}, {"path", "/nonexistent"}});
  EXPECT_TRUE(result.is_error);
}
