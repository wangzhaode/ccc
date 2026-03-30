#include "write_tool.hpp"
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

ToolResult WriteTool::execute(const json& params) {
  std::string file_path = params.at("file_path").get<std::string>();
  std::string content = params.at("content").get<std::string>();

  // Create parent directories if needed
  fs::path p(file_path);
  if (p.has_parent_path()) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    if (ec) {
      return {"Error: Cannot create directory: " + ec.message(), true};
    }
  }

  std::ofstream file(file_path);
  if (!file.is_open()) {
    return {"Error: Cannot open file for writing: " + file_path, true};
  }

  file << content;
  file.close();

  if (file.fail()) {
    return {"Error: Failed to write file: " + file_path, true};
  }

  return {"File written successfully: " + file_path, false};
}
