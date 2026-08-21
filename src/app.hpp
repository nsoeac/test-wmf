#pragma once

#include "config.hpp"
#include "window.hpp"
#include "renderer.hpp"
#include "connection.hpp"
#include "decoder.hpp"

struct App {
    Window window;
    Renderer renderer;
    Decoder decoder;
    Connection connection;

    App(Config &config) : connection(config) {
        std::vector<uint8_t> initial_message = connection.get_initial_message();

        std::span<int32_t> initial_values = { (int32_t *)(initial_message.data()), (int32_t *)(initial_message.data() + sizeof(int32_t) * 4) };
        unsigned video_width = initial_values[1];
        unsigned video_height = initial_values[2];
        unsigned video_framerate = initial_values[3];

        renderer.start(window.handle, &decoder, &window);
        decoder.start(video_width, video_height, video_framerate, &renderer);
        connection.ready();

        std::vector<std::vector<uint8_t>> working_buffers;
        while (!window.started_shutting_down) {
            {
                std::unique_lock lock(connection.mutex);
                connection.condition_variable.wait(lock, [this]() { return !connection.buffers.empty(); });
                std::swap(connection.buffers, working_buffers);
            }

            for (std::vector<uint8_t> &buffer : working_buffers) {
                decoder.process_frame(buffer);
            }

            working_buffers.clear();
        }
    }
};