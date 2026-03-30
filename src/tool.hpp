#pragma once

#include <string>
#include <memory>
#include <map>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Permission levels for tools
enum class PermissionLevel {
  AutoAllow,    // Read, Glob, Grep - no confirmation needed
  NeedsConfirm  // Write, Edit, Bash - requires user confirmation
};

// Result of tool execution
struct ToolResult {
  std::string content;
  bool is_error = false;
};

// Tool base class
class Tool {
 public:
  virtual ~Tool() = default;
  virtual std::string name() const = 0;
  virtual std::string description() const = 0;
  virtual json schema() const = 0;
  virtual PermissionLevel permission_level() const = 0;
  virtual ToolResult execute(const json& params) = 0;
};

// Tool registry - manages all available tools
class ToolRegistry {
 public:
  void register_tool(std::unique_ptr<Tool> tool) {
    std::string tool_name = tool->name();
    tools_[tool_name] = std::move(tool);
  }

  Tool* get(const std::string& name) const {
    auto it = tools_.find(name);
    return it != tools_.end() ? it->second.get() : nullptr;
  }

  ToolResult execute(const std::string& name, const json& params) {
    auto* tool = get(name);
    if (!tool) {
      return {"Unknown tool: " + name, true};
    }
    return tool->execute(params);
  }

  // Get tool definitions for API (Anthropic format)
  json get_tool_definitions() const {
    json tools = json::array();
    for (auto& [name, tool] : tools_) {
      tools.push_back({{"name", tool->name()},
                       {"description", tool->description()},
                       {"input_schema", tool->schema()}});
    }
    return tools;
  }

  const std::map<std::string, std::unique_ptr<Tool>>& all() const { return tools_; }

 private:
  std::map<std::string, std::unique_ptr<Tool>> tools_;
};
