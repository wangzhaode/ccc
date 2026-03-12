#pragma once

#include <string>
#include <map>


struct Skill {
    std::string name;
    std::string description;
    std::string prompt_template;  // For LLM skills, the prompt to inject
    bool is_local;                // true = handled locally, false = sent to LLM
};

class SkillManager {
public:
    SkillManager();

    // Check if input starts with /
    bool is_skill_command(const std::string& input) const;

    // Parse input into (command_name, args). Returns ("", "") if not a skill command.
    std::pair<std::string, std::string> parse(const std::string& input) const;

    // Get a skill by name. Returns nullptr if not found.
    const Skill* get(const std::string& name) const;

    // Build the full prompt for an LLM skill (substitutes args into template)
    std::string build_prompt(const std::string& name, const std::string& args) const;

    // Get all registered skills
    const std::map<std::string, Skill>& all() const { return skills_; }

    // Get help text listing all commands
    std::string help_text() const;

private:
    std::map<std::string, Skill> skills_;
    void register_builtin_skills();
};
