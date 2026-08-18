#include "config.hpp"

#include "lib/lib.hpp"

static constexpr std::string_view config_subpath = ".\\config.txt";
static constexpr int expected_config_line_count = 2;
constexpr bool print_debug_strings = false;

Config read_config(std::string_view output_path) {
    if (print_debug_strings) {
        std::println("Reading config");
    }

    std::filesystem::path config_path = std::filesystem::path(output_path) / std::filesystem::path(config_subpath);

    std::vector<char> buffer = read_file(config_path.string());
    std::vector<std::span<char>> lines = read_lines(buffer);

    if (lines.size() != expected_config_line_count) {
        throw std::runtime_error(std::format("Expected {} config lines; got {} lines", expected_config_line_count, lines.size()));
    }

    Config config;
    config.port = convert(std::string(lines[0].begin(), lines[0].end()));
    config.address = convert(std::string(lines[1].begin(), lines[1].end()));
    return config;
}