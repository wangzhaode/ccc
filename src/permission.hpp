#pragma once

#include <string>
#include <set>
#include <nlohmann/json.hpp>
#include "tool.hpp"

using json = nlohmann::json;

class PermissionManager {
public:
    // Check if tool execution is allowed, prompting user if needed
    bool check_and_request(const std::string& tool_name, const json& params, PermissionLevel level);

private:
    // Tools the user has chosen "always allow" for
    std::set<std::string> always_allowed_;

    // Display tool call details and prompt for confirmation
    bool prompt_user(const std::string& tool_name, const json& params);
};
