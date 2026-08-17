#include "lib/lib.hpp"

using Microsoft::WRL::ComPtr;

struct Config {
    std::wstring port;
    std::wstring address;
};

static constexpr std::string_view config_path = ".\\config.txt";
static constexpr int expected_config_line_count = 2;

static Config read_config() {
    std::println("Reading config");

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

static WSABUF get_wsabuf(std::span<uint8_t> span) {
    WSABUF wsabuf = {};
    wsabuf.buf = (CHAR *)span.data();
    wsabuf.len = (ULONG)span.size();
    return wsabuf;
}

struct Networking {
private:
    struct Header {
        int64_t message_index;
        int64_t packet_index;
        int64_t packet_count;
        int64_t placeholder;
    };

    static constexpr int packet_size = 1'384;
    static constexpr int header_size = sizeof(Header);
    static constexpr int payload_size = packet_size - header_size;
    static constexpr bool print_debug_messages = false;

    struct Packet {
        std::vector<uint8_t> buffer = std::vector<uint8_t>(packet_size);

        Header header() const {
            assert(buffer.size() >= header_size);

            Header header;
            std::memcpy(&header, buffer.data(), sizeof(Header));

            return header;
        }

        int64_t packet_index() const {
            assert(buffer.size() >= header_size);

            int64_t packet_index = 0;
            std::memcpy(&packet_index, buffer.data() + offsetof(Header, packet_index), sizeof(packet_index));

            return packet_index;
        }
    };

    struct Message {
        std::vector<Packet> packets;
        int64_t message_index = 0;
        int64_t packet_count = 0;

        bool is_complete() const {
            return packet_count == (int64_t)packets.size();
        }
    };

    std::vector<Message> messages;

    [[nodiscard]] std::vector<uint8_t> remove_message_and_get_payload(Message &message) {
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
    [[nodiscard]] std::optional<std::vector<uint8_t>> add_packet(Packet &&packet) {
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

    SOCKET socket = INVALID_SOCKET;

    std::vector<uint8_t> send_buffer = std::vector<uint8_t>(packet_size);
    WSABUF wsa_send_buffer = {};
    DWORD bytes_sent = 0;

    std::wstring address;
    std::wstring port;

    bool ready_packet_sent = false;
public:
    Networking(Config &config) :
        address(config.address),
        port(config.port) {
        wsa_send_buffer.buf = (CHAR *)send_buffer.data();
        wsa_send_buffer.len = (ULONG)send_buffer.size();
    }

    std::vector<uint8_t> get_initial_message() {
        if (print_debug_messages) {
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

        socket = WSASocketW(hints.ai_family, hints.ai_socktype, hints.ai_protocol, NULL, 0, 0);
        if (socket == INVALID_SOCKET) {
            THROW_WSA(WSASocketW);
        }

        if (WSAConnect(socket, send_info->ai_addr, sizeof(sockaddr_in), NULL, NULL, NULL, NULL) == SOCKET_ERROR) {
            THROW_WSA(WSAConnect);
        }

        if (print_debug_messages) {
            std::println("Greeting server");
        }

        if (WSASend(socket, &wsa_send_buffer, 1, &bytes_sent, 0, NULL, NULL) != 0) {
            THROW_WSA(WSASend);
        }

        if (print_debug_messages) {
            std::println("Waiting for server response");
        }

        sockaddr server_address = {};
        INT server_address_length = sizeof(server_address);
        DWORD receive_flags = 0;
        std::vector<uint8_t> receive_buffer(packet_size);
        DWORD bytes_received = 0;
        WSABUF wsa_receive_buffer = get_wsabuf(receive_buffer);
        if (WSARecvFrom(socket, &wsa_receive_buffer, 1, &bytes_received, &receive_flags, &server_address, &server_address_length, NULL, NULL) != 0) {
            THROW_WSA(WSARecvFrom);
        }

        if (bytes_received < 4) {
            throw std::runtime_error("Expected 4 bytes for the server receive port");
        }

        std::vector<uint8_t> initial_message = { receive_buffer.begin(), receive_buffer.begin() + bytes_received };

        assert(initial_message.size() >= (sizeof(int32_t) * 4));
        std::span<int32_t> initial_values = { (int32_t *)(initial_message.data()), (int32_t *)(initial_message.data() + sizeof(int32_t) * 4) };

        uint16_t server_port = (uint16_t)initial_values[0];
        if (server_port == 0) {
            throw std::runtime_error(std::format("Server receive port is {}", server_port));
        }

        ((sockaddr_in *)&server_address)->sin_port = htons(server_port);

        if (print_debug_messages) {
            std::println("Received {} bytes; server port is {}", bytes_received, server_port);
        }

        if (WSAConnect(socket, &server_address, sizeof(sockaddr_in), NULL, NULL, NULL, NULL) == SOCKET_ERROR) {
            THROW_WSA(WSAConnect);
        }

        return initial_message;
    }

    [[nodiscard]] std::vector<uint8_t> receive() {
        if (!ready_packet_sent) {
            if (print_debug_messages) {
                std::println("Sending 'ready' packet to server");
            }

            if (WSASend(socket, &wsa_send_buffer, 1, &bytes_sent, 0, NULL, NULL) != 0) {
                THROW_WSA(WSASend);
            }

            ready_packet_sent = true;
        }

        if (print_debug_messages) {
            std::println("Receiving packets from server");
        }

        std::vector<uint8_t> buffer;

        {
            std::optional<std::vector<uint8_t>> message_buffer_option = std::nullopt;

            while (message_buffer_option == std::nullopt) {
                Packet packet;
                WSABUF packet_wsabuf = get_wsabuf(packet.buffer);

                DWORD bytes_received = 0;
                DWORD flags = 0;
                if (WSARecv(socket, &packet_wsabuf, 1, &bytes_received, &flags, NULL, NULL) != 0) {
                    THROW_WSA(WSARecv);
                }

                packet.buffer.resize(bytes_received);
                message_buffer_option = add_packet(std::move(packet));
            }

            buffer = std::move(*message_buffer_option);
        }

        if (print_debug_messages) {
            std::println("Received {} byte(s) from server ({} messages remain)", buffer.size(), messages.size());
        }

        return buffer;
    }
};

static bool is_parameter_sets(std::span<uint8_t> buffer) {
    Bitstream_Reader reader(buffer);
    assert(reader.can_read_bytes(6));

    uint32_t four_byte_sequence = (uint32_t)reader.read_bytes(4);
    if (four_byte_sequence != 0x00'00'00'01) {
        return false;
    }

    uint8_t forbidden_zero_bit = (uint8_t)reader.read_bits(1);
    assert(forbidden_zero_bit == 0);

    uint8_t nal_unit_type = (uint8_t)reader.read_bits(6);

    bool is_header_type = (nal_unit_type == 32) || (nal_unit_type == 33) || (nal_unit_type == 34);
    return is_header_type;
}

static ComPtr<IMFSample> create_sample(size_t size, int64_t sample_time, int64_t sample_duration) {
    ComPtr<IMFMediaBuffer> input_buffer;
    hresult(MFCreateMemoryBuffer((DWORD)size, &input_buffer));

    ComPtr<IMFSample> sample;
    hresult(MFCreateSample(&sample));
    hresult(sample->AddBuffer(input_buffer.Get()));

    hresult(sample->SetSampleTime(sample_time));
    hresult(sample->SetSampleDuration(sample_duration));

    return sample;
}

static void copy_payload_to_first_buffer(ComPtr<IMFSample> sample, std::span<uint8_t> payload) {
    ComPtr<IMFMediaBuffer> media_buffer;
    hresult(sample->GetBufferByIndex(0, &media_buffer));
    BYTE *buffer = nullptr;
    DWORD size = 0;
    hresult(media_buffer->Lock(&buffer, &size, nullptr));
    assert(size >= (DWORD)payload.size());
    std::memcpy(buffer, payload.data(), payload.size());
    hresult(media_buffer->Unlock());

    hresult(media_buffer->SetCurrentLength((DWORD)payload.size()));
}

// Timestamps are in nanoseconds, but the result is in hundreds of nanoseconds since the start timestamp.
static int64_t get_sample_time(int64_t start_timestamp, int64_t end_timestamp) {
    int64_t sample_time = (end_timestamp - start_timestamp) / 100;
    return sample_time;
}

static ComPtr<IMFTransform> create_decoder() {
    std::println("Creating decoder");

    MFT_REGISTER_TYPE_INFO input = {};
    input.guidMajorType = MFMediaType_Video;
    input.guidSubtype = MFVideoFormat_HEVC;
    MFT_REGISTER_TYPE_INFO output = {};
    output.guidMajorType = MFMediaType_Video;
    output.guidSubtype = MFVideoFormat_NV12;
    IMFActivate **activates = nullptr;
    UINT32 activate_count = 0;
    hresult(MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER, MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_SORTANDFILTER, &input, &output, &activates, &activate_count));

    if (activate_count == 0) {
        throw std::runtime_error("Could not find any video decoders");
    }

    ComPtr<IMFTransform> decoder;
    hresult(activates[0]->ActivateObject(IID_PPV_ARGS(&decoder)));

    // Clean up.

    for (UINT32 i = 0; i < activate_count; i++) {
        activates[i]->Release();
    }

    CoTaskMemFree(activates);
    return decoder;
}

struct Output_Sample {
    ComPtr<IMFSample> sample;
    ComPtr<IMFMediaBuffer> media_buffer;
    MFT_OUTPUT_DATA_BUFFER data_buffer = {};
};

struct App {
    struct Frame_Header {
        int32_t frame_index;
        int32_t format_index;
        int64_t timestamp;
    };

    ComPtr<IMFTransform> decoder = create_decoder();
    std::vector<Output_Sample> output_samples;
    MFT_INPUT_STREAM_INFO input_stream_info = {};
    MFT_OUTPUT_STREAM_INFO output_stream_info = {};

    int video_width = -1;
    int video_height = -1;
    int video_framerate = -1;
    int64_t start_timestamp = INT64_MIN;
    int64_t sample_duration = INT64_MIN;
    bool has_parameter_sets = false;

    Networking net;

    App(Config &config) : net(config) {}

    Output_Sample create_output_sample() {
        Output_Sample output_sample;
        hresult(MFCreateMemoryBuffer(output_stream_info.cbSize, &output_sample.media_buffer));

        hresult(MFCreateSample(&output_sample.sample));
        hresult(output_sample.sample->AddBuffer(output_sample.media_buffer.Get()));

        output_sample.data_buffer.dwStreamID = 0;
        output_sample.data_buffer.pSample = output_sample.sample.Get();

        return output_sample;
    }

    void connect() {
        std::vector<uint8_t> initial_message = net.get_initial_message();

        std::println("Setting up initial state");

        std::span<int32_t> initial_values = { (int32_t *)(initial_message.data()), (int32_t *)(initial_message.data() + sizeof(int32_t) * 4) };
        video_width = initial_values[1];
        video_height = initial_values[2];
        video_framerate = initial_values[3];
    }

    void init_decoder() {
        std::println("Setting decoder input type");

        for (DWORD i = 0;; i++) {
            ComPtr<IMFMediaType> input;
            hresult(decoder->GetInputAvailableType(0, i, &input));

            GUID major_type = {};
            hresult(input->GetMajorType(&major_type));
            GUID subtype = {};
            hresult(input->GetGUID(MF_MT_SUBTYPE, &subtype));
            if ((major_type == MFMediaType_Video) && (subtype == MFVideoFormat_HEVC)) {
                hresult(MFSetAttributeSize(input.Get(), MF_MT_FRAME_SIZE, video_width, video_height));
                hresult(decoder->SetInputType(0, input.Get(), 0));
                std::println("Input type set");
                break;
            }
        }

        std::println("Setting decoder output type");

        for (DWORD i = 0;; i++) {
            ComPtr<IMFMediaType> output;
            hresult(decoder->GetOutputAvailableType(0, i, &output));

            GUID major_type = {};
            hresult(output->GetMajorType(&major_type));
            GUID subtype = {};
            output->GetGUID(MF_MT_SUBTYPE, &subtype);
            if ((major_type == MFMediaType_Video) && (subtype == MFVideoFormat_NV12)) {
                hresult(decoder->SetOutputType(0, output.Get(), 0));
                break;
            }
        }

        std::println("Initialising decoder resources");

        hresult(decoder->GetInputStreamInfo(0, &input_stream_info));
        hresult(decoder->GetOutputStreamInfo(0, &output_stream_info));

        hresult(decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));

        sample_duration = 10'000'000 /* Hundred-nanoseconds in a second. */ / video_framerate;
    }

    void process_sample(ComPtr<IMFSample> sample) {
        hresult(decoder->ProcessInput(0, sample.Get(), 0));

        while (true) {
            Output_Sample output_sample = create_output_sample();
            DWORD status = 0;
            HRESULT result = decoder->ProcessOutput(0, 1, &output_sample.data_buffer, &status);

            switch (result) {
            case MF_E_TRANSFORM_NEED_MORE_INPUT:
                std::println("Input drained");
                goto FINISHED;
            case MF_E_TRANSFORM_STREAM_CHANGE: {
                std::println("Handling stream change");

                assert(status & MFT_PROCESS_OUTPUT_STATUS_NEW_STREAMS);
                status &= ~MFT_PROCESS_OUTPUT_STATUS_NEW_STREAMS;

                assert(output_sample.data_buffer.dwStatus & MFT_OUTPUT_DATA_BUFFER_FORMAT_CHANGE);
                output_sample.data_buffer.dwStatus &= ~MFT_OUTPUT_DATA_BUFFER_FORMAT_CHANGE;

                ComPtr<IMFMediaType> output_type;
                hresult(decoder->GetOutputAvailableType(0, 0, &output_type));
                hresult(decoder->SetOutputType(0, output_type.Get(), 0));

                hresult(decoder->GetOutputStreamInfo(0, &output_stream_info));

                std::println("Stream change handled");

                continue;
            }
            case S_OK:
                std::println("Sample processed");
                break;
            default:
                hresult(result);
            }

            assert(status == 0);
            assert(output_sample.data_buffer.dwStatus == 0);
            assert(output_sample.data_buffer.pEvents == nullptr);

            output_samples.push_back(output_sample);
        }

    FINISHED:;
    }

    void process_frame(std::vector<uint8_t> &frame) {
        Frame_Header header = {};
        assert(frame.size() >= sizeof(Frame_Header));
        std::memcpy(&header, frame.data(), sizeof(Frame_Header));
        std::span<uint8_t> payload = { frame.begin() + sizeof(Frame_Header), frame.end() };

        std::println("Processing frame {} ({} byte(s))", header.frame_index, payload.size());

        if (is_parameter_sets(payload)) {
            ComPtr<IMFSample> parameter_sets_sample;

            if (start_timestamp == INT64_MIN) {
                start_timestamp = header.timestamp;
            }

            int64_t sample_time = get_sample_time(start_timestamp, header.timestamp);
            std::println("Adding parameter sets with timestamp {}", sample_time);
            parameter_sets_sample = create_sample(payload.size(), sample_time, 0);
            copy_payload_to_first_buffer(parameter_sets_sample, payload);

            process_sample(parameter_sets_sample);

            has_parameter_sets = true;
            return;
        }

        if (!has_parameter_sets) {
            std::println("Can't process video frame without parameter sets");

            abort();
        }

        int64_t sample_time = get_sample_time(start_timestamp, header.timestamp);
        ComPtr<IMFSample> sample = create_sample(payload.size(), sample_time, sample_duration);
        copy_payload_to_first_buffer(sample, payload);

        std::println("Adding video sample with timestamp {}", sample_time);

        process_sample(sample);

        sample = nullptr;
    }

    void handle_packets() {
        while (true) {
            std::vector<uint8_t> frame = net.receive();
            process_frame(frame);
        }
    }
};

int main() {
    std::println("Initialising COM");

    hresult(CoInitialize(NULL));

    std::println("Initialising WSA");

    {
        WSADATA wsa_data = {};
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            THROW_WSA(WSAStartup);
        }
    }

    Config config = read_config();

    App app(config);
    app.connect();
    app.init_decoder();
    app.handle_packets();
}