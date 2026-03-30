#include "skill_manager.hpp"


SkillManager::SkillManager() {
    register_builtin_skills();
}

void SkillManager::register_builtin_skills() {
    skills_["help"] = {
        "help",
        "Show available commands",
        "",
        true  // local
    };

    skills_["clear"] = {
        "clear",
        "Clear conversation history",
        "",
        true  // local
    };

    skills_["compact"] = {
        "compact",
        "Force compress conversation context",
        "",
        true  // local
    };

    skills_["yolo"] = {
        "yolo",
        "Toggle auto-accept all tool permissions",
        "",
        true  // local
    };

    skills_["commit"] = {
        "commit",
        "Analyze changes and generate a git commit",
        R"(Analyze the current git changes and create a commit. Follow these steps:
1. Run `git status` and `git diff` to see all changes
2. Run `git log --oneline -5` to see recent commit style
3. Draft a concise commit message that follows the repository's style
4. Stage the relevant files and create the commit

User's additional instructions: {args})",
        false  // LLM skill
    };
}

bool SkillManager::is_skill_command(const std::string& input) const {
    if (input.empty() || input[0] != '/') return false;
    // Must have at least one letter after /
    return input.size() > 1 && std::isalpha(input[1]);
}

std::pair<std::string, std::string> SkillManager::parse(const std::string& input) const {
    if (!is_skill_command(input)) return {"", ""};

    // Skip the /
    size_t start = 1;
    size_t end = input.find(' ', start);

    std::string command;
    std::string args;

    if (end == std::string::npos) {
        command = input.substr(start);
    } else {
        command = input.substr(start, end - start);
        // Trim leading spaces from args
        size_t args_start = input.find_first_not_of(' ', end);
        if (args_start != std::string::npos) {
            args = input.substr(args_start);
        }
    }

    return {command, args};
}

const Skill* SkillManager::get(const std::string& name) const {
    auto it = skills_.find(name);
    return it != skills_.end() ? &it->second : nullptr;
}

std::string SkillManager::build_prompt(const std::string& name, const std::string& args) const {
    auto* skill = get(name);
    if (!skill || skill->is_local) return "";

    std::string prompt = skill->prompt_template;
    // Replace {args} placeholder
    size_t pos = prompt.find("{args}");
    if (pos != std::string::npos) {
        prompt.replace(pos, 6, args.empty() ? "(none)" : args);
    }
    return prompt;
}

std::string SkillManager::help_text() const {
    std::string result = "Available commands:\n\n";
    for (const auto& [name, skill] : skills_) {
        result += "  /" + name + " - " + skill.description + "\n";
    }
    return result;
}
