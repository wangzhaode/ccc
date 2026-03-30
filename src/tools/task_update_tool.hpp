#pragma once

#include "../tool.hpp"
#include "../task_manager.hpp"

class TaskUpdateTool : public Tool {
 public:
  explicit TaskUpdateTool(TaskManager* task_manager) : task_manager_(task_manager) {}

  std::string name() const override { return "TaskUpdate"; }
  std::string description() const override {
    return "Update a task's status, subject, description, owner, or dependencies. "
           "Use to mark tasks as in_progress when starting, completed when done, or deleted to "
           "remove.";
  }

  json schema() const override {
    return {
        {"type", "object"},
        {"properties",
         {{"taskId", {{"type", "string"}, {"description", "The ID of the task to update"}}},
          {"status",
           {{"type", "string"},
            {"description", "New status: pending, in_progress, completed, or deleted"}}},
          {"subject", {{"type", "string"}, {"description", "New subject for the task"}}},
          {"description", {{"type", "string"}, {"description", "New description for the task"}}},
          {"owner", {{"type", "string"}, {"description", "New owner for the task"}}},
          {"activeForm",
           {{"type", "string"}, {"description", "Present continuous form for spinner"}}},
          {"addBlocks",
           {{"type", "array"},
            {"items", {{"type", "string"}}},
            {"description", "Task IDs that this task blocks"}}},
          {"addBlockedBy",
           {{"type", "array"},
            {"items", {{"type", "string"}}},
            {"description", "Task IDs that block this task"}}}}},
        {"required", json::array({"taskId"})}};
  }

  PermissionLevel permission_level() const override { return PermissionLevel::AutoAllow; }

  ToolResult execute(const json& params) override {
    std::string task_id = params["taskId"].get<std::string>();

    // Handle deletion
    if (params.contains("status") && params["status"].get<std::string>() == "deleted") {
      if (task_manager_->remove(task_id)) {
        return {"Task #" + task_id + " deleted.", false};
      }
      return {"Task #" + task_id + " not found.", true};
    }

    if (!task_manager_->update(task_id, params)) {
      return {"Task #" + task_id + " not found.", true};
    }

    // Build response message
    std::string msg = "Updated task #" + task_id;
    if (params.contains("status")) {
      msg += " status";
    }
    return {msg, false};
  }

 private:
  TaskManager* task_manager_;
};
