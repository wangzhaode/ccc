#pragma once
#include <string>

class Agent;

namespace ui {

void init();
void cleanup();

void run_tui(Agent& agent);

void print_user_input(const std::string& input);
void print_tool_call(const std::string& name, const std::string& detail);
void print_tool_error(const std::string& message);
void print_api_error(const std::string& message);
void print_warning(const std::string& message);
void print_debug(const std::string& label, const std::string& content);

void start_spinner(const std::string& message = "Thinking...");
void stop_spinner();

// Agent streaming output
void append_text(const std::string& text);
void end_assistant_response();
void print_assistant_stats(const std::string& stats);

// Permission
void print_permission_header(const std::string& tool_name, const std::string& detail);
void print_permission_prompt(const std::string& tool_name);
char prompt_permission(const std::string& tool_name, const std::string& detail);

} // namespace ui
