#pragma once

#include "../tool.hpp"

class EditTool : public Tool {
 public:
  std::string name() const override { return "Edit"; }
  std::string description() const override {
    return "Performs exact string replacements in files. The old_string must be unique in the file "
           "unless replace_all is true.";
  }

  json schema() const override {
    return {{"type", "object"},
            {"properties",
             {{"file_path",
               {{"type", "string"}, {"description", "The absolute path to the file to modify"}}},
              {"old_string", {{"type", "string"}, {"description", "The text to replace"}}},
              {"new_string", {{"type", "string"}, {"description", "The text to replace it with"}}},
              {"replace_all",
               {{"type", "boolean"}, {"description", "Replace all occurrences (default false)"}}}}},
            {"required", json::array({"file_path", "old_string", "new_string"})}};
  }

  PermissionLevel permission_level() const override { return PermissionLevel::NeedsConfirm; }

  ToolResult execute(const json& params) override;
};
