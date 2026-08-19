#pragma once

#include "decoder.hpp"

struct Vertex {
    float x;
    float y;
};

struct Renderer {
    HWND handle = {};
    static LRESULT window_procedure(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle_message(UINT, WPARAM, LPARAM);

    static constexpr uint32_t frame_count = 2;
    static constexpr uint32_t width = 1280;
    static constexpr uint32_t height = 720;

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
    Microsoft::WRL::ComPtr<ID3D12Resource> texture;
    Microsoft::WRL::ComPtr<ID3D12Resource> upload_buffer;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, frame_count> backbuffers;
    Microsoft::WRL::ComPtr<IMFMediaBuffer> media_buffer;
    Microsoft::WRL::ComPtr<IMFSample> sample;
    D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view = {};
    HANDLE fence_event = NULL;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, (float)width, (float)height);
    D3D12_RECT scissor = { 0, 0, (long)width, (long)height };
    static const DXGI_FORMAT swap_chain_format = DXGI_FORMAT_R8G8B8A8_UNORM;

    uint64_t fence_value = 0;
    uint32_t frame_index = 0;
    uint32_t rtv_descriptor_size = 0;
    uint32_t cbv_srv_uav_descriptor_size = 0;

    static constexpr std::string_view class_name = "Window";
    static constexpr std::string_view window_name = "Renderer";
    Renderer(Config &config);
    void wait_for_previous_frame();
    void render_frame();
    void init_renderer();
    void create_texture();
    Decoder decoder;
};