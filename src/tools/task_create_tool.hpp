#pragma once

#include "../tool.hpp"
#include "../task_manager.hpp"

class TaskCreateTool : public Tool {
 public:
  explicit TaskCreateTool(TaskManager* task_manager) : task_manager_(task_manager) {}

  std::string name() const override { return "TaskCreate"; }
  std::string description() const override {
    return "Create a new task to track progress on a coding session. "
           "Use for complex multi-step tasks or when the user provides multiple tasks.";
  }

  json schema() const override {
    return {{"type", "object"},
            {"properties",
             {{"subject", {{"type", "string"}, {"description", "A brief title for the task"}}},
              {"description",
               {{"type", "string"},
                {"description", "A detailed description of what needs to be done"}}},
              {"activeForm",
               {{"type", "string"},
                {"description", "Present continuous form shown in spinner when in_progress"}}}}},
            {"required", json::array({"subject", "description"})}};
  }

  PermissionLevel permission_level() const override { return PermissionLevel::AutoAllow; }

  ToolResult execute(const json& params) override {
    std::string subject = params["subject"].get<std::string>();
    std::string description = params["description"].get<std::string>();
    std::string active_form = params.value("activeForm", "");

    std::string id = task_manager_->create(subject, description, active_form);
    return {"Task #" + id + " created successfully: " + subject, false};
  }

 private:
  TaskManager* task_manager_;
};
