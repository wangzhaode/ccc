#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "api_client.hpp"

using json = nlohmann::json;

class ContextManager {
public:
    ContextManager();

    // Estimate token count for a string (ASCII: 4chars≈1token, CJK: 1char≈1.5token)
    static int estimate_string_tokens(const std::string& text);

    // Estimate token count for a JSON value (recursive)
    static int estimate_json_tokens(const json& j);

    // Check and compress if over threshold, returns true if compressed
    bool maybe_compress(json& messages, const json& system_prompt,
                        const json& tools, ApiClient& api);

    // Force compress (for /compact command)
    bool force_compress(json& messages, const json& system_prompt,
                        const json& tools, ApiClient& api);

private:
    int max_context_tokens_;
    float threshold_;
    int keep_recent_turns_;  // number of recent user/assistant exchanges to keep

    // Core compression logic
    bool compress(json& messages, ApiClient& api);

    // Count total estimated tokens for system + messages + tools
    int estimate_total_tokens(const json& system_prompt, const json& messages,
                              const json& tools) const;
};
