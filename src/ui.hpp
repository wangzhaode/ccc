#pragma once
#include <string>

namespace ui {

// Render an FTXUI Element to the terminal (Element -> string -> stdout)
void render_element(/* ftxui::Element - forward declared usage in .cpp */);

// High-level UI functions
void print_banner();
void print_prompt();
void print_tool_call(const std::string& name, const std::string& detail);
void print_tool_error(const std::string& message);
void print_api_error(const std::string& message);
void print_warning(const std::string& message);
void print_debug(const std::string& label, const std::string& content);

// Permission prompt
void print_permission_header(const std::string& tool_name, const std::string& detail);
void print_permission_prompt(const std::string& tool_name);

// Spinner for API waiting
void start_spinner(const std::string& message = "Thinking...");
void stop_spinner();

} // namespace ui
