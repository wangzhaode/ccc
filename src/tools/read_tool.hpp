#pragma once

#include "../tool.hpp"

class ReadTool : public Tool {
 public:
  std::string name() const override { return "Read"; }
  std::string description() const override {
    return "Reads a file from the local filesystem. Returns file content with line numbers.";
  }

  json schema() const override {
    return {
        {"type", "object"},
        {"properties",
         {{"file_path",
           {{"type", "string"}, {"description", "The absolute path to the file to read"}}},
          {"offset",
           {{"type", "number"}, {"description", "Line number to start reading from (1-based)"}}},
          {"limit", {{"type", "number"}, {"description", "Maximum number of lines to read"}}}}},
        {"required", json::array({"file_path"})}};
  }

  PermissionLevel permission_level() const override { return PermissionLevel::AutoAllow; }

  ToolResult execute(const json& params) override;
};
