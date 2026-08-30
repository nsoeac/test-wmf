#pragma once

#include "config.hpp"
#include "window.hpp"
#include "renderer.hpp"
#include "connection.hpp"
#include "decoder.hpp"

namespace Application {

struct Header {
    int32_t frame_index;
    int32_t flags;
    int64_t timestamp;
};

struct Message {
    Header header;
    std::vector<uint8_t> buffer;
    std::span<uint8_t> contents;
};

struct App {
    HANDLE shutdown_event = NULL;
    Window window;
    Renderer renderer;
    Decoder decoder;
    Networking::Connection connection;

    static constexpr bool print_frame_debug_strings = false;

    App(Config &config);
    Message create_message(std::vector<uint8_t> &&buffer);
    Decoding::Message create_decoding_message(Message &&message);
};

}

using App = Application::App;

template <>
struct std::formatter<Application::Header> {
    constexpr auto parse(auto &context) {
        return context.begin();
    }

    constexpr auto format(const Application::Header &header, std::format_context &context) const {
        return std::format_to(context.out(), "{{ frame_index: {}, flags: {}, timestamp: {} }}", header.frame_index, header.flags, header.timestamp);
    }
};