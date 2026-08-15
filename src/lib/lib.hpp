#pragma once

#include "encoding.hpp"
#include "win32.hpp"

std::vector<char>::iterator find_next_line_ending(std::vector<char> &buffer, std::vector<char>::iterator it);
std::vector<std::span<char>> read_lines(std::vector<char> &buffer);