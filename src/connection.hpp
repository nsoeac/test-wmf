#pragma once

struct Connection {
    static constexpr int packet_size = 1'384;

    struct Settings {
        std::wstring address;
        std::wstring port;
        std::chrono::milliseconds greeting_cooldown = 50ms;
        HANDLE shutdown_event = NULL;
    };

    struct Receive_Resources {
        WSABUF wsabuf = {};
        OVERLAPPED overlapped = {};
        DWORD flags = 0;
        DWORD bytes_received = 0;
        std::vector<uint8_t> buffer = std::vector<uint8_t>(packet_size);
    };

    enum MESSAGE_TYPE {
        MESSAGE_TYPE_NULL = 0,
        MESSAGE_TYPE_HELLO = 1,
        MESSAGE_TYPE_TEST_LATENCY = 2,
    };

    struct Header {
        int64_t message_index;
        int64_t packet_index;
        int64_t packet_count;
        int64_t other;
    };

    static constexpr int header_size = sizeof(Header);
    static constexpr int payload_size = packet_size - header_size;
    static constexpr bool print_packet_debug_strings = false;
    static constexpr bool print_message_debug_strings = false;

    struct Packet {
        std::vector<uint8_t> buffer;
        Header header() const;
        int64_t packet_index() const;
    };

    struct Message {
        std::vector<Packet> packets;
        int64_t message_index = 0;
        int64_t packet_count = 0;
        bool is_complete() const;
    };

    Settings settings;
    bool initialised = false;

    std::mutex mutex;
    std::condition_variable condition_variable;
    std::vector<std::vector<uint8_t>> buffers;
    bool connected = false;
    bool shutting_down = false;

    SOCKET socket = INVALID_SOCKET;
    HANDLE packet_received_event = NULL;
    static constexpr DWORD packet_received_event_index = 0;
    static constexpr DWORD shutdown_event_index = 1;
    std::array<HANDLE, 2> events = { NULL, NULL };

    std::vector<uint8_t> send_buffer = std::vector<uint8_t>(packet_size);
    WSABUF wsa_send_buffer = {};
    DWORD bytes_sent = 0;

    std::vector<Message> messages;

    [[nodiscard]] std::vector<uint8_t> remove_message_and_get_payload(Message &message);
    [[nodiscard]] std::optional<std::vector<uint8_t>> add_packet(Packet &&packet); // Returns message buffer if it completes a message.

    Connection();
    void initialise(Settings settings);

    std::thread thread;
    void thread_function();
    void receive();
    static WSABUF get_wsabuf(std::span<uint8_t> span);
    Receive_Resources get_receive_resources();
    void reset_receive_resources(Receive_Resources &resources) const;
    [[nodiscard]] std::optional<std::vector<uint8_t>> get_message();
private:
    [[nodiscard]] bool wait_for_packet(Receive_Resources &resources);
    [[nodiscard]] sockaddr get_server_address(Receive_Resources& resources);
};