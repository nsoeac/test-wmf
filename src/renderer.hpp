#pragma once

struct Vertex {
    float x;
    float y;
};

struct Renderer {
    struct Decoder *decoder_ = nullptr;
    struct Window *window_ = nullptr;

    static constexpr unsigned frame_count = 2;
    unsigned width = 1280;
    unsigned height = 720;
    bool shutting_down = false;
    bool packed_texture_is_valid = false;

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
    Microsoft::WRL::ComPtr<ID3D12Resource> packed_texture;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, frame_count> backbuffers;
    D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view = {};
    HANDLE fence_event = NULL;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, (float)width, (float)height);
    D3D12_RECT scissor = { 0, 0, (long)width, (long)height };
    static const DXGI_FORMAT swap_chain_format = DXGI_FORMAT_R8G8B8A8_UNORM;

    // These resources protect the texture from being destroyed while the renderer is rendering.

    std::mutex mutex;
    std::condition_variable condition_variable;

    std::thread thread;

    uint64_t fence_value = 0;
    uint32_t frame_index = 0;
    uint32_t rtv_descriptor_size = 0;
    uint32_t cbv_srv_uav_descriptor_size = 0;

    static constexpr std::string_view class_name = "Window";
    static constexpr std::string_view window_name = "Renderer";
    void start(HWND window_handle, struct Decoder *decoder, struct Window *window);
    void create_packed_texture(uint32_t buffer_size);
    void update_packed_texture(std::span<const uint8_t> data);
private:
    void create_backbuffers();
    void update();
    void render_loop();
    void wait_for_previous_frame();
    void render();
};