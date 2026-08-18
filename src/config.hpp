#pragma once

struct Config {
    std::wstring port;
    std::wstring address;
    std::string config_path;
};

Config read_config(std::string_view output_path);