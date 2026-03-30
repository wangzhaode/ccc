#include "permission.hpp"
#include "ui.hpp"
#include <iostream>

bool PermissionManager::check_and_request(const std::string& tool_name, const json& params, PermissionLevel level) {
    // If auto-accept is enabled, allow everything
    if (auto_accept_all_) {
        return true;
    }

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
    // Build detail string for the permission box
    std::string detail;
    if (tool_name == "Bash" && params.contains("command")) {
        detail = "Command: " + params["command"].get<std::string>();
    } else if ((tool_name == "Write" || tool_name == "Edit" || tool_name == "Read") && params.contains("file_path")) {
        detail = "File: " + params["file_path"].get<std::string>();
        if (tool_name == "Edit" && params.contains("old_string")) {
            std::string old_str = params["old_string"].get<std::string>();
            if (old_str.length() > 100) old_str = old_str.substr(0, 100) + "...";
            detail += "\nReplace: " + old_str;
        }
    }

    char choice = ui::prompt_permission(tool_name, detail);

    if (choice == 'a') {
        always_allowed_.insert(tool_name);
        return true;
    }

    return choice == 'y';
}
