#include "context_manager.hpp"
#include "ui.hpp"
#include <algorithm>

ContextManager::ContextManager()
    : max_context_tokens_(200000),
      threshold_(0.8f),
      keep_recent_turns_(4)  // 4 pairs of user/assistant messages
{}

int ContextManager::estimate_string_tokens(const std::string& text) {
  if (text.empty())
    return 0;

  int tokens = 0;
  int ascii_chars = 0;

  for (size_t i = 0; i < text.size();) {
    unsigned char c = text[i];
    if (c < 0x80) {
      // ASCII character
      ascii_chars++;
      i++;
    } else if (c < 0xC0) {
      // Continuation byte (skip)
      i++;
    } else if (c < 0xE0) {
      // 2-byte UTF-8
      tokens += 2;  // ~1.5 tokens, round up
      i += 2;
    } else if (c < 0xF0) {
      // 3-byte UTF-8 (CJK range)
      tokens += 2;  // ~1.5 tokens for CJK
      i += 3;
    } else {
      // 4-byte UTF-8
      tokens += 2;
      i += 4;
    }
  }

  // ASCII: ~4 characters per token
  tokens += (ascii_chars + 3) / 4;
  return std::max(tokens, 1);
}

int ContextManager::estimate_json_tokens(const json& j) {
  if (j.is_string()) {
    return estimate_string_tokens(j.get<std::string>());
  } else if (j.is_number()) {
    return 1;
  } else if (j.is_boolean() || j.is_null()) {
    return 1;
  } else if (j.is_array()) {
    int total = 2;  // brackets
    for (const auto& elem : j) {
      total += estimate_json_tokens(elem);
    }
    return total;
  } else if (j.is_object()) {
    int total = 2;  // braces
    for (auto& [key, val] : j.items()) {
      total += estimate_string_tokens(key);
      total += estimate_json_tokens(val);
    }
    return total;
  }
  return 0;
}

int ContextManager::estimate_total_tokens(const json& system_prompt, const json& messages,
                                          const json& tools) const {
  int total = 0;
  total += estimate_json_tokens(system_prompt);
  total += estimate_json_tokens(messages);
  total += estimate_json_tokens(tools);
  return total;
}

bool ContextManager::maybe_compress(json& messages, const json& system_prompt, const json& tools,
                                    ApiClient& api) {
  int total = estimate_total_tokens(system_prompt, messages, tools);
  int threshold_tokens = static_cast<int>(max_context_tokens_ * threshold_);

  if (total < threshold_tokens) {
    return false;
  }

  ui::print_warning("Context approaching limit (" + std::to_string(total) +
                    " est. tokens). Compressing...");
  return compress(messages, api);
}

bool ContextManager::force_compress(json& messages, const json& system_prompt, const json& tools,
                                    ApiClient& api) {
  ui::print_warning("Force compressing context...");
  return compress(messages, api);
}

bool ContextManager::compress(json& messages, ApiClient& api) {
  if (messages.size() <= static_cast<size_t>(keep_recent_turns_ * 2)) {
    // Not enough messages to compress
    return false;
  }

  // Split messages: old messages to summarize, recent to keep
  size_t keep_count = keep_recent_turns_ * 2;  // user + assistant pairs
  size_t old_count = messages.size() - keep_count;

  json old_messages = json::array();
  for (size_t i = 0; i < old_count; i++) {
    old_messages.push_back(messages[i]);
  }

  // Build summarization request
  json summary_system = json::array();
  summary_system.push_back(
      {{"type", "text"},
       {"text",
        "You are a conversation summarizer. Summarize the following conversation history "
        "concisely, "
        "preserving key decisions, code changes, file paths, and important context. "
        "Be brief but include all actionable information. Output only the summary."}});

  json summary_messages = json::array();
  // Convert old messages to a text block for summarization
  std::string conversation_text;
  for (const auto& msg : old_messages) {
    std::string role = msg.value("role", "unknown");
    conversation_text += "[" + role + "]: ";
    if (msg.contains("content")) {
      if (msg["content"].is_string()) {
        conversation_text += msg["content"].get<std::string>();
      } else if (msg["content"].is_array()) {
        for (const auto& block : msg["content"]) {
          if (block.contains("text")) {
            conversation_text += block["text"].get<std::string>();
          } else if (block.contains("content")) {
            conversation_text += block["content"].get<std::string>();
          }
          conversation_text += " ";
        }
      }
    }
    conversation_text += "\n\n";
  }

  summary_messages.push_back({{"role", "user"}, {"content", conversation_text}});

  try {
    ApiResponse summary_response =
        api.chat(summary_system, summary_messages, json::array(), nullptr);

    // Extract summary text
    std::string summary_text;
    if (summary_response.message.contains("content") &&
        summary_response.message["content"].is_array()) {
      for (const auto& block : summary_response.message["content"]) {
        if (block.value("type", "") == "text") {
          summary_text += block["text"].get<std::string>();
        }
      }
    }

    if (summary_text.empty()) {
      return false;
    }

    // Build new messages array: summary + recent messages
    json new_messages = json::array();

    // Add summary as first user message
    new_messages.push_back(
        {{"role", "user"},
         {"content",
          json::array({{{"type", "text"},
                        {"text", "<system-reminder>\n# Conversation Summary (compressed)\n" +
                                     summary_text + "\n</system-reminder>\n" +
                                     "Continue from the conversation summary above."}}})}});

    // Add a placeholder assistant acknowledgment
    new_messages.push_back(
        {{"role", "assistant"},
         {"content",
          json::array(
              {{{"type", "text"},
                {"text", "I've reviewed the conversation summary and I'm ready to continue."}}})}});

    // Add recent messages
    for (size_t i = old_count; i < messages.size(); i++) {
      new_messages.push_back(messages[i]);
    }

    messages = new_messages;
    ui::print_warning("Context compressed: " + std::to_string(old_count) + " messages summarized.");
    return true;
  } catch (const std::exception& e) {
    ui::print_warning("Context compression failed: " + std::string(e.what()));
    return false;
  }
}
