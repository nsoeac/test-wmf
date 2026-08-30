#pragma once

#include "config.hpp"
#include "window.hpp"
#include "renderer.hpp"
#include "connection.hpp"
#include "decoder.hpp"

struct App {
    HANDLE shutdown_event = NULL;
    Window window;
    Renderer renderer;
    Decoder decoder;
    Networking::Connection connection;

    static constexpr bool print_frame_debug_strings = false;

    App(Config &config);
    Decoding::Message create_message(std::vector<uint8_t> &&buffer);
};