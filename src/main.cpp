#include "lib/lib.hpp"

using Microsoft::WRL::ComPtr;

struct Config {
    std::wstring port;
    std::wstring address;
};

struct Packet_Header {
    int32_t frame_index;
    int32_t packet_index;
    int32_t packet_count;
    int32_t format_index;
    int64_t timestamp;
};

struct Frame {
    int32_t frame_index;
    int32_t packet_count;
    int32_t format_index;
    int64_t timestamp;
    int32_t next_packet_index = 0;
    std::vector<uint8_t> buffer;
};

struct Packet {
    Packet_Header header;
    std::vector<uint8_t> buffer;
    std::span<uint8_t> payload;
};

static constexpr std::string_view config_path = ".\\config.txt";
static constexpr int expected_config_line_count = 2;
static constexpr int packet_size = 1'384;
static constexpr int header_size = sizeof(Packet_Header);

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

struct Networking {
    SOCKET socket = INVALID_SOCKET;

    std::vector<uint8_t> receive_buffer = std::vector<uint8_t>(packet_size);
    WSABUF wsa_receive_buffer = {};
    DWORD bytes_received = 0;

    std::vector<uint8_t> send_buffer = std::vector<uint8_t>(packet_size);
    WSABUF wsa_send_buffer = {};
    DWORD bytes_sent = 0;

    std::wstring address;
    std::wstring port;

    bool ready_packet_sent = false;

    Networking(Config &config) :
        address(config.address),
        port(config.port) {
        wsa_receive_buffer.buf = (CHAR *)receive_buffer.data();
        wsa_receive_buffer.len = (ULONG)receive_buffer.size();
        wsa_send_buffer.buf = (CHAR *)send_buffer.data();
        wsa_send_buffer.len = (ULONG)send_buffer.size();
    }

    std::vector<uint8_t> get_initial_message() {
        std::println("Resolving server address");

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

        std::println("Greeting server");

        if (WSASend(socket, &wsa_send_buffer, 1, &bytes_sent, 0, NULL, NULL) != 0) {
            THROW_WSA(WSASend);
        }

        std::println("Waiting for server response");

        sockaddr server_address = {};
        INT server_address_length = sizeof(server_address);
        DWORD receive_flags = 0;
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
        std::println("Received {} bytes; server port is {}", bytes_received, server_port);

        if (WSAConnect(socket, &server_address, sizeof(sockaddr_in), NULL, NULL, NULL, NULL) == SOCKET_ERROR) {
            THROW_WSA(WSAConnect);
        }

        return initial_message;
    }

    [[nodiscard]] std::vector<uint8_t> receive() {
        if (!ready_packet_sent) {
            std::println("Sending 'ready' packet to server");

            if (WSASend(socket, &wsa_send_buffer, 1, &bytes_sent, 0, NULL, NULL) != 0) {
                THROW_WSA(WSASend);
            }

            ready_packet_sent = true;
        }

        std::vector<uint8_t> result(packet_size);

        {
            std::println("Waiting to receive from server");

            DWORD flags = 0;
            if (WSARecv(socket, &wsa_receive_buffer, 1, &bytes_received, &flags, NULL, NULL) != 0) {
                THROW_WSA(WSARecv);
            }

            std::println("Received {} bytes from server", bytes_received);
        }

        std::memcpy(result.data(), receive_buffer.data(), bytes_received);
        result.resize(bytes_received);

        return result;
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
    ComPtr<IMFTransform> decoder = create_decoder();
    ComPtr<IMFSample> parameter_sets_sample;
    std::vector<Output_Sample> output_samples;
    MFT_INPUT_STREAM_INFO input_stream_info = {};
    MFT_OUTPUT_STREAM_INFO output_stream_info = {};

    int video_width = -1;
    int video_height = -1;
    int video_framerate = -1;
    int64_t start_timestamp = INT64_MIN;
    int64_t sample_duration = INT64_MIN;

    std::vector<Frame> frames;
    std::vector<Frame> completed_frames;
    std::vector<Packet> packets;

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
        std::println("Processing input");

        hresult(decoder->ProcessInput(0, sample.Get(), 0));

        std::println("Processing output");

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

    void process_frame(Frame &frame) {
        std::println("Processing completed frame {} ({} byte(s)); {} packet(s) remain", frame.frame_index, frame.buffer.size(), packets.size());

        bool has_parameter_sets = is_parameter_sets(frame.buffer);
        if (has_parameter_sets) {
            assert(parameter_sets_sample == nullptr);

            if (start_timestamp == INT64_MIN) {
                start_timestamp = frame.timestamp;
            }

            int64_t sample_time = get_sample_time(start_timestamp, frame.timestamp);
            std::println("Adding parameter sets with timestamp {}", sample_time);
            parameter_sets_sample = create_sample(frame.buffer.size(), sample_time, 0);
            copy_payload_to_first_buffer(parameter_sets_sample, frame.buffer);

            process_sample(parameter_sets_sample);

            return;
        }

        if (!parameter_sets_sample) {
            std::println("Can't process video frame without parameter sets");

            abort();
        }

        int64_t sample_time = get_sample_time(start_timestamp, frame.timestamp);
        ComPtr<IMFSample> sample = create_sample(frame.buffer.size(), sample_time, sample_duration);
        copy_payload_to_first_buffer(sample, frame.buffer);

        std::println("Adding video sample with timestamp {}", sample_time);

        process_sample(sample);

        sample = nullptr;
    }

    void fill_completed_frame_buffer(Frame &frame) {
        std::ranges::sort(packets, [](Packet &first, Packet &second) {
            if (first.header.frame_index < second.header.frame_index) {
                return true;
            } else if (second.header.frame_index < first.header.frame_index) {
                return false;
            } else if (first.header.packet_index < second.header.packet_index) {
                return true;
            } else if (second.header.packet_index < first.header.packet_index) {
                return false;
            } else {
                abort(); // Not expecting duplicate frames.
            }
        });

        std::vector<Packet>::iterator start_it = std::ranges::find_if(packets, [&frame](Packet &packet) { return packet.header.frame_index == frame.frame_index; });
        std::vector<Packet>::iterator end_it = start_it + frame.packet_count;

        size_t frame_size = 0;
        for (auto it = start_it; it < end_it; ++it) {
            Packet &packet = *it;
            frame_size += packet.payload.size();
        }

        frame.buffer.resize(frame_size);

        // Copy contents.

        auto copy_dest = frame.buffer.begin();
        size_t expected_packet_index = 0;
        for (auto it = start_it; it < end_it; ++it) {
            Packet &packet = *it;

            assert(packet.header.packet_index == expected_packet_index);

            std::ranges::copy(packet.payload, copy_dest);
            copy_dest += packet.payload.size();

            expected_packet_index++;
        }

        assert(copy_dest == frame.buffer.end());

        // Remove packets.

        auto remove_range = std::ranges::remove_if(packets, [&frame](Packet &packet) { return packet.header.frame_index == frame.frame_index; });
        packets.erase(remove_range.begin(), remove_range.end());
    }

    void receive_packets() {
        while (true) {
            // Get the packet.

            {
                Packet packet;
                packet.buffer = net.receive();
                assert(packet.buffer.size() >= header_size);
                packet.payload = { packet.buffer.begin() + header_size, packet.buffer.end() };
                packet.header = *(Packet_Header *)packet.buffer.data();
                packets.push_back(std::move(packet));
            }

            // Check if the packet completes a frame.

            {
                Packet &packet = packets.back();
                std::vector<Frame>::iterator frame_it = std::ranges::find_if(frames, [&packet](int32_t frame_index) { return packet.header.frame_index == frame_index; }, [this](Frame &frame) { return frame.frame_index; });

                // Add frame if it doesn't exist.

                if (frame_it == frames.end()) {
                    Frame new_frame;
                    new_frame.frame_index = packet.header.frame_index;
                    new_frame.packet_count = packet.header.packet_count;
                    new_frame.format_index = packet.header.format_index;
                    new_frame.timestamp = packet.header.timestamp;
                    frames.push_back(new_frame);
                    frame_it = std::prev(frames.end());
                }

                Frame &frame = *frame_it;

                if (packet.header.packet_index == frame.next_packet_index) {
                    frame.next_packet_index++;
                }

                if (frame.next_packet_index == frame.packet_count) {
                    std::swap(frame, frames.back());
                    Frame completed_frame = std::move(frame);
                    frames.pop_back();

                    fill_completed_frame_buffer(completed_frame);
                    process_frame(completed_frame);
                }
            }
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
    app.receive_packets();
}