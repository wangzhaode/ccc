#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "api_client.hpp"
#include "tool.hpp"
#include "permission.hpp"
#include "memory.hpp"

using json = nlohmann::json;

class Agent {
public:
    Agent();

    // Process a user message through the agent loop
    void process(const std::string& user_input);

private:
    ApiClient api_client_;
    ToolRegistry tool_registry_;
    PermissionManager permission_manager_;
    MemoryManager memory_manager_;
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
};
