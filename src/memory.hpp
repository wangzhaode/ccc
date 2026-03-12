#pragma once

#include <string>
#include <vector>

class MemoryManager {
public:
    MemoryManager();

    // Build the memory section for system prompt (CC.md files)
    std::string build_memory_prompt() const;

    // Auto memory: load MEMORY.md from project memory directory
    std::string load_auto_memory() const;

    // Build auto memory prompt with directory path info for LLM
    std::string build_auto_memory_prompt() const;

    // Get the auto memory directory path
    std::string auto_memory_dir() const;

private:
    std::string project_root_;
    std::string user_home_;

    // Find and load CC.md files
    std::string load_file_if_exists(const std::string& path) const;
    std::string find_project_root() const;
    std::vector<std::string> get_config_paths() const;

    // Compute a hash of the project root path for directory naming
    std::string project_hash() const;

    // Load first N lines from a file
    std::string load_file_lines(const std::string& path, int max_lines) const;
};
