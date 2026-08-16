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

static constexpr std::string_view config_path = ".\\config.txt";
static constexpr int expected_config_line_count = 2;
static constexpr int packet_size = 1'384;
static constexpr int header_size = sizeof(Packet_Header);

static Config read_config() {
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

static bool is_parameter_sets(std::span<uint8_t> &buffer) {
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
}

// Timestamps are in nanoseconds, but the result is in hundreds of nanoseconds since the start timestamp.
static int64_t get_sample_time(int64_t start_timestamp, int64_t end_timestamp) {
    int64_t sample_time = (end_timestamp - start_timestamp) / 100;
    return sample_time;
}

int main() {
    std::println("Initialising COM");

    hresult(CoInitialize(NULL));

    std::println("Creating decoder");

    ComPtr<IMFTransform> decoder;

    {
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

        hresult(activates[0]->ActivateObject(IID_PPV_ARGS(&decoder)));

        // Clean up.

        for (UINT32 i = 0; i < activate_count; i++) {
            activates[i]->Release();
        }

        CoTaskMemFree(activates);
    }

    std::println("Starting WSA");

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

    std::vector<uint8_t> send_buffer(packet_size);
    WSABUF wsa_send_buffer = {};
    wsa_send_buffer.buf = (CHAR *)send_buffer.data();
    wsa_send_buffer.len = packet_size;
    DWORD bytes_sent = 0;
    if (WSASend(socket, &wsa_send_buffer, 1, &bytes_sent, 0, NULL, NULL) != 0) {
        THROW_WSA(WSASend);
    }

    std::vector<uint8_t> receive_buffer(packet_size);
    WSABUF wsa_receive_buffer = {};
    wsa_receive_buffer.buf = (CHAR *)receive_buffer.data();
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

    // Set input and output types.

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

    MFT_INPUT_STREAM_INFO input_stream_info = {};
    MFT_OUTPUT_STREAM_INFO output_stream_info = {};
    hresult(decoder->GetInputStreamInfo(0, &input_stream_info));
    hresult(decoder->GetOutputStreamInfo(0, &output_stream_info));

    // TODO: DO NOT FORGET THAT MFT_INPUT_STREAM_WHOLE_SAMPLES IS SET IN THIS CONFIGURATION SO IF DECODING DOESN'T WORK BREAK THE PARAMETER SETS INTO 3 PACKETS AND PASS THEM IN SEPARATELY.

    hresult(decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));

    // Create output sample.

    ComPtr<IMFMediaBuffer> output_buffer;
    hresult(MFCreateMemoryBuffer(output_stream_info.cbSize, &output_buffer));

    ComPtr<IMFSample> output_sample;
    hresult(MFCreateSample(&output_sample));
    hresult(output_sample->AddBuffer(output_buffer.Get()));

    MFT_OUTPUT_DATA_BUFFER output_sample_buffer = {};
    output_sample_buffer.dwStreamID = 0;
    output_sample_buffer.pSample = output_sample.Get();

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

    ComPtr<IMFSample> parameter_sets_sample;
    int64_t start_timestamp = INT64_MIN;
    int64_t sample_duration = 10'000'000 /* Hundred-nanoseconds in a second. */ / video_framerate;
    std::vector<ComPtr<IMFSample>> video_samples;
    while (true) {
        DWORD flags = 0;
        if (WSARecv(socket, &wsa_receive_buffer, 1, &bytes_received, &flags, NULL, NULL) != 0) {
            THROW_WSA(WSARecv);
        }

        Packet_Header &header = *(Packet_Header *)receive_buffer.data();
        assert(header.format_index >= 0);

        std::span<uint8_t> payload = { receive_buffer.begin() + header_size, receive_buffer.begin() + bytes_received };
        bool has_parameter_sets = is_parameter_sets(payload);

        std::println("Received {} bytes: {{ frame_index: {}, packet_index: {}, packet_count: {}, format_index: {}: timestamp: {} }}", bytes_received, header.frame_index, header.packet_index, header.packet_count, header.format_index, header.timestamp);

        if (has_parameter_sets) {

            assert(parameter_sets_sample == nullptr);

            if (start_timestamp == INT64_MIN) {
                start_timestamp = header.timestamp;
            }

            int64_t sample_time = get_sample_time(start_timestamp, header.timestamp);
            std::println("Adding parameter sets with timestamp {}", sample_time);
            parameter_sets_sample = create_sample(payload.size(), sample_time, 0);
            copy_payload_to_first_buffer(parameter_sets_sample, payload);

            hresult(decoder->ProcessInput(0, parameter_sets_sample.Get(), 0));

            std::println("Parameter sets sample processed");

            DWORD output_status = 0;
            hresult(decoder->GetOutputStatus(&output_status));

            if (output_status & MFT_OUTPUT_STATUS_SAMPLE_READY) {
                std::println("Can process output after parameter sets");

                DWORD status = 0;
                decoder->ProcessOutput(0, 1, &output_sample_buffer, &status);

                if (output_sample_buffer.pEvents != nullptr) {
                    output_sample_buffer.pEvents->Release();
                    output_sample_buffer.pEvents = nullptr;
                }

                output_sample_buffer.dwStatus = 0;
            }
        } else {
            int64_t sample_time = get_sample_time(start_timestamp, header.timestamp);
            std::println("Adding video sample with timestamp {}", sample_time);
            ComPtr<IMFSample> sample = create_sample(payload.size(), sample_time, sample_duration);
            copy_payload_to_first_buffer(sample, payload);

            video_samples.push_back(sample);
        }

        if (parameter_sets_sample != nullptr) {
            for (ComPtr<IMFSample> sample : video_samples) {
                hresult(decoder->ProcessInput(0, sample.Get(), 0));

                DWORD output_status = 0;
                hresult(decoder->GetOutputStatus(&output_status));

                if (output_status & MFT_OUTPUT_STATUS_SAMPLE_READY) {
                    std::println("Sample ready; processing output");

                    DWORD status = 0;
                    HRESULT result = decoder->ProcessOutput(0, 1, &output_sample_buffer, &status);
                    assert(status == 0);

                    if (result == MF_E_TRANSFORM_NEED_MORE_INPUT) {
                        std::println("Need more input");
                    } else if (result != S_OK) {
                        hresult(result);
                    } else {
                        std::println("Video sample processed");
                    }

                    if (output_sample_buffer.pEvents != nullptr) {
                        output_sample_buffer.pEvents->Release();
                        output_sample_buffer.pEvents = nullptr;
                    }

                    output_sample_buffer.dwStatus = 0;
                } else {
                    std::println("Sample not ready after video frame");
                }

                sample = nullptr;
            }

            video_samples.clear();
        } else {
            std::println("Waiting for parameter sets");
        }
    }
}