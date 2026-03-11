#pragma once

#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// API provider type
enum class ApiProvider {
    Anthropic,
    OpenAI
};

// Streaming callback types
using StreamTextCallback = std::function<void(const std::string& text)>;

// API response structure
struct ApiResponse {
    json message;           // The full assistant message (with content blocks)
    std::string stop_reason; // "end_turn", "tool_use", etc.
    int input_tokens = 0;
    int output_tokens = 0;
};

class ApiClient {
public:
    ApiClient();

    // Send messages and get streaming response
    // system_prompt: json array of text blocks (Anthropic format) or plain string
    ApiResponse chat(
        const json& system_prompt,
        const json& messages,
        const json& tools,
        StreamTextCallback on_text = nullptr
    );

    // Getters
    ApiProvider provider() const { return provider_; }
    std::string model() const { return model_; }

private:
    ApiProvider provider_;
    std::string api_key_;
    std::string host_;
    std::string path_prefix_;
    std::string model_;
    bool debug_ = false;
    int max_tokens_ = 16384;

    ApiResponse chat_anthropic(
        const json& system_prompt,
        const json& messages,
        const json& tools,
        StreamTextCallback on_text
    );

    ApiResponse chat_openai(
        const json& system_prompt,
        const json& messages,
        const json& tools,
        StreamTextCallback on_text
    );

    json tools_to_openai(const json& tools);
    json messages_to_openai(const json& messages);

    // Flatten system prompt array to string (for OpenAI)
    std::string system_to_string(const json& system_prompt);
};
