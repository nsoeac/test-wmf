#include "decoder.hpp"

#include "renderer.hpp"

#include "lib/lib.hpp"

using Microsoft::WRL::ComPtr;

Decoder::Decoder(Config &config, Renderer *renderer) : net(config), renderer(renderer) {}

void Decoder::connect() {
    std::vector<uint8_t> initial_message = net.get_initial_message();

    std::println("Setting up initial state");

    std::span<int32_t> initial_values = { (int32_t *)(initial_message.data()), (int32_t *)(initial_message.data() + sizeof(int32_t) * 4) };
    video_width = initial_values[1];
    video_height = initial_values[2];
    video_framerate = initial_values[3];
}

void Decoder::create_texture() {
    if (output_sample != nullptr) {
        hresult(output_sample->RemoveBufferByIndex(0));
    }

    media_buffer = nullptr;
    hresult(MFCreateMemoryBuffer(output_stream_info.cbSize, &media_buffer));

    renderer->create_buffer(output_stream_info.cbSize);

    if (!output_sample) {
        hresult(MFCreateSample(&output_sample));
        data_buffer.pSample = output_sample.Get();
    }

    hresult(output_sample->AddBuffer(media_buffer.Get()));
}

void Decoder::init_decoder() {
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

    {
        ComPtr<IMFAttributes> attributes;
        hresult(decoder->GetAttributes(&attributes));
        hresult(attributes->SetUINT32(MF_LOW_LATENCY, TRUE));
    }

    hresult(decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0));

    sample_duration = 10'000'000 /* Hundred-nanoseconds in a second. */ / video_framerate;

    create_texture();
}

void Decoder::process_sample(ComPtr<IMFSample> sample) {
    hresult(decoder->ProcessInput(0, sample.Get(), 0));

    while (true) {
        DWORD status = 0;
        HRESULT result = decoder->ProcessOutput(0, 1, &data_buffer, &status);

        switch (result) {
        case MF_E_TRANSFORM_NEED_MORE_INPUT:
            std::println("Input drained");
            goto FINISHED;
        case MF_E_TRANSFORM_STREAM_CHANGE: {
            std::println("Handling stream change");

            assert(status & MFT_PROCESS_OUTPUT_STATUS_NEW_STREAMS);
            status &= ~MFT_PROCESS_OUTPUT_STATUS_NEW_STREAMS;

            assert(data_buffer.dwStatus & MFT_OUTPUT_DATA_BUFFER_FORMAT_CHANGE);
            data_buffer.dwStatus &= ~MFT_OUTPUT_DATA_BUFFER_FORMAT_CHANGE;

            assert(status == 0);
            assert(data_buffer.dwStatus == 0);
            assert(data_buffer.pEvents == nullptr);

            ComPtr<IMFMediaType> output_type;
            hresult(decoder->GetOutputAvailableType(0, 0, &output_type));
            hresult(decoder->SetOutputType(0, output_type.Get(), 0));

            hresult(decoder->GetOutputStreamInfo(0, &output_stream_info));

            hresult(MFGetAttributeSize(output_type.Get(), MF_MT_FRAME_SIZE, &video_width, &video_height));

            create_texture();

            std::println("Stream change handled");

            continue;
        }
        case S_OK: {
            std::println("Sample processed");

            D3D12_RANGE read_range = {};

            void *buffer_data = nullptr;
            hresult(renderer->packed_texture->Map(0, &read_range, &buffer_data));
            BYTE *media_data = nullptr;
            DWORD current_length = 0;
            DWORD max_length = 0;
            hresult(media_buffer->Lock(&media_data, &max_length, &current_length));
            std::memcpy(buffer_data, media_data, max_length);
            hresult(media_buffer->Unlock());
            D3D12_RANGE written_range = {};
            written_range.Begin = 0;
            written_range.End = max_length;
            renderer->packed_texture->Unmap(0, &written_range);

            renderer->render_frame();

            break;
        }
        default:
            hresult(result);
        }

        assert(status == 0);
        assert(data_buffer.dwStatus == 0);
        assert(data_buffer.pEvents == nullptr);
    }

FINISHED:;
}

void Decoder::process_frame(std::vector<uint8_t> &frame) {
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

void Decoder::handle_packet() {
    std::vector<uint8_t> frame = net.receive();
    process_frame(frame);
}

void Decoder::handle_packets() {
    while (true) {
        handle_packet();
    }
}

bool Decoder::is_parameter_sets(std::span<uint8_t> buffer) {
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

ComPtr<IMFSample> Decoder::create_sample(size_t size, int64_t sample_time, int64_t sample_duration) {
    ComPtr<IMFMediaBuffer> input_buffer;
    hresult(MFCreateMemoryBuffer((DWORD)size, &input_buffer));

    ComPtr<IMFSample> sample;
    hresult(MFCreateSample(&sample));
    hresult(sample->AddBuffer(input_buffer.Get()));

    hresult(sample->SetSampleTime(sample_time));
    hresult(sample->SetSampleDuration(sample_duration));

    return sample;
}

void Decoder::copy_payload_to_first_buffer(ComPtr<IMFSample> sample, std::span<uint8_t> payload) {
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

int64_t Decoder::get_sample_time(int64_t start_timestamp, int64_t end_timestamp) {
    int64_t sample_time = (end_timestamp - start_timestamp) / 100;
    return sample_time;
}

ComPtr<IMFTransform> Decoder::create_decoder() {
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