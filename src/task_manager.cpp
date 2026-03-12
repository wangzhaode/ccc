#include "task_manager.hpp"
#include <algorithm>

json Task::to_json() const {
    json j = {
        {"id", id},
        {"subject", subject},
        {"description", description},
        {"status", status},
    };
    if (!owner.empty()) j["owner"] = owner;
    if (!active_form.empty()) j["activeForm"] = active_form;
    if (!blocks.empty()) j["blocks"] = blocks;
    if (!blocked_by.empty()) j["blockedBy"] = blocked_by;
    return j;
}

json Task::to_summary_json() const {
    json j = {
        {"id", id},
        {"subject", subject},
        {"status", status},
    };
    if (!owner.empty()) j["owner"] = owner;
    // Only show open blockers
    std::vector<std::string> open_blockers;
    for (const auto& b : blocked_by) {
        open_blockers.push_back(b);
    }
    if (!open_blockers.empty()) j["blockedBy"] = open_blockers;
    return j;
}

std::string TaskManager::create(const std::string& subject, const std::string& description,
                                 const std::string& active_form) {
    std::string id = std::to_string(next_id_++);
    Task task;
    task.id = id;
    task.subject = subject;
    task.description = description;
    task.active_form = active_form;
    tasks_[id] = std::move(task);
    return id;
}

bool TaskManager::update(const std::string& id, const json& updates) {
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return false;

    Task& task = it->second;

    if (updates.contains("status")) {
        task.status = updates["status"].get<std::string>();
    }
    if (updates.contains("subject")) {
        task.subject = updates["subject"].get<std::string>();
    }
    if (updates.contains("description")) {
        task.description = updates["description"].get<std::string>();
    }
    if (updates.contains("owner")) {
        task.owner = updates["owner"].get<std::string>();
    }
    if (updates.contains("activeForm")) {
        task.active_form = updates["activeForm"].get<std::string>();
    }
    if (updates.contains("addBlocks")) {
        for (const auto& b : updates["addBlocks"]) {
            std::string bid = b.get<std::string>();
            if (std::find(task.blocks.begin(), task.blocks.end(), bid) == task.blocks.end()) {
                task.blocks.push_back(bid);
            }
            // Also add reverse dependency
            auto target = tasks_.find(bid);
            if (target != tasks_.end()) {
                auto& tby = target->second.blocked_by;
                if (std::find(tby.begin(), tby.end(), id) == tby.end()) {
                    tby.push_back(id);
                }
            }
        }
    }
    if (updates.contains("addBlockedBy")) {
        for (const auto& b : updates["addBlockedBy"]) {
            std::string bid = b.get<std::string>();
            if (std::find(task.blocked_by.begin(), task.blocked_by.end(), bid) == task.blocked_by.end()) {
                task.blocked_by.push_back(bid);
            }
            // Also add reverse dependency
            auto target = tasks_.find(bid);
            if (target != tasks_.end()) {
                auto& tblocks = target->second.blocks;
                if (std::find(tblocks.begin(), tblocks.end(), id) == tblocks.end()) {
                    tblocks.push_back(id);
                }
            }
        }
    }

    return true;
}

const Task* TaskManager::get(const std::string& id) const {
    auto it = tasks_.find(id);
    return it != tasks_.end() ? &it->second : nullptr;
}

std::vector<Task> TaskManager::list() const {
    std::vector<Task> result;
    for (const auto& [id, task] : tasks_) {
        result.push_back(task);
    }
    return result;
}

bool TaskManager::remove(const std::string& id) {
    auto it = tasks_.find(id);
    if (it == tasks_.end()) return false;

    // Clean up dependencies referencing this task
    for (auto& [tid, task] : tasks_) {
        auto& blocks = task.blocks;
        blocks.erase(std::remove(blocks.begin(), blocks.end(), id), blocks.end());
        auto& blocked = task.blocked_by;
        blocked.erase(std::remove(blocked.begin(), blocked.end(), id), blocked.end());
    }

    tasks_.erase(it);
    return true;
}
