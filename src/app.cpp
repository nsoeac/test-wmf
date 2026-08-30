#include "app.hpp"

#include "lib/lib.hpp"

Decoding::Message App::create_message(std::vector<uint8_t> &&buffer) {
    assert(buffer.size() >= sizeof(Decoding::Header));

    Decoding::Message message;
    message.buffer = std::move(buffer);
    message.header = *(Decoding::Header *)message.buffer.data();
    message.frame = std::span(message.buffer.data() + sizeof(Decoding::Header), message.buffer.data() + message.buffer.size());
    return message;
}

App::App(Config &config) {
    shutdown_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (shutdown_event == NULL) {
        THROW_WIN32(CreateEventW);
    }

    window.init(shutdown_event);
    renderer.start(window.handle, &decoder, &window);

    {
        Networking::Settings connection_settings;
        connection_settings.address = config.address;
        connection_settings.port = config.port;
        connection_settings.shutdown_event = shutdown_event;
        connection.initialise(connection_settings);
    }

    {
        std::vector<Decoding::Message> messages;

        while (true) {
            std::optional<std::vector<uint8_t>> message_buffer = connection.get_message();

            if (!message_buffer) {
                goto BEGIN_SHUTDOWN;
            }

            {
                Decoding::Message message = create_message(std::move(*message_buffer));
                std::println("{}", message.header);
                if (message.header.frame_index == -1) {
                    std::span<int32_t> initial_values = { (int32_t *)(message.frame.data()), sizeof(int32_t) * 3 };
                    unsigned video_width = initial_values[0];
                    unsigned video_height = initial_values[1];
                    unsigned video_framerate = initial_values[2];

                    std::println("{{ video_width: {}, video_height: {}, video_framerate: {} }}", video_width, video_height, video_framerate);

                    decoder.start(video_width, video_height, video_framerate, &renderer);
                    break;
                } else {
                    messages.push_back(std::move(message));
                }
            }
        }

        for (Decoding::Message &message : messages) {
            decoder.process_message(std::move(message));
        }

        messages.clear();
    }

    while (!window.started_shutting_down) {
        std::optional<std::vector<uint8_t>> message_buffer = connection.get_message();

        if (!message_buffer) {
            goto BEGIN_SHUTDOWN;
        }

        {
            Decoding::Message message = create_message(std::move(*message_buffer));

            if (message.header.frame_index == -1) {
                if (print_frame_debug_strings) {
                    std::println("Duplicate initial packet; dropping", message.header, message.frame.size());
                }
                continue;
            } else {
                if (print_frame_debug_strings) {
                    std::println("{}: {} bytes", message.header, message.frame.size());
                }
            }

            decoder.process_message(std::move(message));
        }
    }

BEGIN_SHUTDOWN:
    if (PostMessageW(window.handle, WM_CLOSE, 0, 0) == 0) {
        THROW_WIN32(PostMessageW);
    }

    if (renderer.thread.joinable()) {
        renderer.thread.join();
    }

    connection.join_threads();

    if (decoder.thread.joinable()) {
        decoder.thread.join();
    }

    window.can_finish_shutting_down = true;
    window.shutting_down_condition_variable.notify_all();

    window.thread.join();
}