#pragma once

#include "config.hpp"

struct Connection {
    struct Header {
        int64_t message_index;
        int64_t packet_index;
        int64_t packet_count;
        int64_t placeholder;
    };

    static constexpr int packet_size = 1'384;
    static constexpr int header_size = sizeof(Header);
    static constexpr int payload_size = packet_size - header_size;
    static constexpr bool print_debug_strings = false;

    struct Packet {
        std::vector<uint8_t> buffer = std::vector<uint8_t>(packet_size);
        Header header() const;
        int64_t packet_index() const;
    };

    struct Message {
        std::vector<Packet> packets;
        int64_t message_index = 0;
        int64_t packet_count = 0;
        bool is_complete() const;
    };

    SOCKET socket = INVALID_SOCKET;

    std::vector<uint8_t> send_buffer = std::vector<uint8_t>(packet_size);
    WSABUF wsa_send_buffer = {};
    DWORD bytes_sent = 0;

    std::mutex mutex;
    std::condition_variable condition_variable;
    std::vector<std::vector<uint8_t>> buffers;

    std::wstring address;
    std::wstring port;

    std::vector<Message> messages;
    bool ready_packet_sent = false;

    [[nodiscard]] std::vector<uint8_t> remove_message_and_get_payload(Message &message);
    [[nodiscard]] std::optional<std::vector<uint8_t>> add_packet(Packet &&packet); // Returns message buffer if it completes a message.

    Connection(Config &config);
    std::vector<uint8_t> get_initial_message();
    void ready();

    std::thread receive_thread;
    void receive();
    static WSABUF get_wsabuf(std::span<uint8_t> span);
};