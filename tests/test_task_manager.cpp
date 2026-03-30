#include <gtest/gtest.h>
#include "task_manager.hpp"

class TaskManagerTest : public ::testing::Test {
 protected:
  TaskManager tm;
};

TEST_F(TaskManagerTest, CreateTask) {
  std::string id = tm.create("Test task", "Test description");
  EXPECT_EQ(id, "1");

  const Task* task = tm.get(id);
  ASSERT_NE(task, nullptr);
  EXPECT_EQ(task->subject, "Test task");
  EXPECT_EQ(task->description, "Test description");
  EXPECT_EQ(task->status, "pending");
}

TEST_F(TaskManagerTest, CreateMultipleTasks) {
  std::string id1 = tm.create("Task 1", "Desc 1");
  std::string id2 = tm.create("Task 2", "Desc 2");
  EXPECT_EQ(id1, "1");
  EXPECT_EQ(id2, "2");
}

TEST_F(TaskManagerTest, UpdateStatus) {
  std::string id = tm.create("Task", "Desc");
  json updates = {{"status", "in_progress"}};
  EXPECT_TRUE(tm.update(id, updates));

  const Task* task = tm.get(id);
  EXPECT_EQ(task->status, "in_progress");

  updates = {{"status", "completed"}};
  EXPECT_TRUE(tm.update(id, updates));
  task = tm.get(id);
  EXPECT_EQ(task->status, "completed");
}

TEST_F(TaskManagerTest, UpdateNonExistent) {
  json updates = {{"status", "in_progress"}};
  EXPECT_FALSE(tm.update("999", updates));
}

TEST_F(TaskManagerTest, UpdateSubjectAndDescription) {
  std::string id = tm.create("Old subject", "Old description");
  json updates = {{"subject", "New subject"}, {"description", "New description"}};
  EXPECT_TRUE(tm.update(id, updates));

  const Task* task = tm.get(id);
  EXPECT_EQ(task->subject, "New subject");
  EXPECT_EQ(task->description, "New description");
}

TEST_F(TaskManagerTest, UpdateOwner) {
  std::string id = tm.create("Task", "Desc");
  json updates = {{"owner", "agent-1"}};
  EXPECT_TRUE(tm.update(id, updates));

  const Task* task = tm.get(id);
  EXPECT_EQ(task->owner, "agent-1");
}

TEST_F(TaskManagerTest, AddDependencies) {
  std::string id1 = tm.create("Task 1", "Desc 1");
  std::string id2 = tm.create("Task 2", "Desc 2");

  // Task 2 is blocked by Task 1
  json updates = {{"addBlockedBy", json::array({"1"})}};
  EXPECT_TRUE(tm.update(id2, updates));

  const Task* task2 = tm.get(id2);
  EXPECT_EQ(task2->blocked_by.size(), 1u);
  EXPECT_EQ(task2->blocked_by[0], "1");

  // Reverse dependency should be set
  const Task* task1 = tm.get(id1);
  EXPECT_EQ(task1->blocks.size(), 1u);
  EXPECT_EQ(task1->blocks[0], "2");
}

TEST_F(TaskManagerTest, AddBlocks) {
  std::string id1 = tm.create("Task 1", "Desc 1");
  std::string id2 = tm.create("Task 2", "Desc 2");

  // Task 1 blocks Task 2
  json updates = {{"addBlocks", json::array({"2"})}};
  EXPECT_TRUE(tm.update(id1, updates));

  const Task* task1 = tm.get(id1);
  EXPECT_EQ(task1->blocks.size(), 1u);
  EXPECT_EQ(task1->blocks[0], "2");

  // Reverse dependency
  const Task* task2 = tm.get(id2);
  EXPECT_EQ(task2->blocked_by.size(), 1u);
  EXPECT_EQ(task2->blocked_by[0], "1");
}

TEST_F(TaskManagerTest, NoDuplicateDependencies) {
  std::string id1 = tm.create("Task 1", "Desc 1");
  std::string id2 = tm.create("Task 2", "Desc 2");

  json updates = {{"addBlockedBy", json::array({"1"})}};
  tm.update(id2, updates);
  tm.update(id2, updates);  // duplicate

  const Task* task2 = tm.get(id2);
  EXPECT_EQ(task2->blocked_by.size(), 1u);
}

TEST_F(TaskManagerTest, ListTasks) {
  tm.create("Task 1", "Desc 1");
  tm.create("Task 2", "Desc 2");
  tm.create("Task 3", "Desc 3");

  auto tasks = tm.list();
  EXPECT_EQ(tasks.size(), 3u);
}

TEST_F(TaskManagerTest, DeleteTask) {
  std::string id1 = tm.create("Task 1", "Desc 1");
  std::string id2 = tm.create("Task 2", "Desc 2");

  // Add dependency
  json updates = {{"addBlockedBy", json::array({"1"})}};
  tm.update(id2, updates);

  // Delete task 1
  EXPECT_TRUE(tm.remove(id1));
  EXPECT_EQ(tm.get(id1), nullptr);

  // Dependency should be cleaned up
  const Task* task2 = tm.get(id2);
  EXPECT_TRUE(task2->blocked_by.empty());

  auto tasks = tm.list();
  EXPECT_EQ(tasks.size(), 1u);
}

TEST_F(TaskManagerTest, DeleteNonExistent) {
  EXPECT_FALSE(tm.remove("999"));
}

TEST_F(TaskManagerTest, TaskToJson) {
  std::string id = tm.create("Test", "Description");
  json updates = {{"owner", "agent-1"}, {"status", "in_progress"}};
  tm.update(id, updates);

  const Task* task = tm.get(id);
  json j = task->to_json();

  EXPECT_EQ(j["id"], "1");
  EXPECT_EQ(j["subject"], "Test");
  EXPECT_EQ(j["description"], "Description");
  EXPECT_EQ(j["status"], "in_progress");
  EXPECT_EQ(j["owner"], "agent-1");
}

TEST_F(TaskManagerTest, TaskToSummaryJson) {
  std::string id = tm.create("Test", "Description");
  const Task* task = tm.get(id);
  json j = task->to_summary_json();

  EXPECT_EQ(j["id"], "1");
  EXPECT_EQ(j["subject"], "Test");
  EXPECT_EQ(j["status"], "pending");
  EXPECT_FALSE(j.contains("description"));  // summary should not include description
}

TEST_F(TaskManagerTest, CreateWithActiveForm) {
  std::string id = tm.create("Run tests", "Run all unit tests", "Running tests");
  const Task* task = tm.get(id);
  EXPECT_EQ(task->active_form, "Running tests");
}
