#pragma once

#include "decoder.hpp"

struct Vertex {
    float x;
    float y;
};

// When the producer wants to update a buffer, it must check that the producer and consumer indices are the same.
// If they are, the producer has to wait until the consumer has changed its index and signalled the condition variable.
// When the producer is updating a buffer, it must recreate it when its `version` is less than `current_version`.
struct Double_Buffer {
    static constexpr bool print_debug_strings = false;

    struct Buffer {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        int version = -1;
    };

    int latest_version = -1;

    Microsoft::WRL::ComPtr<ID3D12Resource> &consumer_resource() {
        return buffers[consumer_index].resource;
    }

    void update_consumer_index() {
        if (consumer_index == producer_index) {
            consumer_index = 1 - consumer_index;

            if (print_debug_strings) {
                std::println("Consumer index is now {}", consumer_index);
            }
        }

        condition_variable.notify_all();
    }

    Buffer &producer_buffer() {
        wait_for_producer_buffer();
        return buffers[producer_index];
    }

    bool is_valid() const {
        return buffers[consumer_index].version >= 0;
    }

    void update_producer_index() {
        producer_index = 1 - producer_index;

        if (print_debug_strings) {
            std::println("Producer index is now {}", producer_index);
        }
    }
private:
    std::array<Buffer, 2> buffers;
    int producer_index = 1;
    int consumer_index = 0;

    std::mutex mutex;
    std::condition_variable condition_variable;

    void wait_for_producer_buffer() {
        if (producer_index == consumer_index) {
            std::unique_lock lock(mutex);
            condition_variable.wait(lock, [this]() { return consumer_index != producer_index; });
        }
    }
};

struct Renderer {
    Decoder *decoder_ = nullptr;
    struct Window *window_ = nullptr;

    static constexpr unsigned frame_count = 2;
    unsigned width = 1280;
    unsigned height = 720;
    bool shutting_down = false;

    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> cbv_srv_uav_heap;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocator;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> graphics_command_list;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_state;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
    Microsoft::WRL::ComPtr<ID3D12Resource> vertex_buffer;
    Double_Buffer packed_texture;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, frame_count> backbuffers;
    D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view = {};
    HANDLE fence_event = NULL;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, (float)width, (float)height);
    D3D12_RECT scissor = { 0, 0, (long)width, (long)height };
    static const DXGI_FORMAT swap_chain_format = DXGI_FORMAT_R8G8B8A8_UNORM;

    std::thread thread;

    uint64_t fence_value = 0;
    uint32_t frame_index = 0;
    uint32_t rtv_descriptor_size = 0;
    uint32_t cbv_srv_uav_descriptor_size = 0;

    static constexpr std::string_view class_name = "Window";
    static constexpr std::string_view window_name = "Renderer";
    void start(HWND window_handle, Decoder *decoder, struct Window *window);
    void create_packed_texture(uint32_t buffer_size);
    void update_packed_texture(std::span<const uint8_t> data);
private:
    void create_producer_buffer(Double_Buffer::Buffer &producer_buffer, uint32_t buffer_size);
    void create_backbuffers();
    void update();
    void render_loop();
    void wait_for_previous_frame();
    void render();
};