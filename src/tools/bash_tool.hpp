#pragma once

#include "../tool.hpp"

class BashTool : public Tool {
public:
    std::string name() const override { return "Bash"; }
    std::string description() const override {
        return "Executes a bash command and returns its output (stdout + stderr).";
    }

    json schema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"command", {{"type", "string"}, {"description", "The bash command to execute"}}},
                {"timeout", {{"type", "number"}, {"description", "Timeout in milliseconds (default 120000)"}}}
            }},
            {"required", json::array({"command"})}
        };
    }

    PermissionLevel permission_level() const override { return PermissionLevel::NeedsConfirm; }

    ToolResult execute(const json& params) override;
};
