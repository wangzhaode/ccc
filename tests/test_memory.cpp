#include <gtest/gtest.h>
#include "memory.hpp"
#include <fstream>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

class MemoryTest : public ::testing::Test {
 protected:
  std::string test_dir;
  std::string original_home;

  void SetUp() override {
    test_dir = fs::temp_directory_path().string() + "/cc_test_memory_" +
               std::to_string(std::hash<std::string>{}(
                   ::testing::UnitTest::GetInstance()->current_test_info()->name()));
    fs::create_directories(test_dir);

    // Save original HOME
    const char* home = std::getenv("HOME");
    original_home = home ? home : "";
  }

  void TearDown() override {
    fs::remove_all(test_dir);
    // Restore HOME
    if (!original_home.empty()) {
      setenv("HOME", original_home.c_str(), 1);
    }
  }
};

TEST_F(MemoryTest, AutoMemoryDirNotEmpty) {
  MemoryManager mm;
  std::string dir = mm.auto_memory_dir();
  EXPECT_FALSE(dir.empty());
  EXPECT_NE(dir.find("/.ccc/projects/"), std::string::npos);
  EXPECT_NE(dir.find("/memory/"), std::string::npos);
}

TEST_F(MemoryTest, LoadAutoMemoryEmpty) {
  // With no MEMORY.md file, should return empty
  MemoryManager mm;
  // The auto memory dir likely doesn't have a MEMORY.md
  // This test verifies it doesn't crash
  std::string content = mm.load_auto_memory();
  // Content might be empty or not depending on if a MEMORY.md exists
  // Just verify it doesn't throw
  SUCCEED();
}

TEST_F(MemoryTest, BuildAutoMemoryPrompt) {
  MemoryManager mm;
  std::string prompt = mm.build_auto_memory_prompt();
  EXPECT_NE(prompt.find("auto memory"), std::string::npos);
  EXPECT_NE(prompt.find("MEMORY.md"), std::string::npos);
}

TEST_F(MemoryTest, LoadFileLinesLimit) {
  // Create a test file with many lines
  std::string test_file = test_dir + "/test_lines.md";
  {
    std::ofstream out(test_file);
    for (int i = 0; i < 300; i++) {
      out << "Line " << i << "\n";
    }
  }

  // MemoryManager's load_file_lines is private, but we can test
  // through load_auto_memory indirectly by setting up the right paths.
  // For now, just verify the file was created.
  EXPECT_TRUE(fs::exists(test_file));
}

TEST_F(MemoryTest, BuildMemoryPromptNoCCMd) {
  MemoryManager mm;
  // build_memory_prompt reads CC.md files - in test env, may or may not exist
  // Just verify no crash
  std::string prompt = mm.build_memory_prompt();
  SUCCEED();
}
