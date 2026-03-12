#pragma once

#include <string>
#include <map>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct Task {
    std::string id;
    std::string subject;
    std::string description;
    std::string status = "pending";  // pending, in_progress, completed
    std::string owner;
    std::string active_form;
    std::vector<std::string> blocks;      // task IDs this task blocks
    std::vector<std::string> blocked_by;  // task IDs blocking this task

    json to_json() const;
    json to_summary_json() const;  // compact version for list
};

class TaskManager {
public:
    // Create a new task, returns the task ID
    std::string create(const std::string& subject, const std::string& description,
                       const std::string& active_form = "");

    // Update a task by ID. Returns false if task not found.
    bool update(const std::string& id, const json& updates);

    // Get a task by ID. Returns nullptr if not found.
    const Task* get(const std::string& id) const;

    // List all non-deleted tasks
    std::vector<Task> list() const;

    // Delete a task by ID
    bool remove(const std::string& id);

private:
    std::map<std::string, Task> tasks_;
    int next_id_ = 1;
};
