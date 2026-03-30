#pragma once

#include <string>
#include <set>
#include <nlohmann/json.hpp>
#include "tool.hpp"

using json = nlohmann::json;

class PermissionManager {
 public:
  // Auto-accept all permissions without prompting
  void set_auto_accept(bool enabled) { auto_accept_all_ = enabled; }
  bool auto_accept() const { return auto_accept_all_; }

  // Check if tool execution is allowed, prompting user if needed
  bool check_and_request(const std::string& tool_name, const json& params, PermissionLevel level);

 private:
  bool auto_accept_all_ = false;
  // Tools the user has chosen "always allow" for
  std::set<std::string> always_allowed_;

  // Display tool call details and prompt for confirmation
  bool prompt_user(const std::string& tool_name, const json& params);
};
