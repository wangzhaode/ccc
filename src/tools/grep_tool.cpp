#include "grep_tool.hpp"
#include "glob_tool.hpp"
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

ToolResult GrepTool::execute(const json& params) {
  std::string pattern_str = params.at("pattern").get<std::string>();
  std::string search_path = params.value("path", fs::current_path().string());
  std::string glob_pattern = params.value("glob", "");
  std::string output_mode = params.value("output_mode", "files_with_matches");

  std::regex pattern;
  try {
    pattern = std::regex(pattern_str, std::regex::ECMAScript | std::regex::optimize);
  } catch (const std::regex_error& e) {
    return {"Error: Invalid regex pattern: " + std::string(e.what()), true};
  }

  // Collect files to search
  std::vector<std::string> files;
  if (fs::is_regular_file(search_path)) {
    files.push_back(search_path);
  } else if (fs::is_directory(search_path)) {
    std::error_code ec;
    for (auto& entry : fs::recursive_directory_iterator(
             search_path, fs::directory_options::skip_permission_denied, ec)) {
      if (!entry.is_regular_file())
        continue;

      // Skip binary-looking files and hidden directories
      std::string rel = fs::relative(entry.path(), search_path, ec).string();
      if (ec)
        continue;

      // Skip hidden directories
      if (rel.find("/.") != std::string::npos || rel[0] == '.')
        continue;

      // Apply glob filter
      if (!glob_pattern.empty() && !GlobTool::match_pattern(glob_pattern, rel)) {
        continue;
      }

      files.push_back(entry.path().string());
    }
  } else {
    return {"Error: Path not found: " + search_path, true};
  }

  std::string result;
  int match_count = 0;
  const int max_matches = 500;

  for (const auto& file : files) {
    if (match_count >= max_matches) {
      result += "\n... (results truncated at " + std::to_string(max_matches) + " matches)\n";
      break;
    }

    std::ifstream infile(file);
    if (!infile.is_open())
      continue;

    std::string line;
    int line_num = 0;
    bool file_matched = false;

    while (std::getline(infile, line)) {
      line_num++;
      if (match_count >= max_matches)
        break;

      try {
        if (std::regex_search(line, pattern)) {
          if (output_mode == "files_with_matches") {
            if (!file_matched) {
              result += file + "\n";
              file_matched = true;
              match_count++;
            }
            break;  // Only need one match per file for this mode
          } else {
            // content mode
            result += file + ":" + std::to_string(line_num) + ": " + line + "\n";
            match_count++;
          }
        }
      } catch (const std::regex_error&) {
        // Skip lines that cause regex errors (e.g. very long lines)
        continue;
      }
    }
  }

  if (result.empty()) {
    return {"No matches found for pattern: " + pattern_str, false};
  }

  return {result, false};
}
