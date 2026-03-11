#include "glob_tool.hpp"
#include <filesystem>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

// Simple glob pattern matching supporting * and **
bool GlobTool::match_pattern(const std::string& pattern, const std::string& path) {
    // Split pattern and path into segments
    auto split = [](const std::string& s, char delim) {
        std::vector<std::string> parts;
        std::string part;
        for (char c : s) {
            if (c == delim) {
                if (!part.empty()) parts.push_back(part);
                part.clear();
            } else {
                part += c;
            }
        }
        if (!part.empty()) parts.push_back(part);
        return parts;
    };

    // Simple wildcard match for a single segment (supports * but not **)
    std::function<bool(const std::string&, const std::string&)> match_segment;
    match_segment = [&](const std::string& pat, const std::string& str) -> bool {
        size_t pi = 0, si = 0;
        size_t star_p = std::string::npos, star_s = 0;
        while (si < str.size()) {
            if (pi < pat.size() && (pat[pi] == '?' || pat[pi] == str[si])) {
                pi++; si++;
            } else if (pi < pat.size() && pat[pi] == '*') {
                star_p = pi++;
                star_s = si;
            } else if (star_p != std::string::npos) {
                pi = star_p + 1;
                si = ++star_s;
            } else {
                return false;
            }
        }
        while (pi < pat.size() && pat[pi] == '*') pi++;
        return pi == pat.size();
    };

    auto pat_parts = split(pattern, '/');
    auto path_parts = split(path, '/');

    // Match with ** support using recursive backtracking
    std::function<bool(size_t, size_t)> match;
    match = [&](size_t pi, size_t si) -> bool {
        if (pi == pat_parts.size() && si == path_parts.size()) return true;
        if (pi == pat_parts.size()) return false;

        if (pat_parts[pi] == "**") {
            // ** matches zero or more path segments
            for (size_t i = si; i <= path_parts.size(); i++) {
                if (match(pi + 1, i)) return true;
            }
            return false;
        }

        if (si == path_parts.size()) return false;

        if (match_segment(pat_parts[pi], path_parts[si])) {
            return match(pi + 1, si + 1);
        }
        return false;
    };

    return match(0, 0);
}

ToolResult GlobTool::execute(const json& params) {
    std::string pattern = params.at("pattern").get<std::string>();
    std::string search_path = params.value("path", fs::current_path().string());

    if (!fs::exists(search_path) || !fs::is_directory(search_path)) {
        return {"Error: Directory not found: " + search_path, true};
    }

    std::vector<std::pair<std::string, fs::file_time_type>> matches;

    std::error_code ec;
    for (auto& entry : fs::recursive_directory_iterator(search_path,
            fs::directory_options::skip_permission_denied, ec)) {
        if (!entry.is_regular_file()) continue;

        std::string rel_path = fs::relative(entry.path(), search_path, ec).string();
        if (ec) continue;

        if (match_pattern(pattern, rel_path)) {
            matches.emplace_back(entry.path().string(), entry.last_write_time());
        }
    }

    // Sort by modification time (newest first)
    std::sort(matches.begin(), matches.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    if (matches.empty()) {
        return {"No files matched pattern: " + pattern, false};
    }

    std::string result;
    for (auto& [path, _] : matches) {
        result += path + "\n";
    }

    return {result, false};
}
