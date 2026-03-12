#include <gtest/gtest.h>
#include "context_manager.hpp"

class ContextManagerTest : public ::testing::Test {
protected:
    ContextManager cm;
};

TEST_F(ContextManagerTest, EstimateStringTokensASCII) {
    // 4 ASCII chars ≈ 1 token
    EXPECT_EQ(ContextManager::estimate_string_tokens(""), 0);
    EXPECT_EQ(ContextManager::estimate_string_tokens("a"), 1);
    EXPECT_EQ(ContextManager::estimate_string_tokens("abcd"), 1);
    EXPECT_EQ(ContextManager::estimate_string_tokens("abcde"), 2);
    EXPECT_EQ(ContextManager::estimate_string_tokens("hello world!"), 3);  // 12 chars → 3 tokens
}

TEST_F(ContextManagerTest, EstimateStringTokensCJK) {
    // CJK characters: ~1.5 tokens each (rounded to 2)
    std::string chinese = "你好";  // 2 CJK chars, each 3 bytes
    int tokens = ContextManager::estimate_string_tokens(chinese);
    EXPECT_GE(tokens, 2);  // at least 2 tokens for 2 CJK chars
}

TEST_F(ContextManagerTest, EstimateJsonTokens) {
    // Simple string
    json str = "hello";
    EXPECT_GT(ContextManager::estimate_json_tokens(str), 0);

    // Number
    json num = 42;
    EXPECT_EQ(ContextManager::estimate_json_tokens(num), 1);

    // Boolean
    json boolean = true;
    EXPECT_EQ(ContextManager::estimate_json_tokens(boolean), 1);

    // Array
    json arr = json::array({1, 2, 3});
    EXPECT_GT(ContextManager::estimate_json_tokens(arr), 3);  // 3 elements + brackets

    // Object
    json obj = {{"key", "value"}};
    EXPECT_GT(ContextManager::estimate_json_tokens(obj), 2);
}

TEST_F(ContextManagerTest, EstimateJsonTokensNested) {
    json nested = {
        {"messages", json::array({
            {{"role", "user"}, {"content", "hello world"}},
            {{"role", "assistant"}, {"content", "hi there"}}
        })}
    };
    int tokens = ContextManager::estimate_json_tokens(nested);
    EXPECT_GT(tokens, 10);
}

TEST_F(ContextManagerTest, MaybeCompressDoesNothingWhenBelowThreshold) {
    // With minimal messages, should not trigger compression
    json messages = json::array({
        {{"role", "user"}, {"content", "hello"}},
        {{"role", "assistant"}, {"content", "hi"}}
    });
    json system_prompt = json::array({
        {{"type", "text"}, {"text", "you are helpful"}}
    });
    json tools = json::array();

    // Create a mock-like api client (won't actually be called since we're under threshold)
    ApiClient api;

    // This should return false (no compression needed)
    // Note: we can't easily test the actual API call in unit tests
    // but we can verify the threshold logic
    bool compressed = cm.maybe_compress(messages, system_prompt, tools, api);
    EXPECT_FALSE(compressed);
    EXPECT_EQ(messages.size(), 2u);  // unchanged
}
