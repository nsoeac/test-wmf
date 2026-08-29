#pragma once

#include "config.hpp"
#include "window.hpp"
#include "renderer.hpp"
#include "connection.hpp"
#include "decoder.hpp"

struct App {
    struct Header {
        int32_t frame_index;
        int32_t format_index;
        int64_t timestamp;
    };

    struct Message {
        Header header;
        std::vector<uint8_t> buffer;
        std::span<uint8_t> frame;
    };

    HANDLE shutdown_event = NULL;
    Window window;
    Renderer renderer;
    Decoder decoder;
    Connection connection;

    static constexpr bool print_frame_debug_strings = false;

    App(Config &config);
    Message create_message(std::vector<uint8_t> &&buffer);
    void process_message(Message &message);
};

template <>
struct std::formatter<App::Header> {
    constexpr auto parse(auto &context) {
        return context.begin();
    }

    constexpr auto format(const App::Header &header, std::format_context &context) const {
        return std::format_to(context.out(), "{{ frame_index: {}, format_index: {}, timestamp: {} }}", header.frame_index, header.format_index, header.timestamp);
    }
};