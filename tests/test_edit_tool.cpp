#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include "tools/edit_tool.hpp"

namespace fs = std::filesystem;

class EditToolTest : public ::testing::Test {
 protected:
  std::string test_dir;
  EditTool tool;

  void SetUp() override {
    test_dir = fs::temp_directory_path().string() + "/cc_test_edit";
    fs::create_directories(test_dir);
  }

  void TearDown() override { fs::remove_all(test_dir); }

  std::string create_file(const std::string& name, const std::string& content) {
    std::string path = test_dir + "/" + name;
    std::ofstream f(path);
    f << content;
    return path;
  }

  std::string read_file(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
  }
};

TEST_F(EditToolTest, BasicReplacement) {
  auto path = create_file("test.txt", "hello world\nfoo bar\n");
  auto result = tool.execute(
      {{"file_path", path}, {"old_string", "hello world"}, {"new_string", "hello earth"}});
  EXPECT_FALSE(result.is_error);
  EXPECT_EQ(read_file(path), "hello earth\nfoo bar\n");
}

TEST_F(EditToolTest, NonUniqueStringFails) {
  auto path = create_file("test.txt", "aaa\naaa\n");
  auto result = tool.execute({{"file_path", path}, {"old_string", "aaa"}, {"new_string", "bbb"}});
  EXPECT_TRUE(result.is_error);
  EXPECT_NE(result.content.find("2 times"), std::string::npos);
}

TEST_F(EditToolTest, ReplaceAll) {
  auto path = create_file("test.txt", "aaa\naaa\n");
  auto result = tool.execute(
      {{"file_path", path}, {"old_string", "aaa"}, {"new_string", "bbb"}, {"replace_all", true}});
  EXPECT_FALSE(result.is_error);
  EXPECT_EQ(read_file(path), "bbb\nbbb\n");
}

TEST_F(EditToolTest, StringNotFound) {
  auto path = create_file("test.txt", "hello world\n");
  auto result = tool.execute(
      {{"file_path", path}, {"old_string", "not here"}, {"new_string", "replacement"}});
  EXPECT_TRUE(result.is_error);
  EXPECT_NE(result.content.find("not found"), std::string::npos);
}

TEST_F(EditToolTest, FileNotFound) {
  auto result = tool.execute(
      {{"file_path", test_dir + "/nonexistent.txt"}, {"old_string", "a"}, {"new_string", "b"}});
  EXPECT_TRUE(result.is_error);
}

TEST_F(EditToolTest, IdenticalStringsFails) {
  auto path = create_file("test.txt", "hello\n");
  auto result =
      tool.execute({{"file_path", path}, {"old_string", "hello"}, {"new_string", "hello"}});
  EXPECT_TRUE(result.is_error);
}
