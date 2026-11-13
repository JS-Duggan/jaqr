#pragma once

#include <string_view>
#include <vector>

// Splits source text into the tokeniser's input: one vector per line, each keeping its '\n'.
// A final line without a '\n' is kept as written, so tests can cover both endings.
inline std::vector<std::vector<char>> to_lines(std::string_view source) {
    std::vector<std::vector<char>> lines;
    std::vector<char> line;
    for (const char ch : source) {
        line.push_back(ch);
        if (ch == '\n') {
            lines.push_back(line);
            line.clear();
        }
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}
