#include "app.hpp"

#include "lib/lib.hpp"

App::App(Config &config) : connection(config) {
    shutdown_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (shutdown_event == NULL) {
        THROW_WIN32(CreateEventW);
    }

    window.init(shutdown_event);

    std::optional<std::vector<uint8_t>> initial_message = connection.get_initial_message(shutdown_event);

    if (!initial_message) {
        goto BEGIN_SHUTDOWN;
    }

    {
        std::span<int32_t> initial_values = { (int32_t *)((*initial_message).data()), sizeof(int32_t) * 4 };
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
                connection.condition_variable.wait(lock, [this]() { return !connection.buffers.empty() || window.started_shutting_down; });

                if (window.started_shutting_down) {
                    goto SHUTDOWN;
                }

                std::swap(connection.buffers, working_buffers);
            }

            for (size_t i = 0; i < working_buffers.size(); i++) {
                decoder.process_frame(std::move(working_buffers[i]));
            }

            working_buffers.clear();
        }
    }

BEGIN_SHUTDOWN:
    if (PostMessageW(window.handle, WM_CLOSE, 0, 0) == 0) {
        THROW_WIN32(PostMessageW);
    }

SHUTDOWN:
    if (renderer.thread.joinable()) {
        renderer.thread.join();
    }

    if (connection.thread.joinable()) {
        connection.thread.join();
    }

    if (decoder.thread.joinable()) {
        decoder.thread.join();
    }

    window.can_finish_shutting_down = true;
    window.shutting_down_condition_variable.notify_all();

    window.thread.join();
}