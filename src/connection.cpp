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
            return buffer;
        } else {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }
}

Connection::Connection(Config &config) :
    address(config.address),
    port(config.port) {
    wsa_send_buffer.buf = (CHAR *)send_buffer.data();
    wsa_send_buffer.len = (ULONG)send_buffer.size();

    packet_received_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (packet_received_event == NULL) {
        THROW_WIN32(CreateEventW);
    }
}

bool Connection::wait_for_packet(OVERLAPPED *overlapped, DWORD *bytes_transferred, DWORD *flags) {
    DWORD wait_result = WaitForMultipleObjects((DWORD)events.size(), events.data(), FALSE, INFINITE);
    bool wait_succeeded = (wait_result >= WAIT_OBJECT_0) && (wait_result < (WAIT_OBJECT_0 + (DWORD)events.size()));
    bool abandoned = (wait_result >= WAIT_ABANDONED_0) && (wait_result < (WAIT_ABANDONED_0 + (DWORD)events.size()));
    assert(!abandoned);
    if (wait_succeeded) {
        DWORD event_index = wait_result - WAIT_OBJECT_0;
        bool succeeded = event_index == packet_received_event_index;
        if (succeeded) {
            if (WSAGetOverlappedResult(socket, overlapped, bytes_transferred, FALSE, flags) == FALSE) {
                THROW_WSA(WSAGetOverlappedResult);
            }

            return true;
        } else {
            return false;
        }
    } else {
        THROW_WIN32(WaitForMultipleObjects);
    }
}

std::optional<std::vector<uint8_t>> Connection::get_initial_message(HANDLE shutdown_event) {
    shutdown_event_ = shutdown_event;
    events = { packet_received_event, shutdown_event_ };

    if (print_debug_strings) {
        std::println("Resolving server address");
    }

    ADDRINFOW hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    PADDRINFOW send_info = NULL;
    if (GetAddrInfoW(address.data(), port.data(), &hints, &send_info) != 0) {
        THROW_WSA(GetAddrInfoW);
    }

    socket = WSASocketW(hints.ai_family, hints.ai_socktype, hints.ai_protocol, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (socket == INVALID_SOCKET) {
        THROW_WSA(WSASocketW);
    }

    if (WSAConnect(socket, send_info->ai_addr, sizeof(sockaddr_in), NULL, NULL, NULL, NULL) == SOCKET_ERROR) {
        THROW_WSA(WSAConnect);
    }

    if (print_debug_strings) {
        std::println("Greeting server");
    }

    if (WSASend(socket, &wsa_send_buffer, 1, &bytes_sent, 0, NULL, NULL) != 0) {
        THROW_WSA(WSASend);
    }

    if (print_debug_strings) {
        std::println("Waiting for server response");
    }

    sockaddr server_address = {};
    INT server_address_length = sizeof(server_address);
    DWORD receive_flags = 0;
    std::vector<uint8_t> receive_buffer(packet_size);
    WSABUF wsa_receive_buffer = get_wsabuf(receive_buffer);
    OVERLAPPED overlapped = { .hEvent = packet_received_event };
    int receive_result = WSARecvFrom(socket, &wsa_receive_buffer, 1, NULL, &receive_flags, &server_address, &server_address_length, &overlapped, NULL);
    if (receive_result != 0) {
        int last_error = WSAGetLastError();
        if (last_error != WSA_IO_PENDING) {
            THROW_WSA_CODE(WSARecvFrom, last_error);
        }
    }

    DWORD bytes_received = 0;
    if (!wait_for_packet(&overlapped, &bytes_received, &receive_flags)) {
        std::println("Shutting down while getting initial packet");
        return std::nullopt;
    }

    assert(bytes_received >= (sizeof(int32_t) * 4));

    std::vector<uint8_t> initial_message;
    initial_message.assign_range(std::span(receive_buffer.begin(), bytes_received));
    std::span<int32_t> initial_values = { (int32_t *)(receive_buffer.data()), 4 };

    uint16_t server_port = (uint16_t)initial_values[0];
    if (server_port == 0) {
        throw std::runtime_error(std::format("Server receive port is {}", server_port));
    }

    ((sockaddr_in *)&server_address)->sin_port = htons(server_port);

    if (print_debug_strings) {
        std::println("Received {} bytes; server port is {}", bytes_received, server_port);
    }

    if (WSAConnect(socket, &server_address, sizeof(sockaddr_in), NULL, NULL, NULL, NULL) == SOCKET_ERROR) {
        THROW_WSA(WSAConnect);
    }

    return initial_message;
}

void Connection::receive() {
    while (true) {
        std::optional<std::vector<uint8_t>> message_buffer_option = std::nullopt;

        while (message_buffer_option == std::nullopt) {
            Packet packet;
            WSABUF packet_wsabuf = get_wsabuf(packet.buffer);

            DWORD flags = 0;
            OVERLAPPED overlapped = { .hEvent = packet_received_event };
            int receive_result = WSARecv(socket, &packet_wsabuf, 1, NULL, &flags, &overlapped, NULL);
            if (receive_result != 0) {
                int last_error = WSAGetLastError();
                if (last_error != WSA_IO_PENDING) {
                    THROW_WSA_CODE(WSARecvFrom, last_error);
                }
            }

            DWORD bytes_received = 0;
            if (!wait_for_packet(&overlapped, &bytes_received, &flags)) {
                std::println("Shutting down while getting message");
                goto SHUTDOWN;
            }

            if (print_debug_strings) {
                std::println("Received {} byte(s) from server ({} messages remain)", bytes_received, messages.size());
            }

            packet.buffer.resize(bytes_received);
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

void Connection::ready() {
    assert(!ready_packet_sent);

    if (print_debug_strings) {
        std::println("Sending 'ready' packet to server");
    }

    if (WSASend(socket, &wsa_send_buffer, 1, &bytes_sent, 0, NULL, NULL) != 0) {
        THROW_WSA(WSASend);
    }

    ready_packet_sent = true;

    thread = std::thread(&Connection::receive, this);
}

WSABUF Connection::get_wsabuf(std::span<uint8_t> span) {
    WSABUF wsabuf = {};
    wsabuf.buf = (CHAR *)span.data();
    wsabuf.len = (ULONG)span.size();
    return wsabuf;
}