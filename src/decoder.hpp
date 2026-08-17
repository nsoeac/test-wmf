#pragma once

#include "networking.hpp"

struct Decoder {
private:
    struct Frame_Header {
        int32_t frame_index;
        int32_t format_index;
        int64_t timestamp;
    };

    Microsoft::WRL::ComPtr<IMFTransform> decoder = create_decoder();
    MFT_INPUT_STREAM_INFO input_stream_info = {};
    MFT_OUTPUT_STREAM_INFO output_stream_info = {};

    int video_width = -1;
    int video_height = -1;
    int video_framerate = -1;
    int64_t start_timestamp = INT64_MIN;
    int64_t sample_duration = INT64_MIN;
    bool has_parameter_sets = false;

    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d11_context;
    Microsoft::WRL::ComPtr<IMFMediaBuffer> media_buffer;
    Microsoft::WRL::ComPtr<IMFSample> output_sample;
    MFT_OUTPUT_DATA_BUFFER data_buffer = {};
    Microsoft::WRL::ComPtr<ID3D12Device> d12_device;
    Microsoft::WRL::ComPtr<ID3D11Device> d11_device;
    Microsoft::WRL::ComPtr<IDXGIResource1> texture;
    Microsoft::WRL::ComPtr<IDXGIKeyedMutex> texture_mutex;
    HANDLE texture_handle = INVALID_HANDLE_VALUE;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> d11_texture;
    Microsoft::WRL::ComPtr<ID3D12Resource> d12_texture;
    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> device_manager;

    Networking net;
public:
    Decoder(Config &config);
    void connect();
    void init_decoder();
    void handle_packets();
private:
    void create_texture();
    void process_sample(Microsoft::WRL::ComPtr<IMFSample> sample);
    void process_frame(std::vector<uint8_t> &frame);
    static bool is_parameter_sets(std::span<uint8_t> buffer);
    static Microsoft::WRL::ComPtr<IMFSample> create_sample(size_t size, int64_t sample_time, int64_t sample_duration);
    static void copy_payload_to_first_buffer(Microsoft::WRL::ComPtr<IMFSample> sample, std::span<uint8_t> payload);
    static int64_t get_sample_time(int64_t start_timestamp, int64_t end_timestamp); // Timestamps are in nanoseconds, but the result is in hundreds of nanoseconds since the start timestamp.
    static Microsoft::WRL::ComPtr<IMFTransform> create_decoder();
};