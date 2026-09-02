#pragma once

namespace Networking {

struct Header {
    int64_t message_index;
    int64_t packet_index;
    int64_t packet_count;
    int64_t other;
};

static constexpr int packet_size = 1'384;
static constexpr int header_size = sizeof(Header);
static constexpr int payload_size = packet_size - header_size;

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

struct Send_Resources {
    std::vector<uint8_t> buffer = std::vector<uint8_t>(packet_size);
    WSABUF wsabuf = {};
    DWORD bytes_sent = 0;
    Send_Resources();
};

enum MESSAGE_TYPE {
    MESSAGE_TYPE_NULL = 0,
    MESSAGE_TYPE_PORT_ACCEPTED = 1,
    MESSAGE_TYPE_MESSAGE_COMPLETE = 2,
    MESSAGE_TYPE_REQUEST_MISSING_RESOURCES = 3,
};

struct Packet {
    std::vector<uint8_t> buffer;
    Header header() const;
    int64_t packet_index() const;
};

struct Partial_Message {
    std::chrono::high_resolution_clock::time_point last_activity;
    std::vector<Packet> packets;
    int64_t message_index = 0;
    int64_t packet_count = 0;
    bool is_complete() const;
};

struct Message {
    int64_t index = 0;
    int64_t other = 0;
    std::vector<uint8_t> buffer;
    Message(size_t size);
};

WSABUF get_wsabuf(std::span<uint8_t> span);

struct Receive_State {
    std::mutex mutex;
    std::vector<Partial_Message> partial_messages;
    int64_t highest_message_index_encountered = -1;
    int64_t highest_message_index_completed = -1;
};

struct Completed_State {
    std::mutex mutex;
    std::condition_variable condition_variable;
    std::vector<std::vector<uint8_t>> completed_buffers;
    std::vector<int64_t> completed_indices;
};

struct Packets_Descriptor {
    int64_t message_index;
    std::vector<int64_t> packet_indices;
};

struct Missing_Resources {
    std::vector<int64_t> message_indices;
    std::vector<Packets_Descriptor> packets_descriptors;
};

struct Connection {
    Connection();
    void initialise(Settings settings);
    [[nodiscard]] std::optional<std::vector<uint8_t>> get_message();
    void join_threads();
private:
    static constexpr bool print_packet_debug_strings = false;
    static constexpr bool print_message_debug_strings = false;
    static constexpr LONG timer_period = 1'000'000; // 100 milliseconds in 100-nanosecond intervals.
    static constexpr std::chrono::milliseconds message_activity_cooldown = 100ms;

    Settings settings;
    bool initialised = false;

    std::mutex mutex;
    std::condition_variable condition_variable;
    bool shutting_down = false;

    std::mutex send_mutex;
    std::condition_variable send_cv;
    std::vector<Message> send_messages;
    size_t next_message_index = 0;
    size_t get_next_message_index();

    SOCKET socket = INVALID_SOCKET;
    void init_socket();

    HANDLE packet_received_event = NULL;
    static constexpr DWORD packet_received_event_index = 0;
    static constexpr DWORD shutdown_event_index = 1;
    std::array<HANDLE, 2> events = { NULL, NULL };
    Receive_State receive_state;
    Completed_State completed_state;

    std::vector<Send_Resources> get_message_packets(Message &message);
    void dispatch_message(Message &&message);
    std::vector<uint8_t> remove_message_and_get_payload(Partial_Message &message);
    std::optional<std::vector<uint8_t>> add_packet(Packet &&packet, Header &header); // Returns message buffer if it completes a message.
    void acknowledge_completed_message(int64_t message_index);
    bool wait_for_packet(Receive_Resources &resources);
    sockaddr get_server_address(Receive_Resources &resources);

    Receive_Resources get_receive_resources();
    void reset_receive_resources(Receive_Resources &resources) const;

    std::thread receive_thread;
    std::thread send_thread;
    std::thread missing_packet_thread;
    void receive_thread_function();
    void send_thread_function();
    void missing_packet_thread_function();
    void request_missing_resources();
    Missing_Resources get_missing_resources();
};

}