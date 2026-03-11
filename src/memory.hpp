#pragma once

#include <string>
#include <vector>

class MemoryManager {
public:
    MemoryManager();

    // Build the memory section for system prompt
    std::string build_memory_prompt() const;

private:
    std::string project_root_;
    std::string user_home_;

    // Find and load CC.md files
    std::string load_file_if_exists(const std::string& path) const;
    std::string find_project_root() const;
    std::vector<std::string> get_config_paths() const;
};
