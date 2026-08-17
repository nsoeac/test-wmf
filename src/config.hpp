#pragma once

struct Config {
    std::wstring port;
    std::wstring address;
};

Config read_config();