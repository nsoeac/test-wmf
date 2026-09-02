#include "connection.hpp"

#include "lib/lib.hpp"

namespace Networking {

Header Packet::header() const {
    assert(buffer.size() >= header_size);

    Header header;
    std::memcpy(&header, buffer.data(), sizeof(Header));

    return header;
}

int64_t Packet::packet_index() const {
    assert(buffer.size() >= header_size);

    int64_t packet_index = 0;
    std::memcpy(&packet_index, buffer.data() + offsetof(Header, packet_index), sizeof(packet_index));

    return packet_index;
}

bool Partial_Message::is_complete() const {
    return packet_count == (int64_t)packets.size();
}

Send_Resources::Send_Resources() {
    wsabuf.buf = (CHAR *)buffer.data();
    wsabuf.len = (ULONG)buffer.size();
}

WSABUF get_wsabuf(std::span<uint8_t> span) {
    WSABUF wsabuf = {};
    wsabuf.buf = (CHAR *)span.data();
    wsabuf.len = (ULONG)span.size();
    return wsabuf;
}

Message::Message(size_t size) : buffer(size) {}

#pragma region Connection
std::vector<uint8_t> Connection::remove_message_and_get_payload(Partial_Message &message) {
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

    std::swap(message, receive_state.partial_messages.back());
    receive_state.partial_messages.pop_back();

    return message_payload;
}

// Returns message buffer if it completes a message.
std::optional<std::vector<uint8_t>> Connection::add_packet(Packet &&packet, Header &header) {
    std::scoped_lock lock(receive_state.mutex);

    auto message_it = std::ranges::find_if(receive_state.partial_messages, [&header](int64_t message_index) { return message_index == header.message_index; }, &Partial_Message::message_index);

    // If the message doesn't exist, create it.

    if (message_it == receive_state.partial_messages.end()) {
        Partial_Message message;
        message.last_activity = std::chrono::high_resolution_clock::now();
        message.message_index = header.message_index;
        message.packet_count = header.packet_count;

        if (receive_state.highest_message_index_encountered < header.message_index) {
            receive_state.highest_message_index_encountered = header.message_index;
        }

        receive_state.partial_messages.push_back(message);
        message_it = std::prev(receive_state.partial_messages.end());
    }

    Partial_Message &message = *message_it;

    // Add the packet if we don't already have it.

    if (!std::ranges::contains(message.packets, header.packet_index, &Packet::packet_index)) {
        message.packets.push_back(std::move(packet));
        std::ranges::sort(message.packets, [](int64_t first_packet_index, int64_t second_packet_index) { return first_packet_index < second_packet_index; }, &Packet::packet_index);

        if (message.is_complete()) {
            if (receive_state.highest_message_index_completed < header.message_index) {
                receive_state.highest_message_index_completed = header.message_index;
            }

            std::vector<uint8_t> buffer = remove_message_and_get_payload(message);

            if (print_message_debug_strings) {
                std::println("Message {} ({} bytes) completed; {} messages remain", message.message_index, buffer.size(), receive_state.partial_messages.size());
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

            std::scoped_lock send_lock(send_mutex);
            send_cv.notify_all();

            return false;
        } else {
            return false;
        }
    } else {
        THROW_WIN32(WaitForMultipleObjects);
    }
}

Receive_Resources Connection::get_receive_resources() {
    Receive_Resources resources;
    resources.wsabuf = get_wsabuf(resources.buffer);
    resources.overlapped = { .hEvent = packet_received_event };
    return resources;
}

std::optional<std::vector<uint8_t>> Connection::get_message() {
    while (true) {
        std::unique_lock lock(completed_state.mutex);
        completed_state.condition_variable.wait(lock, [this]() { return !completed_state.completed_buffers.empty() || shutting_down; });

        if (shutting_down) {
            return std::nullopt;
        } else if (!completed_state.completed_buffers.empty()) {
            std::vector<uint8_t> buffer = std::move(completed_state.completed_buffers.front());
            completed_state.completed_buffers.erase(completed_state.completed_buffers.begin());
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

    sockaddr server_address = {};
    INT server_address_length = sizeof(server_address);
    int receive_result = WSARecvFrom(socket, &resources.wsabuf, 1, NULL, &resources.flags, &server_address, &server_address_length, &resources.overlapped, NULL);

    if (shutting_down) {
        return {};
    }

    if (receive_result == 0) {
        return server_address;
    } else {
        int last_error = WSAGetLastError();
        if (last_error == WSA_IO_PENDING) {
            if (wait_for_packet(resources)) {
                return server_address;
            } else {
                throw std::runtime_error("Failed to get server address");
            }
        } else {
            THROW_WSA_CODE(WSARecvFrom, last_error);
        }
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

    {
        WSABUF wsabuf = {};
        DWORD bytes_sent = 0;
        if (WSASend(socket, &wsabuf, 1, &bytes_sent, 0, NULL, NULL) != 0) {
            THROW_WSA(WSASend);
        }
    }

    {
        Receive_Resources resources = get_receive_resources();
        sockaddr server_address = get_server_address(resources);

        if (shutting_down) {
            std::println("Got server address but shutting down");
            return;
        } else {
            std::println("Got server address");
        }

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
    }
}

size_t Connection::get_next_message_index() {
    size_t message_index = next_message_index;
    next_message_index++;
    return message_index;
}

std::vector<Send_Resources> Connection::get_message_packets(Message &message) {
    size_t packet_count = message.buffer.size() / payload_size + ((message.buffer.size() % payload_size) > 0) ? 1 : 0;

    if (packet_count == 0) {
        packet_count = 1;
    }

    Header header = {};
    header.message_index = message.index;
    header.packet_count = packet_count;
    header.other = message.other;

    std::vector<Send_Resources> packets(packet_count);
    size_t bytes_read = 0;
    for (size_t i = 0; i < packet_count; i++) {
        Send_Resources &resources = packets[i];

        header.packet_index = i;
        std::memcpy(resources.buffer.data(), &header, sizeof(Header));
        size_t bytes_to_read = std::min<size_t>(message.buffer.size() - bytes_read, payload_size);
        std::memcpy(resources.buffer.data() + sizeof(Header), message.buffer.data() + bytes_read, bytes_to_read);

        bytes_read += bytes_to_read;
        resources.wsabuf.len = (ULONG)(bytes_to_read + header_size);
    }

    assert(bytes_read == message.buffer.size());

    return packets;
}

void Connection::initialise(Settings settings_) {
    assert(!initialised);
    settings = settings_;

    events = { packet_received_event, settings.shutdown_event };

    init_socket();

    initialised = true;

    receive_thread = std::thread(&Connection::receive_thread_function, this);
    send_thread = std::thread(&Connection::send_thread_function, this);
    missing_packet_thread = std::thread(&Connection::missing_packet_thread_function, this);

    Message ready_message(0);
    ready_message.other = MESSAGE_TYPE_PORT_ACCEPTED;
    dispatch_message(std::move(ready_message));
}

void Connection::join_threads() {
    if (receive_thread.joinable()) {
        receive_thread.join();
    }

    if (send_thread.joinable()) {
        send_thread.join();
    }

    if (missing_packet_thread.joinable()) {
        missing_packet_thread.join();
    }

    if (socket != INVALID_SOCKET) {
        if (closesocket(socket) == SOCKET_ERROR) {
            THROW_WSA(closesocket);
        }
    }
}

void Connection::dispatch_message(Message &&message) {
    {
        std::scoped_lock lock(send_mutex);
        send_messages.push_back(std::move(message));
    }

    send_cv.notify_all();
}

void Connection::acknowledge_completed_message(int64_t message_index) {
    Message message(0);
    message.index = message_index;
    message.other = (int64_t)MESSAGE_TYPE_MESSAGE_COMPLETE;

    if (print_message_debug_strings) {
        std::println("Dispatching message to acknowledge message {}", message_index);
    }

    dispatch_message(std::move(message));

    if (print_message_debug_strings) {
        std::println("Dispatched message to acknowledge message {}", message_index);
    }
}

void Connection::receive_thread_function() {
    while (true) {
        std::optional<std::vector<uint8_t>> message_buffer_option = std::nullopt;
        Header header = {};

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
            header = packet.header();
            message_buffer_option = add_packet(std::move(packet), header);

            if (message_buffer_option) {
                acknowledge_completed_message(header.message_index);
            }
        }

        {
            std::scoped_lock lock(completed_state.mutex);
            completed_state.completed_buffers.push_back(std::move(*message_buffer_option));

            auto index_insert_location = std::ranges::find_if(completed_state.completed_indices, [&header](int64_t message_index) { return message_index > header.message_index; });
            completed_state.completed_indices.insert(index_insert_location, header.message_index);
        }

        completed_state.condition_variable.notify_all();
    }

SHUTDOWN:
    condition_variable.notify_all(); // Because the main thread can block on the mutex.
}

void Connection::send_thread_function() {
    while (true) {
        std::unique_lock lock(send_mutex);
        send_cv.wait(lock, [this]() { return !send_messages.empty() || shutting_down; });

        if (shutting_down) {
            break;
        }

        for (size_t i = 0; i < send_messages.size(); i++) {
            Message &message = send_messages[i];
            std::vector<Send_Resources> packets = get_message_packets(message);

            for (size_t j = 0; j < packets.size(); j++) {
                Send_Resources &packet = packets[j];
                if (WSASend(socket, &packet.wsabuf, 1, &packet.bytes_sent, 0, NULL, NULL) == SOCKET_ERROR) {
                    THROW_WSA(WSASend);
                }

                if (print_packet_debug_strings) {
                    std::println("Sent {} byte(s) to server", packet.bytes_sent);
                }
            }
        }

        send_messages.clear();
    }
}

static std::vector<int64_t> get_partial_message_missing_packet_indices(Partial_Message &partial_message) {
    auto it = partial_message.packets.begin();
    std::vector<int64_t> result;

    int64_t packet_index = 0;
    while (packet_index != partial_message.packet_count) {
        if (it == partial_message.packets.end()) {
            // There are no more packets from the current index to the packet count (exclusive), so add in all the remaining indices.

            size_t start_index = result.size();
            size_t indices_to_add = partial_message.packet_count - packet_index - 1;
            result.resize(result.size() + indices_to_add);
            std::iota(result.begin() + start_index, result.end(), packet_index);
            packet_index += indices_to_add;
        } else {
            Packet &packet = *it;
            int64_t packet_packet_index = packet.packet_index();
            if (packet_packet_index == packet_index) {
                // The current packet index corresponds to an existing packet.
                packet_index++;
                ++it;
            } else {
                assert(packet_packet_index > packet_index);

                // The packet's index is greater than the current packet index, so add all the missing packet indices up to the packet's index.
                // Afterwards increment the iterator and the current packet index to skip the matching packet.

                size_t indices_to_add = packet_packet_index - packet_index;
                size_t start_index = result.size();
                result.resize(result.size() + indices_to_add);
                std::iota(result.begin() + start_index, result.end(), packet_index);

                packet_index += indices_to_add + 1;
                ++it;
            }
        }
    }

    return result;
}

Missing_Resources Connection::get_missing_resources() {
    auto is_message_complete = [this](std::vector<int64_t>::iterator &completed_message_index_it, int64_t message_index) -> bool {
        while (completed_message_index_it != completed_state.completed_indices.end()) {
            int64_t completed_message_index = *completed_message_index_it;
            if (completed_message_index == message_index) {
                return true;
            } else if (completed_message_index > message_index) {
                return false;
            } else {
                ++completed_message_index_it;
            }
        }

        return false;
    };

    auto get_missing_descriptor = [this](std::vector<Partial_Message>::iterator &partial_message_it, int64_t message_index) -> std::optional<Packets_Descriptor> {
        while (partial_message_it != receive_state.partial_messages.end()) {
            Partial_Message &partial_message = *partial_message_it;
            if (partial_message.message_index == message_index) {
                Packets_Descriptor missing_descriptor = {};
                missing_descriptor.message_index = message_index;
                missing_descriptor.packet_indices = get_partial_message_missing_packet_indices(partial_message);
                return missing_descriptor;
            } else if (partial_message.message_index > message_index) {
                return std::nullopt;
            } else {
                ++partial_message_it;
            }
        }

        return std::nullopt;
    };

    std::scoped_lock receive_lock(receive_state.mutex);
    std::scoped_lock completed_lock(completed_state.mutex);

    Missing_Resources missing_resources;
    auto partial_it = receive_state.partial_messages.begin();
    auto completed_message_index_it = completed_state.completed_indices.begin();
    for (int64_t message_index = (receive_state.highest_message_index_completed + 1); message_index <= receive_state.highest_message_index_encountered; message_index++) {
        // For each message index from the next index to complete to the highest one encountered:
        // - If the message index has a message, then call `get_partial_message_missing_packet_indices`.
        // - If the message index has no message, then add it to `missing_message_indices`.

        bool message_is_complete = is_message_complete(completed_message_index_it, message_index);

        if (message_is_complete) {
            continue;
        }

        std::optional<Packets_Descriptor> missing_descriptor_opt = get_missing_descriptor(partial_it, message_index);

        if (missing_descriptor_opt) {
            missing_resources.packets_descriptors.push_back(std::move(*missing_descriptor_opt));
        } else {
            // Message is missing.

            missing_resources.message_indices.push_back(message_index);
        }
    }

    return missing_resources;
}

void Connection::request_missing_resources() {
    std::println("request_missing_resources called");

    Missing_Resources missing_resources = get_missing_resources();
}

void Connection::missing_packet_thread_function() {
    HANDLE timer = CreateWaitableTimerW(NULL, FALSE, NULL);
    if (timer == NULL) {
        THROW_WIN32(CreateWaitableTimerW);
    }

    if (SetWaitableTimer(timer, 0, timer_period, NULL, NULL, FALSE) == 0) {
        THROW_WIN32(SetWaitableTimer);
    }

    std::array<HANDLE, 2> handles = { timer, events[shutdown_event_index] };
    DWORD handle_count = (DWORD)handles.size();

    while (!shutting_down) {
        DWORD wait_result = WaitForMultipleObjects(handle_count, handles.data(), FALSE, 0);
        bool succeeded = (wait_result >= WAIT_OBJECT_0) && (wait_result < (WAIT_OBJECT_0 + handle_count));
        bool abandoned = (wait_result >= WAIT_ABANDONED_0) && (wait_result < (WAIT_ABANDONED_0 + handle_count));
        assert(!abandoned);
        if (succeeded) {
            DWORD object_index = wait_result - WAIT_OBJECT_0;
            if (object_index == 0) {
                request_missing_resources();
            } else if (object_index == 1) {
                break;
            } else {
                abort();
            }
        } else {
            assert(wait_result != WAIT_TIMEOUT);
            if (wait_result != WAIT_FAILED) {
                std::println("Unexpected wait result {}", wait_result);
            }
            THROW_WIN32(WaitForMultipleObjects);
        }
    }
}
#pragma endregion

}