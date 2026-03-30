#include "memory.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <functional>

namespace fs = std::filesystem;

MemoryManager::MemoryManager() {
  project_root_ = find_project_root();
  const char* home = std::getenv("HOME");
  user_home_ = home ? home : "";
}

std::string MemoryManager::load_file_if_exists(const std::string& path) const {
  if (!fs::exists(path))
    return "";
  std::ifstream file(path);
  if (!file.is_open())
    return "";
  std::stringstream ss;
  ss << file.rdbuf();
  return ss.str();
}

std::string MemoryManager::find_project_root() const {
  return fs::current_path().string();
}

std::vector<std::string> MemoryManager::get_config_paths() const {
  std::vector<std::string> paths;

  // 1. Project root CCC.md
  if (!project_root_.empty()) {
    paths.push_back(project_root_ + "/CCC.md");
    paths.push_back(project_root_ + "/.ccc/CCC.md");
  }

  // 2. User-level CCC.md
  if (!user_home_.empty()) {
    paths.push_back(user_home_ + "/.ccc/CCC.md");
  }

  return paths;
}

std::string MemoryManager::build_memory_prompt() const {
  std::string result;

  for (const auto& path : get_config_paths()) {
    std::string content = load_file_if_exists(path);
    if (!content.empty()) {
      result += "\n# CCC.md (" + path + ")\n\n";
      result += content;
      result += "\n";
    }
  }

  return result;
}

std::string MemoryManager::project_hash() const {
  if (project_root_.empty())
    return "default";
  size_t hash = std::hash<std::string>{}(project_root_);
  std::ostringstream oss;
  oss << std::hex << hash;
  return oss.str();
}

std::string MemoryManager::auto_memory_dir() const {
  if (user_home_.empty())
    return "";

  // Replace path separators with dashes for the directory name
  std::string safe_path = project_root_;
  for (auto& c : safe_path) {
    if (c == '/')
      c = '-';
  }

  return user_home_ + "/.ccc/projects/" + safe_path + "/memory/";
}

std::string MemoryManager::load_file_lines(const std::string& path, int max_lines) const {
  if (!fs::exists(path))
    return "";
  std::ifstream file(path);
  if (!file.is_open())
    return "";

  std::string result;
  std::string line;
  int count = 0;
  while (std::getline(file, line) && count < max_lines) {
    result += line + "\n";
    count++;
  }
  return result;
}

std::string MemoryManager::load_auto_memory() const {
  std::string dir = auto_memory_dir();
  if (dir.empty())
    return "";

  std::string memory_path = dir + "MEMORY.md";
  return load_file_lines(memory_path, 200);
}

std::string MemoryManager::build_auto_memory_prompt() const {
  std::string dir = auto_memory_dir();
  if (dir.empty())
    return "";

  std::string memory_content = load_auto_memory();

  std::string result;
  result += "\n# Auto Memory\n\n";
  result += "You have a persistent auto memory directory at `" + dir + "`. ";
  result += "Its contents persist across conversations.\n\n";

  if (!memory_content.empty()) {
    result += "## Current MEMORY.md contents:\n\n";
    result += memory_content;
    result += "\n";
  }

  result += "## How to save memories:\n";
  result += "- Use the Write tool to create/update files in the memory directory\n";
  result += "- `MEMORY.md` is the main memory file (first 200 lines are loaded)\n";
  result += "- Create separate topic files for detailed notes\n";
  result += "- Save stable patterns, key decisions, user preferences\n";
  result += "- Do NOT save session-specific or speculative information\n";

  return result;
}
