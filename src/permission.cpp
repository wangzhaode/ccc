#include "permission.hpp"
#include <iostream>

bool PermissionManager::check_and_request(const std::string& tool_name, const json& params, PermissionLevel level) {
    // Auto-allow tools don't need confirmation
    if (level == PermissionLevel::AutoAllow) {
        return true;
    }

    // Check if user previously chose "always allow" for this tool
    if (always_allowed_.count(tool_name)) {
        return true;
    }

    return prompt_user(tool_name, params);
}

bool PermissionManager::prompt_user(const std::string& tool_name, const json& params) {
    std::cout << "\n\033[1;33m--- Permission Request ---\033[0m\n";
    std::cout << "Tool: \033[1m" << tool_name << "\033[0m\n";

    // Show relevant parameters
    if (tool_name == "Bash" && params.contains("command")) {
        std::cout << "Command: " << params["command"].get<std::string>() << "\n";
    } else if ((tool_name == "Write" || tool_name == "Edit" || tool_name == "Read") && params.contains("file_path")) {
        std::cout << "File: " << params["file_path"].get<std::string>() << "\n";
        if (tool_name == "Edit") {
            if (params.contains("old_string")) {
                std::string old_str = params["old_string"].get<std::string>();
                if (old_str.length() > 100) old_str = old_str.substr(0, 100) + "...";
                std::cout << "Replace: " << old_str << "\n";
            }
        }
    }

    std::cout << "\033[1;33m[y]es / [n]o / [a]lways allow " << tool_name << ": \033[0m";
    std::cout.flush();

    std::string input;
    std::getline(std::cin, input);

    if (input.empty()) return false;
    char choice = std::tolower(input[0]);

    if (choice == 'a') {
        always_allowed_.insert(tool_name);
        return true;
    }

    return choice == 'y';
}
