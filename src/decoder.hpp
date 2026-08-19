#pragma once

#include "networking.hpp"

struct Decoder {
    struct Renderer *renderer = nullptr;

    struct Frame_Header {
        int32_t frame_index;
        int32_t format_index;
        int64_t timestamp;
    };

    Microsoft::WRL::ComPtr<IMFTransform> decoder = create_decoder();
    MFT_INPUT_STREAM_INFO input_stream_info = {};
    MFT_OUTPUT_STREAM_INFO output_stream_info = {};
    MFT_OUTPUT_DATA_BUFFER data_buffer = {};

    uint32_t video_width = (uint32_t)-1;
    uint32_t video_height = (uint32_t)-1;
    int video_framerate = -1;
    int64_t start_timestamp = INT64_MIN;
    int64_t sample_duration = INT64_MIN;
    bool has_parameter_sets = false;

    Microsoft::WRL::ComPtr<IMFMediaBuffer> media_buffer;
    Microsoft::WRL::ComPtr<IMFSample> output_sample;

    Networking net;
    Decoder(Config &config, struct Renderer *renderer);
    void connect();
    void init_decoder();
    void handle_packets();
    void handle_packet();
    void create_texture();
    void process_sample(Microsoft::WRL::ComPtr<IMFSample> sample);
    void process_frame(std::vector<uint8_t> &frame);
    static bool is_parameter_sets(std::span<uint8_t> buffer);
    static Microsoft::WRL::ComPtr<IMFSample> create_sample(size_t size, int64_t sample_time, int64_t sample_duration);
    static void copy_payload_to_first_buffer(Microsoft::WRL::ComPtr<IMFSample> sample, std::span<uint8_t> payload);
    static int64_t get_sample_time(int64_t start_timestamp, int64_t end_timestamp); // Timestamps are in nanoseconds, but the result is in hundreds of nanoseconds since the start timestamp.
    static Microsoft::WRL::ComPtr<IMFTransform> create_decoder();
};