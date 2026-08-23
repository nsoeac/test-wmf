#include "renderer.hpp"

#include "decoder.hpp"
#include "window.hpp"

#include "lib/lib.hpp"

using Microsoft::WRL::ComPtr;

void Renderer::start(HWND window_handle, Decoder *decoder, Window *window) {
    decoder_ = decoder;
    window_ = window;

    std::println("Initialising factory and adapter");

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

        hresult(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));

        rtv_descriptor_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        cbv_srv_uav_descriptor_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        std::println("Initialising command queue");

        {
            D3D12_COMMAND_QUEUE_DESC queue_desc = {};
            queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            hresult(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue)));
        }

        hresult(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator)));

        hresult(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator.Get(), nullptr, IID_PPV_ARGS(&graphics_command_list)));
        hresult(graphics_command_list->Close());

        std::println("Initialising swap chain");

        DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
        swap_chain_desc.BufferCount = frame_count;
        swap_chain_desc.Width = width;
        swap_chain_desc.Height = height;
        swap_chain_desc.Format = swap_chain_format;
        swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swap_chain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        swap_chain_desc.SampleDesc.Count = 1;
        swap_chain_desc.Scaling = DXGI_SCALING_NONE;

        ComPtr<IDXGISwapChain1> swap_chain_1;
        hresult(factory->CreateSwapChainForHwnd(command_queue.Get(), window_handle, &swap_chain_desc, nullptr, nullptr, &swap_chain_1));
        hresult(swap_chain_1.As(&swap_chain));

        std::println("Creating descriptor heaps");

        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            desc.NumDescriptors = 32767;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&cbv_srv_uav_heap));
        }

        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            desc.NumDescriptors = 32767;
            device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&rtv_heap));
        }

        create_backbuffers();
    }

    {
        hresult(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        fence_value = 1;

        if ((fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr)) == NULL) {
            std::println("CreateEventW failed: {}", get_win32_error());
            abort();
        }
    }

    std::println("Initialising pipeline");

    {
        static constexpr D3D12_BLEND_DESC blend_desc = {
            .AlphaToCoverageEnable = FALSE,
            .IndependentBlendEnable = FALSE,
            .RenderTarget = { {
                .BlendEnable = TRUE,
                .LogicOpEnable = FALSE,
                .SrcBlend = D3D12_BLEND_ONE,
                .DestBlend = D3D12_BLEND_INV_SRC_ALPHA,
                .BlendOp = D3D12_BLEND_OP_ADD,
                .SrcBlendAlpha = D3D12_BLEND_ONE,
                .DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA,
                .BlendOpAlpha = D3D12_BLEND_OP_ADD,
                .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
            } },
        };

        CD3DX12_ROOT_PARAMETER1 root_parameters[2] = {};
        root_parameters[0].InitAsShaderResourceView(0);
        root_parameters[1].InitAsConstants(2, 0);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc = {};
        root_signature_desc.Init_1_1(_countof(root_parameters), root_parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        std::vector<D3D12_INPUT_ELEMENT_DESC> input_elements = {
            { "SV_POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        Microsoft::WRL::ComPtr<ID3DBlob> signature;
        Microsoft::WRL::ComPtr<ID3DBlob> error;
        hresult(D3D12SerializeVersionedRootSignature(&root_signature_desc, &signature, &error));
        hresult(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root_signature)));

        compile_shader("texture.hlsl", "VSMain", "vs_6_6", "texture_vs.cso");
        compile_shader("texture.hlsl", "PSMain", "ps_6_6", "texture_ps.cso");
        Microsoft::WRL::ComPtr<ID3DBlob> vs = load_shader("texture_vs.cso");
        Microsoft::WRL::ComPtr<ID3DBlob> ps = load_shader("texture_ps.cso");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
        pso_desc.InputLayout = { input_elements.data(), (uint32_t)input_elements.size() };
        pso_desc.pRootSignature = root_signature.Get();
        pso_desc.VS = CD3DX12_SHADER_BYTECODE(vs.Get());
        pso_desc.PS = CD3DX12_SHADER_BYTECODE(ps.Get());
        pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        pso_desc.BlendState = blend_desc;
        pso_desc.SampleMask = UINT_MAX;
        pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso_desc.NumRenderTargets = 1;
        pso_desc.RTVFormats[0] = swap_chain_format;
        pso_desc.SampleDesc.Count = 1;
        hresult(device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state)));
    }

    // Upload vertices.

    {
        std::array<Vertex, 6> vertices = { {
            { -1.0f, -1.0f },
            { -1.0f, 1.0f },
            { 1.0f, 1.0f },
            { -1.0f, -1.0f },
            { 1.0f, 1.0f },
            { 1.0f, -1.0f },
        } };

        size_t buffer_size = sizeof(Vertex) * vertices.size();

        {
            D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(buffer_size);
            D3D12_HEAP_PROPERTIES upload_heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            hresult(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertex_buffer)));
        }

        {
            UINT8 *buffer = nullptr;
            CD3DX12_RANGE read_range(0, 0);
            hresult(vertex_buffer->Map(0, &read_range, reinterpret_cast<void **>(&buffer)));
            memcpy(buffer, vertices.data(), buffer_size);
            vertex_buffer->Unmap(0, nullptr);
        }

        vertex_buffer_view.BufferLocation = vertex_buffer->GetGPUVirtualAddress();
        vertex_buffer_view.StrideInBytes = sizeof(Vertex);
        vertex_buffer_view.SizeInBytes = (uint32_t)buffer_size;
    }

    thread = std::thread(&Renderer::render_loop, this);
}

void Renderer::update_packed_texture(std::span<const uint8_t> buffer) {
    auto &producer_buffer = packed_texture.producer_buffer();

    if (producer_buffer.version < packed_texture.latest_version) {
        create_producer_buffer(producer_buffer, (uint32_t)buffer.size());
    }

    D3D12_RANGE read_range = {};

    void *buffer_data = nullptr;
    hresult(producer_buffer.resource->Map(0, &read_range, &buffer_data));

    std::memcpy(buffer_data, buffer.data(), buffer.size());

    D3D12_RANGE written_range = {};
    written_range.Begin = 0;
    written_range.End = buffer.size();
    producer_buffer.resource->Unmap(0, &written_range);

    packed_texture.update_producer_index();
}

void Renderer::create_producer_buffer(Double_Buffer::Buffer &producer_buffer, uint32_t buffer_size) {
    producer_buffer.resource = nullptr;

    D3D12_HEAP_PROPERTIES upload_heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Buffer(buffer_size);
    hresult(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&producer_buffer.resource)));

    producer_buffer.version = packed_texture.latest_version;
}

void Renderer::create_packed_texture(uint32_t buffer_size) {
    packed_texture.latest_version++;

    auto &producer_buffer = packed_texture.producer_buffer();
    create_producer_buffer(producer_buffer, buffer_size);

    packed_texture.update_producer_index();
}

void Renderer::render() {
    hresult(command_allocator->Reset());
    hresult(graphics_command_list->Reset(command_allocator.Get(), pipeline_state.Get()));

    {
        std::vector<D3D12_RESOURCE_BARRIER> barriers = {
            CD3DX12_RESOURCE_BARRIER::Transition(backbuffers[frame_index].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET),
        };

        if (packed_texture.is_valid()) {
            barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(packed_texture.consumer_resource().Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
        }

        graphics_command_list->ResourceBarrier((UINT)barriers.size(), barriers.data());
    }

    graphics_command_list->RSSetViewports(1, &viewport);
    graphics_command_list->RSSetScissorRects(1, &scissor);
    auto backbuffer_handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtv_heap->GetCPUDescriptorHandleForHeapStart(), frame_index, rtv_descriptor_size);
    graphics_command_list->OMSetRenderTargets(1, &backbuffer_handle, FALSE, nullptr);
    FLOAT clear_colour[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    graphics_command_list->ClearRenderTargetView(backbuffer_handle, clear_colour, 0, nullptr);

    if (packed_texture.is_valid()) {
        graphics_command_list->SetGraphicsRootSignature(root_signature.Get());
        graphics_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        graphics_command_list->SetGraphicsRootShaderResourceView(0, packed_texture.consumer_resource()->GetGPUVirtualAddress());
        assert(vertex_buffer_view.SizeInBytes == (sizeof(Vertex) * 6));
        graphics_command_list->IASetVertexBuffers(0, 1, &vertex_buffer_view);
        graphics_command_list->SetGraphicsRoot32BitConstant(1, decoder_->width, 0);
        graphics_command_list->SetGraphicsRoot32BitConstant(1, decoder_->height, 1);
        graphics_command_list->DrawInstanced(6, 1, 0, 0);
    }

    {
        std::vector<D3D12_RESOURCE_BARRIER> barriers = {
            CD3DX12_RESOURCE_BARRIER::Transition(backbuffers[frame_index].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT)
        };

        if (packed_texture.is_valid()) {
            barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(packed_texture.consumer_resource().Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON));
        }

        graphics_command_list->ResourceBarrier((UINT)barriers.size(), barriers.data());
    }

    graphics_command_list->Close();
    ID3D12CommandList *command_lists[] = { graphics_command_list.Get() };
    command_queue->ExecuteCommandLists(_countof(command_lists), command_lists);

    HRESULT present_result = swap_chain->Present(0, 0);
    switch (present_result) {
    case DXGI_STATUS_OCCLUDED:
        break;
    default:
        hresult(present_result);
    }

    packed_texture.update_consumer_index();

    wait_for_previous_frame();
    frame_index = swap_chain->GetCurrentBackBufferIndex();
}

void Renderer::wait_for_previous_frame() {
    uint64_t current_fence_value = fence_value;
    hresult(command_queue->Signal(fence.Get(), current_fence_value));
    fence_value++;

    if (fence->GetCompletedValue() < current_fence_value) {
        hresult(fence->SetEventOnCompletion(current_fence_value, fence_event));
        WaitForSingleObject(fence_event, INFINITE);
    }
}

void Renderer::update() {
    std::unique_lock lock(window_->dimensions_mutex);
    bool dimensions_changed = (window_->width != width) || (window_->height != height);
    bool dimensions_valid = (window_->width > 0) && (window_->height > 0);
    if (dimensions_changed && dimensions_valid) {
        width = window_->width;
        height = window_->height;
        viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, (float)width, (float)height);
        scissor = { 0, 0, (long)width, (long)height };

        for (auto &backbuffer : backbuffers) {
            backbuffer = nullptr;
        }

        hresult(swap_chain->ResizeBuffers(frame_count, width, height, swap_chain_format, DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING));
        create_backbuffers();

        frame_index = swap_chain->GetCurrentBackBufferIndex();
    }
}

void Renderer::render_loop() {
    while (!window_->started_shutting_down) {
        update();
        render();
    }

    std::println("Shutting renderer down");
}

void Renderer::create_backbuffers() {
    for (unsigned i = 0; i < frame_count; i++) {
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtv_heap->GetCPUDescriptorHandleForHeapStart(), i, rtv_descriptor_size);
        hresult(swap_chain->GetBuffer(i, IID_PPV_ARGS(&backbuffers[i])));
        device->CreateRenderTargetView(backbuffers[i].Get(), nullptr, rtv_handle);
    }
}