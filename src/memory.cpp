#include "memory.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

MemoryManager::MemoryManager() {
    project_root_ = find_project_root();
    const char* home = std::getenv("HOME");
    user_home_ = home ? home : "";
}

std::string MemoryManager::load_file_if_exists(const std::string& path) const {
    if (!fs::exists(path)) return "";
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

std::string MemoryManager::find_project_root() const {
    return fs::current_path().string();
}

std::vector<std::string> MemoryManager::get_config_paths() const {
    std::vector<std::string> paths;

    // 1. Project root CC.md
    if (!project_root_.empty()) {
        paths.push_back(project_root_ + "/CC.md");
        paths.push_back(project_root_ + "/.cc/CC.md");
    }

    // 2. User-level CC.md
    if (!user_home_.empty()) {
        paths.push_back(user_home_ + "/.cc/CC.md");
    }

    return paths;
}

std::string MemoryManager::build_memory_prompt() const {
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
