#pragma once

using Renderer_Pointer = struct Renderer *;

namespace Decoding {

struct Header {
    int32_t frame_index;
    int32_t format_index;
    int64_t timestamp;
};

struct Message {
    Header header;
    std::vector<uint8_t> buffer;
    std::span<uint8_t> frame;
};

struct Decoder {
    Renderer_Pointer renderer_ = nullptr;

    Microsoft::WRL::ComPtr<IMFTransform> decoder = create_decoder();
    MFT_INPUT_STREAM_INFO input_stream_info = {};
    MFT_OUTPUT_STREAM_INFO output_stream_info = {};
    MFT_OUTPUT_DATA_BUFFER data_buffer = {};
    static constexpr bool print_debug_strings = false;

    unsigned width;
    unsigned height;
    unsigned framerate;
    int64_t start_timestamp = INT64_MIN;
    int64_t sample_duration = INT64_MIN;
    bool has_parameter_sets = false;

    std::thread thread;

    Microsoft::WRL::ComPtr<IMFMediaBuffer> media_buffer;
    Microsoft::WRL::ComPtr<IMFSample> output_sample;

    std::vector<Message> cached_messages;
    int64_t next_frame_index = 0;

    void start(unsigned video_width, unsigned video_height, unsigned video_framerate, Renderer_Pointer renderer);
    void init();
    void create_texture();
    void process_sample(Microsoft::WRL::ComPtr<IMFSample> sample);
    void process_message(Message &&message);
    void cache_message(Message &&message);
    void decode_frame(std::span<uint8_t> buffer, int64_t timestamp);
    void process_cached_messages();
    static bool is_parameter_sets(std::span<uint8_t> buffer);
    static Microsoft::WRL::ComPtr<IMFSample> create_sample(size_t size, int64_t sample_time, int64_t sample_duration);
    static void copy_payload_to_first_buffer(Microsoft::WRL::ComPtr<IMFSample> sample, std::span<uint8_t> payload);
    static int64_t get_sample_time(int64_t start_timestamp, int64_t end_timestamp); // Timestamps are in nanoseconds, but the result is in hundreds of nanoseconds since the start timestamp.
    static Microsoft::WRL::ComPtr<IMFTransform> create_decoder();
};

}

using Decoder = Decoding::Decoder;

template <>
struct std::formatter<Decoding::Header> {
    constexpr auto parse(auto &context) {
        return context.begin();
    }

    constexpr auto format(const Decoding::Header &header, std::format_context &context) const {
        return std::format_to(context.out(), "{{ frame_index: {}, format_index: {}, timestamp: {} }}", header.frame_index, header.format_index, header.timestamp);
    }
};