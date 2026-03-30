#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "api_client.hpp"
#include "tool.hpp"
#include "permission.hpp"
#include "memory.hpp"
#include "context_manager.hpp"
#include "task_manager.hpp"
#include "skill_manager.hpp"

using json = nlohmann::json;

class Agent {
public:
    Agent();

    // Process a user message through the agent loop
    void process(const std::string& user_input);

    // Getters for UI display
    std::string model() const { return api_client_.model(); }

    // Get list of skill commands: [(name, description), ...]
    std::vector<std::pair<std::string, std::string>> get_skill_list() const;

    // Auto-accept all tool permissions without prompting
    void set_auto_accept(bool enabled) { permission_manager_.set_auto_accept(enabled); }

private:
    ApiClient api_client_;
    TaskManager task_manager_;        // Must be before tool_registry_
    ToolRegistry tool_registry_;
    PermissionManager permission_manager_;
    MemoryManager memory_manager_;
    ContextManager context_manager_;
    SkillManager skill_manager_;
    json messages_;  // Conversation history

    // Build system prompt as array of text blocks (Anthropic format)
    json build_system_prompt() const;

    // Build user message with system-reminder injections
    json build_user_message(const std::string& user_input) const;

    // Register all built-in tools
    void register_tools();

    // Execute a tool call and return the result
    json execute_tool_call(const json& tool_use_block);

    // Run the agent loop (may loop if there are tool calls)
    void agent_loop();

    // Handle local skill commands. Returns true if handled locally.
    bool handle_local_skill(const std::string& command, const std::string& args);
};
