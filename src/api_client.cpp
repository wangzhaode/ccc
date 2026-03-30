#include "api_client.hpp"
#include "ui.hpp"
#include <httplib.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <filesystem>

static std::string get_env(const std::string& name, const std::string& default_val = "") {
    const char* val = std::getenv(name.c_str());
    return val ? std::string(val) : default_val;
}

static json load_settings() {
    const char* home = std::getenv("HOME");
    if (!home) return json::object();

    std::string path = std::string(home) + "/.ccc/settings.json";
    if (!std::filesystem::exists(path)) return json::object();

    std::ifstream file(path);
    if (!file.is_open()) return json::object();

    try {
        return json::parse(file);
    } catch (const json::exception&) {
        std::cerr << "Warning: Failed to parse " << path << "\n";
        return json::object();
    }
}

static void split_url(const std::string& url, std::string& host, std::string& path_prefix) {
    auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) {
        host = url;
        path_prefix = "";
        return;
    }
    auto path_start = url.find('/', scheme_end + 3);
    if (path_start == std::string::npos) {
        host = url;
        path_prefix = "";
    } else {
        host = url.substr(0, path_start);
        path_prefix = url.substr(path_start);
        while (!path_prefix.empty() && path_prefix.back() == '/') {
            path_prefix.pop_back();
        }
    }
}

ApiClient::ApiClient() {
    json settings = load_settings();

    std::string provider_str = settings.value("provider",
        get_env("API_PROVIDER", "anthropic"));

    std::string base_url;
    if (provider_str == "openai") {
        provider_ = ApiProvider::OpenAI;
        api_key_ = settings.value("api_key", get_env("OPENAI_API_KEY"));
        base_url = settings.value("base_url", get_env("API_BASE_URL", "https://api.openai.com"));
        model_ = settings.value("model", get_env("MODEL", "gpt-4o"));
    } else {
        provider_ = ApiProvider::Anthropic;
        api_key_ = settings.value("api_key", get_env("ANTHROPIC_API_KEY"));
        base_url = settings.value("base_url", get_env("API_BASE_URL", "https://api.anthropic.com"));
        model_ = settings.value("model", get_env("MODEL", "claude-sonnet-4-20250514"));
    }

    max_tokens_ = settings.value("max_tokens", 16384);
    split_url(base_url, host_, path_prefix_);
    debug_ = !get_env("CC_DEBUG").empty();

    if (api_key_.empty()) {
        std::cerr << "Warning: API key not set. Configure ~/.ccc/settings.json or set environment variables.\n";
    }
}

std::string ApiClient::system_to_string(const json& system_prompt) {
    if (system_prompt.is_string()) return system_prompt.get<std::string>();
    if (system_prompt.is_array()) {
        std::string result;
        for (auto& block : system_prompt) {
            if (block.contains("text")) {
                if (!result.empty()) result += "\n";
                result += block["text"].get<std::string>();
            }
        }
        return result;
    }
    return "";
}

static void debug_print(const std::string& label, const std::string& content) {
    ui::print_debug(label, content);
}

ApiResponse ApiClient::chat(
    const json& system_prompt,
    const json& messages,
    const json& tools,
    StreamTextCallback on_text
) {
    if (debug_) {
        json request;
        request["system"] = system_prompt;
        request["messages"] = messages;
        request["tools"] = tools;
        request["model"] = model_;
        request["max_tokens"] = max_tokens_;
        debug_print("REQUEST", request.dump(2));
    }

    ApiResponse response;
    if (provider_ == ApiProvider::OpenAI) {
        response = chat_openai(system_prompt, messages, tools, on_text);
    } else {
        response = chat_anthropic(system_prompt, messages, tools, on_text);
    }

    if (debug_) {
        json output;
        output["message"] = response.message;
        output["stop_reason"] = response.stop_reason;
        output["input_tokens"] = response.input_tokens;
        output["output_tokens"] = response.output_tokens;
        debug_print("RESPONSE", output.dump(2));
    }

    return response;
}

static void parse_sse_lines(const std::string& body,
    std::function<void(const json&)> on_event)
{
    std::istringstream stream(body);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line.substr(0, 6) != "data: ") continue;
        std::string data_str = line.substr(6);
        if (data_str == "[DONE]") continue;
        try {
            on_event(json::parse(data_str));
        } catch (const json::exception&) {
        }
    }
}

ApiResponse ApiClient::chat_anthropic(
    const json& system_prompt,
    const json& messages,
    const json& tools,
    StreamTextCallback on_text
) {
    json body;
    body["model"] = model_;
    body["max_tokens"] = max_tokens_;
    body["stream"] = true;
    // System prompt: pass as-is (array of text blocks or string)
    body["system"] = system_prompt;
    body["messages"] = messages;
    if (!tools.empty()) {
        body["tools"] = tools;
    }

    httplib::Client cli(host_);
    cli.set_read_timeout(120, 0);
    cli.set_connection_timeout(30, 0);

    httplib::Headers headers = {
        {"x-api-key", api_key_},
        {"anthropic-version", "2023-06-01"}
    };

    std::string endpoint = path_prefix_ + "/v1/messages";
    auto res = cli.Post(endpoint, headers, body.dump(), "application/json");

    if (!res || res->status != 200) {
        std::string error_msg = res ? res->body : "Connection failed";
        int status = res ? res->status : 0;
        throw std::runtime_error("Anthropic API error (HTTP " + std::to_string(status) + "): " + error_msg);
    }

    ApiResponse result;
    json content_blocks = json::array();
    // Track tool JSON per index for interleaved streaming (some APIs send
    // start events for multiple tool_use blocks before sending their stops)
    std::map<int, std::string> tool_json_by_index;
    std::map<int, int> index_to_block;  // SSE index -> content_blocks position

    if (debug_) {
        std::ofstream sse_dump("/tmp/ccc_sse_debug.txt");
        sse_dump << res->body;
        sse_dump.close();
        debug_print("SSE_BODY_LENGTH", std::to_string(res->body.size()));
    }

    parse_sse_lines(res->body, [&](const json& event) {
        std::string type = event.value("type", "");
        int idx = event.value("index", -1);

        if (type == "message_start") {
            if (event.contains("message") && event["message"].contains("usage")) {
                result.input_tokens = event["message"]["usage"].value("input_tokens", 0);
            }
        } else if (type == "content_block_start") {
            json block = event["content_block"];
            std::string block_type = block.value("type", "");
            if (block_type == "text") {
                index_to_block[idx] = (int)content_blocks.size();
                content_blocks.push_back({{"type", "text"}, {"text", ""}});
            } else if (block_type == "tool_use") {
                index_to_block[idx] = (int)content_blocks.size();
                content_blocks.push_back({
                    {"type", "tool_use"},
                    {"id", block.value("id", "")},
                    {"name", block.value("name", "")},
                    {"input", block.value("input", json::object())}
                });
                tool_json_by_index[idx] = "";
            }
        } else if (type == "content_block_delta") {
            json delta = event["delta"];
            std::string delta_type = delta.value("type", "");
            if (delta_type == "text_delta") {
                std::string text = delta.value("text", "");
                auto it = index_to_block.find(idx);
                if (it != index_to_block.end() && it->second < (int)content_blocks.size()) {
                    content_blocks[it->second]["text"] = content_blocks[it->second]["text"].get<std::string>() + text;
                }
                if (on_text) on_text(text);
            } else if (delta_type == "input_json_delta") {
                tool_json_by_index[idx] += delta.value("partial_json", "");
            }
        } else if (type == "content_block_stop") {
            auto it = index_to_block.find(idx);
            if (it != index_to_block.end() && it->second < (int)content_blocks.size()) {
                auto& block = content_blocks[it->second];
                if (block["type"] == "tool_use") {
                    auto& tool_json = tool_json_by_index[idx];
                    try {
                        if (!tool_json.empty()) {
                            block["input"] = json::parse(tool_json);
                        }
                    } catch (...) {
                        block["input"] = json::object();
                    }
                }
            }
        } else if (type == "message_delta") {
            if (event.contains("delta")) {
                result.stop_reason = event["delta"].value("stop_reason", "");
            }
            if (event.contains("usage")) {
                result.output_tokens = event["usage"].value("output_tokens", 0);
            }
        }
    });

    result.message = {
        {"role", "assistant"},
        {"content", content_blocks}
    };

    return result;
}

// --- OpenAI format conversion helpers ---

static json convert_tool_result_to_openai(const json& msg) {
    json results = json::array();
    for (auto& block : msg["content"]) {
        if (block["type"] == "tool_result") {
            json tool_msg;
            tool_msg["role"] = "tool";
            tool_msg["tool_call_id"] = block.value("tool_use_id", "");
            if (block.contains("content")) {
                if (block["content"].is_string()) {
                    tool_msg["content"] = block["content"];
                } else if (block["content"].is_array()) {
                    std::string text;
                    for (auto& c : block["content"]) {
                        if (c["type"] == "text") text += c["text"].get<std::string>();
                    }
                    tool_msg["content"] = text;
                }
            } else {
                tool_msg["content"] = "";
            }
            results.push_back(tool_msg);
        }
    }
    return results;
}

static json convert_assistant_to_openai(const json& msg) {
    json oai_msg;
    oai_msg["role"] = "assistant";

    std::string text_content;
    json tool_calls_arr = json::array();

    for (auto& block : msg["content"]) {
        if (block["type"] == "text") {
            text_content += block["text"].get<std::string>();
        } else if (block["type"] == "tool_use") {
            tool_calls_arr.push_back({
                {"id", block["id"]},
                {"type", "function"},
                {"function", {
                    {"name", block["name"]},
                    {"arguments", block["input"].dump()}
                }}
            });
        }
    }

    oai_msg["content"] = text_content.empty() ? json(nullptr) : json(text_content);
    if (!tool_calls_arr.empty()) {
        oai_msg["tool_calls"] = tool_calls_arr;
    }

    return oai_msg;
}

ApiResponse ApiClient::chat_openai(
    const json& system_prompt,
    const json& messages,
    const json& tools,
    StreamTextCallback on_text
) {
    // Flatten system prompt to string for OpenAI
    json oai_messages = messages_to_openai(messages);
    oai_messages.insert(oai_messages.begin(),
        {{"role", "system"}, {"content", system_to_string(system_prompt)}});

    json body;
    body["model"] = model_;
    body["stream"] = true;
    body["messages"] = oai_messages;
    if (!tools.empty()) {
        body["tools"] = tools_to_openai(tools);
    }

    httplib::Client cli(host_);
    cli.set_read_timeout(120, 0);
    cli.set_connection_timeout(30, 0);

    httplib::Headers headers = {
        {"Authorization", "Bearer " + api_key_}
    };

    std::string endpoint = path_prefix_ + "/v1/chat/completions";
    auto res = cli.Post(endpoint, headers, body.dump(), "application/json");

    if (!res || (res->status != 200)) {
        std::string error_msg = res ? res->body : "Connection failed";
        int status = res ? res->status : 0;
        throw std::runtime_error("OpenAI API error (HTTP " + std::to_string(status) + "): " + error_msg);
    }

    ApiResponse result;
    std::string full_text;
    json tool_calls = json::array();
    std::map<int, std::string> tool_call_args;

    parse_sse_lines(res->body, [&](const json& event) {
        if (!event.contains("choices") || event["choices"].empty()) return;

        json delta = event["choices"][0].value("delta", json::object());
        std::string finish = event["choices"][0].value("finish_reason", "");

        if (delta.contains("content") && !delta["content"].is_null()) {
            std::string text = delta["content"].get<std::string>();
            full_text += text;
            if (on_text) on_text(text);
        }

        if (delta.contains("tool_calls")) {
            for (auto& tc : delta["tool_calls"]) {
                int idx = tc.value("index", 0);
                if (tc.contains("id")) {
                    while ((int)tool_calls.size() <= idx) {
                        tool_calls.push_back(json::object());
                    }
                    tool_calls[idx]["id"] = tc["id"];
                    tool_calls[idx]["name"] = tc["function"]["name"];
                    tool_call_args[idx] = "";
                }
                if (tc.contains("function") && tc["function"].contains("arguments")) {
                    tool_call_args[idx] += tc["function"]["arguments"].get<std::string>();
                }
            }
        }

        if (!finish.empty() && finish != "null") {
            result.stop_reason = (finish == "tool_calls") ? "tool_use" : "end_turn";
        }

        if (event.contains("usage")) {
            result.input_tokens = event["usage"].value("prompt_tokens", 0);
            result.output_tokens = event["usage"].value("completion_tokens", 0);
        }
    });

    json content_blocks = json::array();
    if (!full_text.empty()) {
        content_blocks.push_back({{"type", "text"}, {"text", full_text}});
    }
    for (int i = 0; i < (int)tool_calls.size(); i++) {
        json input;
        try {
            input = json::parse(tool_call_args[i]);
        } catch (...) {
            input = json::object();
        }
        content_blocks.push_back({
            {"type", "tool_use"},
            {"id", tool_calls[i].value("id", "")},
            {"name", tool_calls[i].value("name", "")},
            {"input", input}
        });
    }

    result.message = {
        {"role", "assistant"},
        {"content", content_blocks}
    };

    return result;
}

json ApiClient::tools_to_openai(const json& tools) {
    json oai_tools = json::array();
    for (auto& tool : tools) {
        oai_tools.push_back({
            {"type", "function"},
            {"function", {
                {"name", tool.value("name", "")},
                {"description", tool.value("description", "")},
                {"parameters", tool.value("input_schema", json::object())}
            }}
        });
    }
    return oai_tools;
}

json ApiClient::messages_to_openai(const json& messages) {
    json result = json::array();
    for (auto& msg : messages) {
        std::string role = msg.value("role", "");
        if (role == "user") {
            bool has_tool_result = false;
            if (msg["content"].is_array()) {
                for (auto& block : msg["content"]) {
                    if (block.is_object() && block.value("type", "") == "tool_result") {
                        has_tool_result = true;
                        break;
                    }
                }
            }
            if (has_tool_result) {
                auto tool_msgs = convert_tool_result_to_openai(msg);
                for (auto& tm : tool_msgs) result.push_back(tm);
            } else {
                // Flatten content blocks to string
                if (msg["content"].is_string()) {
                    result.push_back({{"role", "user"}, {"content", msg["content"]}});
                } else if (msg["content"].is_array()) {
                    std::string text;
                    for (auto& block : msg["content"]) {
                        if (block.is_object() && block.value("type", "") == "text") {
                            text += block["text"].get<std::string>();
                        }
                    }
                    result.push_back({{"role", "user"}, {"content", text}});
                }
            }
        } else if (role == "assistant") {
            result.push_back(convert_assistant_to_openai(msg));
        }
    }
    return result;
}
