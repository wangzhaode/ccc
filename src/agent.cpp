#include "agent.hpp"
#include "ui.hpp"
#include "tools/read_tool.hpp"
#include "tools/write_tool.hpp"
#include "tools/edit_tool.hpp"
#include "tools/bash_tool.hpp"
#include "tools/glob_tool.hpp"
#include "tools/grep_tool.hpp"
#include "tools/task_create_tool.hpp"
#include "tools/task_update_tool.hpp"
#include "tools/task_list_tool.hpp"
#include <filesystem>
#include <cstdlib>

static bool params_present(const json& input, const std::string& key) {
    if (!input.contains(key)) return false;
    // Also reject null values
    return !input[key].is_null();
}

Agent::Agent() : messages_(json::array()) {
    register_tools();
}

void Agent::register_tools() {
    tool_registry_.register_tool(std::make_unique<ReadTool>());
    tool_registry_.register_tool(std::make_unique<WriteTool>());
    tool_registry_.register_tool(std::make_unique<EditTool>());
    tool_registry_.register_tool(std::make_unique<BashTool>());
    tool_registry_.register_tool(std::make_unique<GlobTool>());
    tool_registry_.register_tool(std::make_unique<GrepTool>());
    tool_registry_.register_tool(std::make_unique<TaskCreateTool>(&task_manager_));
    tool_registry_.register_tool(std::make_unique<TaskUpdateTool>(&task_manager_));
    tool_registry_.register_tool(std::make_unique<TaskListTool>(&task_manager_));
}

json Agent::build_system_prompt() const {
    std::string system_str = "You are ccc, a CLI-based AI coding assistant.\n";

    system_str += R"(
You are an interactive agent that helps users with software engineering tasks. Use the instructions below and the tools available to you to assist the user.

# System
 - All text you output outside of tool use is displayed to the user. Output text to communicate with the user. You can use Github-flavored markdown for formatting.
 - Tool results and user messages may include <system-reminder> tags. Tags contain information from the system.
 - The system will automatically compress prior messages in your conversation as it approaches context limits.

# Doing tasks
 - The user will primarily request you to perform software engineering tasks. These may include solving bugs, adding new functionality, refactoring code, explaining code, and more.
 - You are highly capable and often allow users to complete ambitious tasks that would otherwise be too complex or take too long.
 - In general, do not propose changes to code you haven't read. If a user asks about or wants you to modify a file, read it first.
 - Do not create files unless they're absolutely necessary. Generally prefer editing an existing file to creating a new one.
 - If your approach is blocked, do not attempt to brute force your way to the outcome. Consider alternative approaches or ask the user.
 - Be careful not to introduce security vulnerabilities. Prioritize writing safe, secure, and correct code.
 - Avoid over-engineering. Only make changes that are directly requested or clearly necessary. Keep solutions simple and focused.
  - Don't add features, refactor code, or make "improvements" beyond what was asked.
  - Don't add error handling or validation for scenarios that can't happen.
  - Don't create helpers or abstractions for one-time operations.

# Executing actions with care
 - Carefully consider the reversibility and blast radius of actions.
 - For actions that are hard to reverse, affect shared systems, or could be destructive, check with the user before proceeding.
 - Examples of risky actions that warrant user confirmation:
  - Destructive operations: deleting files/branches, rm -rf, overwriting uncommitted changes
  - Hard-to-reverse operations: force-pushing, git reset --hard
  - Actions visible to others: pushing code, creating/commenting on PRs or issues

# Using your tools
 - Do NOT use Bash when a dedicated tool is available:
  - To read files use Read instead of cat/head/tail
  - To edit files use Edit instead of sed/awk
  - To create files use Write instead of echo/cat heredoc
  - To search for files use Glob instead of find/ls
  - To search file contents use Grep instead of grep/rg
  - Reserve Bash exclusively for system commands that require shell execution.
 - You can call multiple tools in a single response. Make independent tool calls in parallel.

# Task management
 - Use TaskCreate, TaskUpdate, and TaskList tools to track complex multi-step tasks.
 - Create tasks for complex work requiring 3+ steps. Mark as in_progress when starting, completed when done.

# Tone and style
 - Only use emojis if the user explicitly requests it.
 - Your responses should be short and concise.
 - When referencing code include the pattern file_path:line_number.

# Environment
 - Working directory: )" + std::filesystem::current_path().string() + R"(
 - Platform: )"
#if defined(__APPLE__)
    + "macOS"
#elif defined(__linux__)
    + "Linux"
#else
    + "Unknown"
#endif
    + R"(
 - Shell: )" + std::string(std::getenv("SHELL") ? std::getenv("SHELL") : "unknown") + "\n";

    // Append auto memory
    std::string auto_memory = memory_manager_.build_auto_memory_prompt();
    if (!auto_memory.empty()) {
        system_str += "\n" + auto_memory;
    }

    return system_str;
}

json Agent::build_user_message(const std::string& user_input) const {
    json content = json::array();

    // Inject memory (CCC.md) as system-reminder
    std::string memory = memory_manager_.build_memory_prompt();
    if (!memory.empty()) {
        content.push_back({
            {"type", "text"},
            {"text", "<system-reminder>\n# Project Instructions\n" + memory + "</system-reminder>\n"}
        });
    }

    // User's actual text
    content.push_back({
        {"type", "text"},
        {"text", user_input}
    });

    return {
        {"role", "user"},
        {"content", content}
    };
}

bool Agent::handle_local_skill(const std::string& command, const std::string& args) {
    if (command == "help") {
        ui::append_text(skill_manager_.help_text());
        ui::end_assistant_response();
        return true;
    }
    if (command == "clear") {
        messages_ = json::array();
        ui::append_text("Conversation history cleared.");
        ui::end_assistant_response();
        return true;
    }
    if (command == "compact") {
        json system_prompt = build_system_prompt();
        json tools = tool_registry_.get_tool_definitions();
        bool compressed = context_manager_.force_compress(messages_, system_prompt, tools, api_client_);
        if (compressed) {
            ui::append_text("Context compressed successfully.");
        } else {
            ui::append_text("Not enough context to compress.");
        }
        ui::end_assistant_response();
        return true;
    }
    if (command == "yolo") {
        bool enabled = !permission_manager_.auto_accept();
        permission_manager_.set_auto_accept(enabled);
        ui::append_text(enabled ? "Auto-accept enabled. All tools will run without confirmation."
                                : "Auto-accept disabled. Tools will ask for confirmation.");
        ui::end_assistant_response();
        return true;
    }
    return false;
}

void Agent::process(const std::string& user_input) {
    // Check for skill commands
    if (skill_manager_.is_skill_command(user_input)) {
        auto [command, args] = skill_manager_.parse(user_input);
        auto* skill = skill_manager_.get(command);
        if (!skill) {
            ui::append_text("Unknown command: /" + command + "\n");
            ui::append_text(skill_manager_.help_text());
            ui::end_assistant_response();
            return;
        }
        if (skill->is_local) {
            if (handle_local_skill(command, args)) {
                return;
            }
        } else {
            // LLM skill: build prompt and process as normal message
            std::string prompt = skill_manager_.build_prompt(command, args);
            messages_.push_back(build_user_message(prompt));
            agent_loop();
            return;
        }
    }

    // Normal message processing
    messages_.push_back(build_user_message(user_input));
    agent_loop();
}

std::vector<std::pair<std::string, std::string>> Agent::get_skill_list() const {
    std::vector<std::pair<std::string, std::string>> result;
    for (const auto& [name, skill] : skill_manager_.all()) {
        result.emplace_back(name, skill.description);
    }
    return result;
}

json Agent::execute_tool_call(const json& tool_use_block) {
    std::string tool_name = tool_use_block["name"].get<std::string>();
    std::string tool_id = tool_use_block["id"].get<std::string>();
    json input = tool_use_block.value("input", json::object());

    auto make_error = [&](const std::string& msg) -> json {
        ui::print_tool_call(tool_name, "");
        ui::print_tool_error(msg);
        return {
            {"type", "tool_result"},
            {"tool_use_id", tool_id},
            {"content", msg},
            {"is_error", true}
        };
    };

    // Check tool exists
    auto* tool = tool_registry_.get(tool_name);
    if (!tool) {
        return make_error("Unknown tool: " + tool_name);
    }

    // Validate required parameters before permission check
    json tool_schema = tool->schema();
    if (tool_schema.contains("required") && tool_schema["required"].is_array()) {
        for (auto& req : tool_schema["required"]) {
            std::string key = req.get<std::string>();
            if (!params_present(input, key)) {
                return make_error("Missing required parameter: \"" + key + "\". "
                                  "You must provide the \"" + key + "\" parameter.");
            }
        }
    }

    // Permission check (only after params are valid)
    if (!permission_manager_.check_and_request(tool_name, input, tool->permission_level())) {
        return {
            {"type", "tool_result"},
            {"tool_use_id", tool_id},
            {"content", "Permission denied by user."},
            {"is_error", true}
        };
    }

    // Print tool call info
    std::string detail;
    if (input.contains("file_path")) {
        detail = input["file_path"].get<std::string>();
    } else if (input.contains("command")) {
        detail = input["command"].get<std::string>();
        if (detail.length() > 80) detail = detail.substr(0, 80) + "...";
    } else if (input.contains("pattern")) {
        detail = input["pattern"].get<std::string>();
    } else if (input.contains("subject")) {
        detail = input["subject"].get<std::string>();
    } else if (input.contains("taskId")) {
        detail = "task #" + input["taskId"].get<std::string>();
    }
    ui::print_tool_call(tool_name, detail);

    // Execute with exception handling
    ToolResult result;
    try {
        result = tool->execute(input);
    } catch (const std::exception& e) {
        result = {e.what(), true};
    }

    if (result.is_error) {
        ui::print_tool_error(result.content);
    }

    return {
        {"type", "tool_result"},
        {"tool_use_id", tool_id},
        {"content", result.content},
        {"is_error", result.is_error}
    };
}

void Agent::agent_loop() {
    const int max_iterations = 50;

    for (int iter = 0; iter < max_iterations; iter++) {
        json system_prompt = build_system_prompt();
        json tools = tool_registry_.get_tool_definitions();

        // Check and compress context if needed
        context_manager_.maybe_compress(messages_, system_prompt, tools, api_client_);

        ui::start_spinner("Thinking...");

        auto start_time = std::chrono::steady_clock::now();
        std::chrono::duration<double> thought_duration{0};

        ApiResponse response;
        bool first_token = true;
        try {
            response = api_client_.chat(system_prompt, messages_, tools,
                [&first_token, &thought_duration, start_time](const std::string& text) {
                    if (first_token) {
                        ui::stop_spinner();
                        first_token = false;
                        thought_duration = std::chrono::steady_clock::now() - start_time;
                    }
                    ui::append_text(text);
                }
            );
        } catch (const std::exception& e) {
            ui::stop_spinner();
            ui::print_api_error(e.what());
            return;
        }

        ui::stop_spinner();
        ui::end_assistant_response();
        auto end_time = std::chrono::steady_clock::now();
        std::chrono::duration<double> total_duration = end_time - start_time;
        if (first_token) {
            thought_duration = total_duration;
        }

        char stats_buf[128];
        snprintf(stats_buf, sizeof(stats_buf), "(%.1fs \xc2\xb7 \xe2\x86\x91 %d tokens \xc2\xb7 \xe2\x86\x93 %d tokens \xc2\xb7 thought for %.1fs)",
                 total_duration.count(), response.input_tokens, response.output_tokens, thought_duration.count());
        ui::print_assistant_stats(stats_buf);

        // Add assistant message to history
        messages_.push_back(response.message);

        // Check if there are tool calls
        bool has_tool_use = false;
        json tool_results = json::array();

        for (auto& block : response.message["content"]) {
            if (block["type"] == "tool_use") {
                has_tool_use = true;
                tool_results.push_back(execute_tool_call(block));
            }
        }

        if (!has_tool_use) {
            return;
        }

        // Add tool results to history and continue loop
        messages_.push_back({
            {"role", "user"},
            {"content", tool_results}
        });
    }

    ui::print_warning("Agent loop reached maximum iterations.");
}
