#include "lib/lib.hpp"

static constexpr std::string_view config_path = ".\\config.txt";
static constexpr int expected_config_line_count = 2;

struct Config {
    std::wstring port;
    std::wstring address;
};

Config read_config() {
    std::ifstream input_stream(config_path.data(), std::ios::in | std::ios::binary | std::ios::ate);

    if (!input_stream) {
        throw std::runtime_error("Failed to open config file");
    }

    auto size = input_stream.tellg();
    if (size == -1) {
        throw std::runtime_error("Failed to read size of config file");
    }

    input_stream.seekg(0);

    std::vector<char> buffer(size);
    input_stream.read(buffer.data(), size);

    input_stream.close();

    std::vector<std::span<char>> lines = read_lines(buffer);

    if (lines.size() != expected_config_line_count) {
        throw std::runtime_error(std::format("Expected {} config lines; got {} lines", expected_config_line_count, lines.size()));
    }

    Config config;
    config.port = convert(std::string(lines[0].begin(), lines[0].end()));
    config.address = convert(std::string(lines[1].begin(), lines[1].end()));
    return config;
}

int main() {
    Config config = read_config();
}