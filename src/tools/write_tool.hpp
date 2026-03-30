#pragma once

#include "../tool.hpp"

class WriteTool : public Tool {
 public:
  std::string name() const override { return "Write"; }
  std::string description() const override {
    return "Writes content to a file. Creates parent directories if needed. Overwrites existing "
           "files.";
  }

  json schema() const override {
    return {
        {"type", "object"},
        {"properties",
         {{"file_path",
           {{"type", "string"}, {"description", "The absolute path to the file to write"}}},
          {"content", {{"type", "string"}, {"description", "The content to write to the file"}}}}},
        {"required", json::array({"file_path", "content"})}};
  }

  PermissionLevel permission_level() const override { return PermissionLevel::NeedsConfirm; }

  ToolResult execute(const json& params) override;
};
