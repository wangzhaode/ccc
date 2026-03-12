#include <gtest/gtest.h>
#include "skill_manager.hpp"

class SkillManagerTest : public ::testing::Test {
protected:
    SkillManager sm;
};

TEST_F(SkillManagerTest, IsSkillCommand) {
    EXPECT_TRUE(sm.is_skill_command("/help"));
    EXPECT_TRUE(sm.is_skill_command("/commit"));
    EXPECT_TRUE(sm.is_skill_command("/clear"));
    EXPECT_TRUE(sm.is_skill_command("/commit fix the bug"));
    EXPECT_FALSE(sm.is_skill_command(""));
    EXPECT_FALSE(sm.is_skill_command("hello"));
    EXPECT_FALSE(sm.is_skill_command("/"));
    EXPECT_FALSE(sm.is_skill_command("/123"));  // not alpha after /
    EXPECT_FALSE(sm.is_skill_command("/ help"));  // space after /
}

TEST_F(SkillManagerTest, ParseCommandOnly) {
    auto [cmd, args] = sm.parse("/help");
    EXPECT_EQ(cmd, "help");
    EXPECT_EQ(args, "");
}

TEST_F(SkillManagerTest, ParseCommandWithArgs) {
    auto [cmd, args] = sm.parse("/commit fix the login bug");
    EXPECT_EQ(cmd, "commit");
    EXPECT_EQ(args, "fix the login bug");
}

TEST_F(SkillManagerTest, ParseNonCommand) {
    auto [cmd, args] = sm.parse("hello");
    EXPECT_EQ(cmd, "");
    EXPECT_EQ(args, "");
}

TEST_F(SkillManagerTest, GetBuiltinSkills) {
    EXPECT_NE(sm.get("help"), nullptr);
    EXPECT_NE(sm.get("clear"), nullptr);
    EXPECT_NE(sm.get("compact"), nullptr);
    EXPECT_NE(sm.get("commit"), nullptr);
    EXPECT_EQ(sm.get("nonexistent"), nullptr);
}

TEST_F(SkillManagerTest, LocalSkillsAreLocal) {
    EXPECT_TRUE(sm.get("help")->is_local);
    EXPECT_TRUE(sm.get("clear")->is_local);
    EXPECT_TRUE(sm.get("compact")->is_local);
}

TEST_F(SkillManagerTest, LLMSkillsAreNotLocal) {
    EXPECT_FALSE(sm.get("commit")->is_local);
}

TEST_F(SkillManagerTest, BuildPromptForLLMSkill) {
    std::string prompt = sm.build_prompt("commit", "fix the bug");
    EXPECT_FALSE(prompt.empty());
    EXPECT_NE(prompt.find("fix the bug"), std::string::npos);
}

TEST_F(SkillManagerTest, BuildPromptForLocalSkill) {
    std::string prompt = sm.build_prompt("help", "");
    EXPECT_TRUE(prompt.empty());  // local skills don't have LLM prompts
}

TEST_F(SkillManagerTest, BuildPromptNoArgs) {
    std::string prompt = sm.build_prompt("commit", "");
    EXPECT_FALSE(prompt.empty());
    EXPECT_NE(prompt.find("(none)"), std::string::npos);
}

TEST_F(SkillManagerTest, HelpText) {
    std::string help = sm.help_text();
    EXPECT_NE(help.find("/help"), std::string::npos);
    EXPECT_NE(help.find("/clear"), std::string::npos);
    EXPECT_NE(help.find("/compact"), std::string::npos);
    EXPECT_NE(help.find("/commit"), std::string::npos);
}
