#include "lib/lib.hpp"

std::vector<char>::iterator find_next_line_ending(std::vector<char> &buffer, std::vector<char>::iterator it) {
    bool non_line_ending_read = false;

    while (it != buffer.end()) {
        if ((*it == '\n') || (*it == '\r')) {
            if (non_line_ending_read) {
                return it;
            }
        } else {
            if (!non_line_ending_read) {
                non_line_ending_read = true;
            }
        }

        ++it;
    }

    return it;
}

std::vector<std::span<char>> read_lines(std::vector<char> &buffer) {
    auto it = buffer.begin();
    std::vector<std::span<char>> lines;

    while (it != buffer.end()) {
        auto line_beginning = it;

        while ((it != buffer.end()) && !((*it == '\r') || (*it == '\n'))) {
            ++it;
        }

        auto line_ending = it;

        if ((it != buffer.end()) && (*it == '\r')) {
            ++it;

            if ((it != buffer.end()) && (*it == '\n')) {
                ++it;
            }
        } else if ((it != buffer.end()) && (*it == '\n')) {
            ++it;
        }

        std::span<char> line(line_beginning, line_ending);
        lines.push_back(line);
    }

    return lines;
}