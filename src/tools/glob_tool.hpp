#pragma once

#include "../tool.hpp"

class GlobTool : public Tool {
 public:
  std::string name() const override { return "Glob"; }
  std::string description() const override {
    return "Fast file pattern matching. Supports glob patterns like \"**/*.cpp\" or "
           "\"src/**/*.h\". Returns matching file paths.";
  }

  json schema() const override {
    return {{"type", "object"},
            {"properties",
             {{"pattern",
               {{"type", "string"}, {"description", "The glob pattern to match files against"}}},
              {"path",
               {{"type", "string"},
                {"description", "The directory to search in (default: current directory)"}}}}},
            {"required", json::array({"pattern"})}};
  }

  PermissionLevel permission_level() const override { return PermissionLevel::AutoAllow; }

  ToolResult execute(const json& params) override;

  // Public for testing
  static bool match_pattern(const std::string& pattern, const std::string& path);
};
