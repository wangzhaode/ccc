#pragma once

#include "../tool.hpp"
#include "../task_manager.hpp"

class TaskListTool : public Tool {
 public:
  explicit TaskListTool(TaskManager* task_manager) : task_manager_(task_manager) {}

  std::string name() const override { return "TaskList"; }
  std::string description() const override {
    return "List all tasks in the task list. Returns a summary of each task including id, subject, "
           "status, owner, and blockers.";
  }

  json schema() const override {
    return {{"type", "object"}, {"properties", json::object()}, {"required", json::array()}};
  }

  PermissionLevel permission_level() const override { return PermissionLevel::AutoAllow; }

  ToolResult execute(const json& /*params*/) override {
    auto tasks = task_manager_->list();
    if (tasks.empty()) {
      return {"No tasks found.", false};
    }

    json result = json::array();
    for (const auto& task : tasks) {
      result.push_back(task.to_summary_json());
    }
    return {result.dump(2), false};
  }

 private:
  TaskManager* task_manager_;
};
