#pragma once

#include "../tool.hpp"

class GrepTool : public Tool {
public:
    std::string name() const override { return "Grep"; }
    std::string description() const override {
        return "Search for patterns in file contents using regex. Returns matching file paths or content lines.";
    }

    json schema() const override {
        return {
            {"type", "object"},
            {"properties", {
                {"pattern", {{"type", "string"}, {"description", "The regex pattern to search for"}}},
                {"path", {{"type", "string"}, {"description", "File or directory to search in (default: current directory)"}}},
                {"glob", {{"type", "string"}, {"description", "Glob pattern to filter files (e.g. \"*.cpp\")"}}},
                {"output_mode", {{"type", "string"}, {"description", "\"files_with_matches\" (default) or \"content\""}}}
            }},
            {"required", json::array({"pattern"})}
        };
    }

    PermissionLevel permission_level() const override { return PermissionLevel::AutoAllow; }

    ToolResult execute(const json& params) override;
};
