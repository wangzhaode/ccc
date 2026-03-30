#include "read_tool.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>

ToolResult ReadTool::execute(const json& params) {
  std::string file_path = params.at("file_path").get<std::string>();
  int offset = params.value("offset", 1);
  int limit = params.value("limit", 2000);

  if (offset < 1)
    offset = 1;

  if (!std::filesystem::exists(file_path)) {
    return {"Error: File not found: " + file_path, true};
  }

  if (std::filesystem::is_directory(file_path)) {
    return {"Error: Path is a directory, not a file: " + file_path, true};
  }

  std::ifstream file(file_path);
  if (!file.is_open()) {
    return {"Error: Cannot open file: " + file_path, true};
  }

  std::ostringstream result;
  std::string line;
  int line_num = 0;
  int lines_output = 0;

  while (std::getline(file, line)) {
    line_num++;
    if (line_num < offset)
      continue;
    if (lines_output >= limit)
      break;

    // Truncate long lines
    if (line.length() > 2000) {
      line = line.substr(0, 2000) + "... (truncated)";
    }

    result << "     " << line_num << "\t" << line << "\n";
    lines_output++;
  }

  if (lines_output == 0) {
    return {"File is empty or offset is beyond file length.", false};
  }

  return {result.str(), false};
}
