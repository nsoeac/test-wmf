#include "connection.hpp"

#include "lib/lib.hpp"

Connection::Header Connection::Packet::header() const {
    assert(buffer.size() >= header_size);

    Header header;
    std::memcpy(&header, buffer.data(), sizeof(Header));

    return header;
}

int64_t Connection::Packet::packet_index() const {
    assert(buffer.size() >= header_size);

    int64_t packet_index = 0;
    std::memcpy(&packet_index, buffer.data() + offsetof(Header, packet_index), sizeof(packet_index));

    return packet_index;
}

bool Connection::Message::is_complete() const {
    return packet_count == (int64_t)packets.size();
}

[[nodiscard]] std::vector<uint8_t> Connection::remove_message_and_get_payload(Message &message) {
    assert(message.is_complete());

    // Initialise buffer of appropriate size.

    int64_t buffer_size = std::ranges::fold_left(message.packets, 0, [](int64_t size, Packet &packet) { return size + ((int64_t)packet.buffer.size() - header_size); });
    std::vector<uint8_t> message_payload(buffer_size);

    // Copy all packet contents to the buffer.

    auto copy_destination = message_payload.begin();
    for (Packet &packet : message.packets) {
        std::span<uint8_t> packet_payload = { packet.buffer.begin() + header_size, packet.buffer.end() };
        std::ranges::copy(packet_payload, copy_destination);
        copy_destination += packet_payload.size();
    }

    // Remove the message.

    std::swap(message, messages.back());
    messages.pop_back();

    return message_payload;
}

// Returns message buffer if it completes a message.
[[nodiscard]] std::optional<std::vector<uint8_t>> Connection::add_packet(Packet &&packet) {
    Header header = packet.header();

    auto message_it = std::ranges::find_if(messages, [&header](int64_t message_index) { return message_index == header.message_index; }, &Message::message_index);

    // If the message doesn't exist, create it.

    if (message_it == messages.end()) {
        Message message;
        message.message_index = header.message_index;
        message.packet_count = header.packet_count;
        messages.push_back(message);
        message_it = std::prev(messages.end());
    }

    Message &message = *message_it;

    // Add the packet if we don't already have it.

    if (!std::ranges::contains(message.packets, header.packet_index, &Packet::packet_index)) {
        message.packets.push_back(std::move(packet));
        std::ranges::sort(message.packets, [](int64_t first_packet_index, int64_t second_packet_index) { return first_packet_index < second_packet_index; }, &Packet::packet_index);

        if (message.is_complete()) {
            std::vector<uint8_t> buffer = remove_message_and_get_payload(message);

            if (print_message_debug_strings) {
                std::println("Message {} ({} bytes) completed; {} messages remain", message.message_index, buffer.size(), messages.size());
            }

            return buffer;
        } else {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }
}

Connection::Connection() {
    wsa_send_buffer.buf = (CHAR *)send_buffer.data();
    wsa_send_buffer.len = (ULONG)send_buffer.size();

    packet_received_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (packet_received_event == NULL) {
        THROW_WIN32(CreateEventW);
    }
}

bool Connection::wait_for_packet(Receive_Resources &resources) {
    DWORD wait_result = WaitForMultipleObjects((DWORD)events.size(), events.data(), FALSE, INFINITE);
    bool wait_succeeded = (wait_result >= WAIT_OBJECT_0) && (wait_result < (WAIT_OBJECT_0 + (DWORD)events.size()));
    bool abandoned = (wait_result >= WAIT_ABANDONED_0) && (wait_result < (WAIT_ABANDONED_0 + (DWORD)events.size()));
    assert(!abandoned);
    if (wait_succeeded) {
        DWORD event_index = wait_result - WAIT_OBJECT_0;

        assert((event_index == packet_received_event_index) || (event_index == shutdown_event_index));

        if (event_index == packet_received_event_index) {
            if (WSAGetOverlappedResult(socket, &resources.overlapped, &resources.bytes_received, FALSE, &resources.flags) == FALSE) {
                THROW_WSA(WSAGetOverlappedResult);
            }

            return true;
        } else if (event_index == shutdown_event_index) {
            std::scoped_lock lock(mutex);
            shutting_down = true;
            return false;
        } else {
            return false;
        }
    } else {
        THROW_WIN32(WaitForMultipleObjects);
    }
}

Connection::Receive_Resources Connection::get_receive_resources() {
    Receive_Resources resources;
    resources.wsabuf = get_wsabuf(resources.buffer);
    resources.overlapped = { .hEvent = packet_received_event };
    return resources;
}

std::optional<std::vector<uint8_t>> Connection::get_message() {
    while (true) {
        std::unique_lock lock(mutex);
        condition_variable.wait(lock, [this]() { return !buffers.empty() || shutting_down; });

        if (shutting_down) {
            return std::nullopt;
        } else if (!buffers.empty()) {
            std::swap(buffers.front(), buffers.back());
            std::vector<uint8_t> buffer = std::move(buffers.back());
            buffers.pop_back();
            return buffer;
        }
    }
}

void Connection::reset_receive_resources(Receive_Resources &resources) const {
    resources.wsabuf = get_wsabuf(resources.buffer);
    resources.overlapped = { .hEvent = packet_received_event };
    resources.flags = 0;
    resources.bytes_received = 0;
}

sockaddr Connection::get_server_address(Receive_Resources &resources) {
    if (print_packet_debug_strings) {
        std::println("Getting server address");
    }

    while (true) {
        sockaddr server_address = {};
        INT server_address_length = sizeof(server_address);
        int receive_result = WSARecvFrom(socket, &resources.wsabuf, 1, NULL, &resources.flags, &server_address, &server_address_length, &resources.overlapped, NULL);
        if (receive_result == 0) {
            return server_address;
        } else {
            int last_error = WSAGetLastError();
            if (last_error == WSA_IO_PENDING) {
                if (wait_for_packet(resources)) {
                    return server_address;
                }
            } else {
                THROW_WSA_CODE(WSARecvFrom, last_error);
            }
        }

        reset_receive_resources(resources);
    }
}

void Connection::init_socket() {
    if (print_packet_debug_strings) {
        std::println("Resolving server address");
    }

    ADDRINFOW hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    PADDRINFOW send_info = NULL;
    if (GetAddrInfoW(settings.address.data(), settings.port.data(), &hints, &send_info) != 0) {
        THROW_WSA(GetAddrInfoW);
    }

    socket = WSASocketW(hints.ai_family, hints.ai_socktype, hints.ai_protocol, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (socket == INVALID_SOCKET) {
        THROW_WSA(WSASocketW);
    }

    assert(settings.greeting_cooldown.count() <= INT32_MAX);
    DWORD greeting_cooldown = (int32_t)settings.greeting_cooldown.count();
    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&greeting_cooldown, sizeof(greeting_cooldown)) == SOCKET_ERROR) {
        THROW_WSA(setsockopt);
    }

    if (WSAConnect(socket, send_info->ai_addr, sizeof(sockaddr_in), NULL, NULL, NULL, NULL) == SOCKET_ERROR) {
        THROW_WSA(WSAConnect);
    }

    if (print_packet_debug_strings) {
        std::println("Greeting server");
    }

    WSABUF empty_wsabuf = {};
    if (WSASend(socket, &empty_wsabuf, 1, &bytes_sent, 0, NULL, NULL) != 0) {
        THROW_WSA(WSASend);
    }

    Receive_Resources resources = get_receive_resources();
    sockaddr server_address = get_server_address(resources);

    assert(resources.bytes_received == sizeof(int32_t));

    std::vector<uint8_t> initial_message;
    uint16_t server_port = (uint16_t)*(int32_t *)resources.buffer.data();
    if (server_port == 0) {
        throw std::runtime_error(std::format("Server receive port is {}", server_port));
    }

    ((sockaddr_in *)&server_address)->sin_port = htons(server_port);

    if (print_packet_debug_strings) {
        std::println("Received {} bytes; server port is {}", resources.bytes_received, server_port);
    }

    if (WSAConnect(socket, &server_address, sizeof(sockaddr_in), NULL, NULL, NULL, NULL) == SOCKET_ERROR) {
        THROW_WSA(WSAConnect);
    }

    {
        std::unique_lock lock(mutex);
        connected = true;
    }

    condition_variable.notify_all();

    if (print_packet_debug_strings) {
        std::println("Sending 'ready' packet to server");
    }

    if (WSASend(socket, &wsa_send_buffer, 1, &bytes_sent, 0, NULL, NULL) != 0) {
        THROW_WSA(WSASend);
    }
}

void Connection::receive_thread_function() {
    init_socket();

    while (true) {
        std::optional<std::vector<uint8_t>> message_buffer_option = std::nullopt;

        while (message_buffer_option == std::nullopt) {
            Receive_Resources resources = get_receive_resources();
            Packet packet;
            int receive_result = WSARecv(socket, &resources.wsabuf, 1, NULL, &resources.flags, &resources.overlapped, NULL);
            if (receive_result != 0) {
                int last_error = WSAGetLastError();
                if (last_error != WSA_IO_PENDING) {
                    THROW_WSA_CODE(WSARecvFrom, last_error);
                }
            }

            if (!wait_for_packet(resources)) {
                std::println("Shutting down while getting message");
                goto SHUTDOWN;
            }

            if (print_packet_debug_strings) {
                std::println("Received {} byte(s) from server", resources.bytes_received);
            }

            resources.buffer.resize(resources.bytes_received);

            packet.buffer = std::move(resources.buffer);
            message_buffer_option = add_packet(std::move(packet));
        }

        {
            std::unique_lock lock(mutex);
            buffers.push_back(std::move(*message_buffer_option));
        }

        condition_variable.notify_all();
    }

SHUTDOWN:
    condition_variable.notify_all(); // Because the main thread can block on the mutex.
}

void Connection::send_thread_function() {
}

void Connection::initialise(Settings settings_) {
    assert(!initialised);
    settings = settings_;

    events = { packet_received_event, settings.shutdown_event };

    initialised = true;

    receive_thread = std::thread(&Connection::receive_thread_function, this);

    {
        std::unique_lock lock(mutex);
        condition_variable.wait(lock, [this]() { return connected || shutting_down; });

        if (shutting_down) {
            return;
        }
    }

    send_thread = std::thread(&Connection::send_thread_function, this);
}

WSABUF Connection::get_wsabuf(std::span<uint8_t> span) {
    WSABUF wsabuf = {};
    wsabuf.buf = (CHAR *)span.data();
    wsabuf.len = (ULONG)span.size();
    return wsabuf;
}

void Connection::join_threads() {
    if (receive_thread.joinable()) {
        receive_thread.join();
    }

    if (send_thread.joinable()) {
        send_thread.join();
    }
}