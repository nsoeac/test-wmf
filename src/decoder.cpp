#include "decoder.hpp"

#include "lib/lib.hpp"

using Microsoft::WRL::ComPtr;

Decoder::Decoder(Config &config) : net(config) {}

void Decoder::connect() {
    std::vector<uint8_t> initial_message = net.get_initial_message();

    std::println("Setting up initial state");

    std::span<int32_t> initial_values = { (int32_t *)(initial_message.data()), (int32_t *)(initial_message.data() + sizeof(int32_t) * 4) };
    video_width = initial_values[1];
    video_height = initial_values[2];
    video_framerate = initial_values[3];
}

void Decoder::create_texture() {
    if (texture_handle != INVALID_HANDLE_VALUE) {
        if (create_shared_texture) {
            std::println("Destroying old texture");

            d12_texture = nullptr;

            if (CloseHandle(texture_handle) == 0) {
                THROW_WIN32(CloseHandle);
            }

            texture_handle = INVALID_HANDLE_VALUE;

            if (texture_mutex) {
                hresult(texture_mutex->ReleaseSync(0));
            }
        }

        d11_texture = nullptr;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = video_width;
    desc.Height = video_height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    if (create_shared_texture) {
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    }

    hresult(d11_device->CreateTexture2D(&desc, nullptr, &d11_texture));

    if (create_shared_texture) {
        hresult(d11_texture.As(&shared_texture));
        hresult(shared_texture->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &texture_handle));
        hresult(d12_device->OpenSharedHandle(texture_handle, IID_PPV_ARGS(&d12_texture)));
        hresult(d11_texture.As(&texture_mutex));
    }

    hresult(MFCreateSample(&output_sample));
    hresult(MFCreateDXGISurfaceBuffer(IID_ID3D11Texture2D, d11_texture.Get(), 0, FALSE, &media_buffer));

    data_buffer.dwStreamID = 0;
    data_buffer.pSample = output_sample.Get();
    hresult(output_sample->AddBuffer(media_buffer.Get()));

    if (create_shared_texture) {
        std::println("Acquiring texture mutex");

        hresult(texture_mutex->AcquireSync(0, INFINITE));
    }
}

void Decoder::init_decoder() {
    std::println("Initialising rendering resources");

    {
        ComPtr<IDXGIFactory4> factory;
        {
            UINT factory_flags = 0;
#ifdef _DEBUG
            {
                ComPtr<ID3D12Debug> debug_controller;
                hresult(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)));
                debug_controller->EnableDebugLayer();
                factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
            }
#endif
            hresult(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&factory)));
        }

        ComPtr<IDXGIAdapter1> adapter;
        for (UINT adapter_index = 0;; adapter_index++) {
            HRESULT result = factory->EnumAdapters1(adapter_index, &adapter);
            if (SUCCEEDED(result)) {
                break;
            } else if (result != DXGI_ERROR_INVALID_CALL) {
                hresult(result);
            }
        }

        UINT reset_token = 0;

        D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
        hresult(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT, &feature_level, 1, D3D11_SDK_VERSION, &d11_device, nullptr, &d11_context));
        hresult(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d12_device)));
        hresult(MFCreateDXGIDeviceManager(&reset_token, &device_manager));
        hresult(device_manager->ResetDevice(d11_device.Get(), reset_token));
    }

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

    std::println("Checking that the decoder is D3D11-aware");

    {
        ComPtr<IMFAttributes> decoder_attributes;
        hresult(decoder->GetAttributes(&decoder_attributes));
        BOOL is_decoder_d3d11_aware = (BOOL)MFGetAttributeUINT32(decoder_attributes.Get(), MF_SA_D3D11_AWARE, (UINT32)-1);
        assert(is_decoder_d3d11_aware != -1);
        if (is_decoder_d3d11_aware == FALSE) {
            throw std::runtime_error("Decoder is not D3D11-aware");
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

            ComPtr<IMFMediaType> output_type;
            hresult(decoder->GetOutputAvailableType(0, 0, &output_type));
            hresult(decoder->SetOutputType(0, output_type.Get(), 0));

            hresult(decoder->GetOutputStreamInfo(0, &output_stream_info));

            create_texture();

            std::println("Stream change handled");

            continue;
        }
        case S_OK: {
            std::println("Sample processed");

            ComPtr<IMFDXGIBuffer> dxgi_buffer;
            hresult(media_buffer.As(&dxgi_buffer));

            std::println("Got DXGI buffer");

            ComPtr<ID3D11Texture2D> sample_texture;
            hresult(dxgi_buffer->GetResource(IID_PPV_ARGS(&sample_texture)));

            d11_context->CopyResource(d11_texture.Get(), sample_texture.Get());

            std::println("Texture copied");

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

void Decoder::handle_packets() {
    while (true) {
        std::vector<uint8_t> frame = net.receive();
        process_frame(frame);
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