#include "edit_tool.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>

ToolResult EditTool::execute(const json& params) {
    std::string file_path = params.at("file_path").get<std::string>();
    std::string old_string = params.at("old_string").get<std::string>();
    std::string new_string = params.at("new_string").get<std::string>();
    bool replace_all = params.value("replace_all", false);

    if (!std::filesystem::exists(file_path)) {
        return {"Error: File not found: " + file_path, true};
    }

    if (old_string == new_string) {
        return {"Error: old_string and new_string are identical", true};
    }

    // Read file content
    std::ifstream infile(file_path);
    if (!infile.is_open()) {
        return {"Error: Cannot open file: " + file_path, true};
    }
    std::stringstream ss;
    ss << infile.rdbuf();
    std::string content = ss.str();
    infile.close();

    // Count occurrences
    int count = 0;
    size_t pos = 0;
    while ((pos = content.find(old_string, pos)) != std::string::npos) {
        count++;
        pos += old_string.length();
    }

    if (count == 0) {
        return {"Error: old_string not found in file. Make sure the string matches exactly.", true};
    }

    if (count > 1 && !replace_all) {
        return {"Error: old_string found " + std::to_string(count) +
                " times. Provide more context to make it unique, or set replace_all to true.", true};
    }

    // Perform replacement
    std::string result = content;
    if (replace_all) {
        pos = 0;
        while ((pos = result.find(old_string, pos)) != std::string::npos) {
            result.replace(pos, old_string.length(), new_string);
            pos += new_string.length();
        }
    } else {
        pos = result.find(old_string);
        result.replace(pos, old_string.length(), new_string);
    }

    // Write back
    std::ofstream outfile(file_path);
    if (!outfile.is_open()) {
        return {"Error: Cannot open file for writing: " + file_path, true};
    }
    outfile << result;
    outfile.close();

    std::string msg = "Edited " + file_path + ": replaced " + std::to_string(replace_all ? count : 1) + " occurrence(s).";
    return {msg, false};
}
