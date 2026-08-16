#include "lib/lib.hpp"

static constexpr std::string_view config_path = ".\\config.txt";
static constexpr int expected_config_line_count = 2;
static constexpr int packet_size = 1'384;

struct Config {
    std::wstring port;
    std::wstring address;
};

Config read_config() {
    std::ifstream input_stream(config_path.data(), std::ios::in | std::ios::binary | std::ios::ate);

    if (!input_stream) {
        throw std::runtime_error("Failed to open config file");
    }

    auto size = input_stream.tellg();
    if (size == -1) {
        throw std::runtime_error("Failed to read size of config file");
    }

    input_stream.seekg(0);

    std::vector<char> buffer(size);
    input_stream.read(buffer.data(), size);

    input_stream.close();

    std::vector<std::span<char>> lines = read_lines(buffer);

    if (lines.size() != expected_config_line_count) {
        throw std::runtime_error(std::format("Expected {} config lines; got {} lines", expected_config_line_count, lines.size()));
    }

    Config config;
    config.port = convert(std::string(lines[0].begin(), lines[0].end()));
    config.address = convert(std::string(lines[1].begin(), lines[1].end()));
    return config;
}

struct Packet_Header {
    int32_t frame_index;
    int32_t packet_index;
    int32_t packet_count;
    int32_t format_index;
};

int main() {
    WSADATA wsa_data = {};
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        THROW_WSA(WSAStartup);
    }

    Config config = read_config();

    ADDRINFOW hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    PADDRINFOW send_info = NULL;
    if (GetAddrInfoW(config.address.data(), config.port.data(), &hints, &send_info) != 0) {
        THROW_WSA(GetAddrInfoW);
    }

    SOCKET socket = WSASocketW(hints.ai_family, hints.ai_socktype, hints.ai_protocol, NULL, 0, 0);
    if (socket == INVALID_SOCKET) {
        THROW_WSA(WSASocketW);
    }

    if (WSAConnect(socket, send_info->ai_addr, sizeof(sockaddr_in), NULL, NULL, NULL, NULL) == SOCKET_ERROR) {
        THROW_WSA(WSAConnect);
    }

    std::vector<char> send_buffer(packet_size);
    WSABUF wsa_send_buffer = {};
    wsa_send_buffer.buf = send_buffer.data();
    wsa_send_buffer.len = packet_size;
    DWORD bytes_sent = 0;
    if (WSASend(socket, &wsa_send_buffer, 1, &bytes_sent, 0, NULL, NULL) != 0) {
        THROW_WSA(WSASend);
    }

    std::vector<char> receive_buffer(packet_size);
    WSABUF wsa_receive_buffer = {};
    wsa_receive_buffer.buf = receive_buffer.data();
    wsa_receive_buffer.len = packet_size;
    DWORD bytes_received = 0;
    sockaddr server_address = {};
    INT server_address_length = sizeof(server_address);
    DWORD receive_flags = 0;
    if (WSARecvFrom(socket, &wsa_receive_buffer, 1, &bytes_received, &receive_flags, &server_address, &server_address_length, NULL, NULL) != 0) {
        THROW_WSA(WSARecvFrom);
    }

    if (bytes_received < 4) {
        throw std::runtime_error("Expected 4 bytes for the server receive port");
    }

    std::span<int32_t> initial_values = { (int32_t *)(receive_buffer.data()), (int32_t *)(receive_buffer.data() + sizeof(int32_t) * 4) };

    uint16_t server_port = (uint16_t)initial_values[0];
    if (server_port == 0) {
        throw std::runtime_error(std::format("Server receive port is {}", server_port));
    }

    int video_width = initial_values[1];
    int video_height = initial_values[2];
    int video_framerate = initial_values[3];
    std::ignore = video_width;
    std::ignore = video_height;
    std::ignore = video_framerate;

    ((sockaddr_in *)&server_address)->sin_port = htons(server_port);
    std::println("Received {} bytes; server port is {}", bytes_received, server_port);

    if (WSAConnect(socket, &server_address, sizeof(sockaddr_in), NULL, NULL, NULL, NULL) == SOCKET_ERROR) {
        THROW_WSA(WSAConnect);
    }

    std::println("Connected to server", bytes_received, server_port);

    if (WSASend(socket, &wsa_send_buffer, 1, &bytes_sent, 0, NULL, NULL) != 0) {
        THROW_WSA(WSASend);
    }

    std::println("Sent first packet to server", bytes_received, server_port);

    while (true) {
        DWORD flags = 0;
        if (WSARecv(socket, &wsa_receive_buffer, 1, &bytes_received, &flags, NULL, NULL) != 0) {
            THROW_WSA(WSARecv);
        }

        Packet_Header &packet_header = *(Packet_Header *)receive_buffer.data();
        assert(packet_header.format_index >= 0);

        std::println("Received {} bytes: {{ frame_index: {}, packet_index: {}, packet_count: {}, format_index: {} }}", bytes_received, packet_header.frame_index, packet_header.packet_index, packet_header.packet_count, packet_header.format_index);
    }
}