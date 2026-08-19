#include "renderer.hpp"

#include "lib/lib.hpp"

using Microsoft::WRL::ComPtr;

LRESULT Renderer::window_procedure(HWND handle, UINT message, WPARAM w_param, LPARAM l_param) {
    Renderer *renderer = nullptr;

    if (message == WM_NCCREATE) {
        CREATESTRUCTW *create_struct = (CREATESTRUCTW *)l_param;
        renderer = (Renderer *)create_struct->lpCreateParams;

        renderer->handle = handle;

        SetLastError(0);
        if (SetWindowLongPtrW(handle, GWLP_USERDATA, (LONG_PTR)renderer) == 0) {
            DWORD error_code = GetLastError();
            if (error_code != 0) {
                std::println("SetWindowLongPtrW failed: {}", get_win32_error_from_code(error_code));
                abort();
            }
        }
    } else {
        renderer = (Renderer *)GetWindowLongPtrW(handle, GWLP_USERDATA);
        if (renderer == nullptr) {
            DWORD error_code = GetLastError();
            if (error_code != 0) {
                std::println("GetWindowLongPtrW failed: {}", get_win32_error_from_code(error_code));
                abort();
            }
        }
    }

    if (renderer) {
        return renderer->handle_message(message, w_param, l_param);
    } else {
        return DefWindowProcW(handle, message, w_param, l_param);
    }
}

LRESULT Renderer::handle_message(UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case WM_CLOSE:
        DestroyWindow(handle);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(handle, message, w_param, l_param);
    }

    return 0;
}

void Renderer::init_renderer() {
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
        hresult(factory->CreateSwapChainForHwnd(command_queue.Get(), handle, &swap_chain_desc, nullptr, nullptr, &swap_chain_1));
        hresult(swap_chain_1.As(&swap_chain));
    }

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

        CD3DX12_ROOT_PARAMETER1 root_parameters[3] = {};
        root_parameters[0].InitAsUnorderedAccessView(0);
        root_parameters[1].InitAsUnorderedAccessView(1);
        root_parameters[2].InitAsConstants(2, 0);

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
        pso_desc.SampleDesc.Count = sample_count;
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
            D3D12_HEAP_PROPERTIES heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            hresult(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertex_buffer)));
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
}

void Renderer::create_texture() {
    texture = nullptr;
    D3D12_RESOURCE_DESC texture_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_NV12, decoder.video_width, decoder.video_height, 1, 1);
    texture_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES upload_heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_GPU_UPLOAD);
    hresult(device->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE, &texture_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texture)));

    uint32_t pixel_count = decoder.video_width * decoder.video_height;
    D3D12_UNORDERED_ACCESS_VIEW_DESC luminance_subresource = {};
    luminance_subresource.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
    luminance_subresource.Format = DXGI_FORMAT_R8_UINT;
    luminance_subresource.Texture2DArray.ArraySize = -1;
    luminance_subresource.Texture2DArray.PlaneSlice = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE luminance_handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(cbv_srv_uav_heap->GetCPUDescriptorHandleForHeapStart(), 0, cbv_srv_uav_descriptor_size);
    device->CreateUnorderedAccessView(texture.Get(), nullptr, &luminance_subresource, luminance_handle);
    D3D12_UNORDERED_ACCESS_VIEW_DESC chrominance_subresource = {};
    chrominance_subresource.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
    chrominance_subresource.Format = DXGI_FORMAT_R8G8_UINT;
    chrominance_subresource.Texture2DArray.ArraySize = -1;
    chrominance_subresource.Texture2DArray.PlaneSlice = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE chrominance_handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(cbv_srv_uav_heap->GetCPUDescriptorHandleForHeapStart(), 1, cbv_srv_uav_descriptor_size);
    device->CreateUnorderedAccessView(texture.Get(), nullptr, &chrominance_subresource, chrominance_handle);
}

Renderer::Renderer(Config &config) :
    decoder(config, this) {
    {
        std::println("Initialising window");

        std::wstring wide_class_name = convert(class_name);
        std::wstring wide_window_name = convert(window_name);

        HINSTANCE instance = (HINSTANCE)get_module_handle();
        WNDCLASSW window_class = {};
        window_class.lpfnWndProc = window_procedure;
        window_class.hInstance = instance;
        window_class.lpszClassName = wide_class_name.data();

        if (RegisterClassW(&window_class) == 0) {
            THROW_WIN32(RegisterClassW);
        }

        handle = CreateWindowExW(0, wide_class_name.data(), wide_window_name.data(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, NULL, NULL, instance, this);
        if (handle == NULL) {
            THROW_WIN32(CreateWindowExW);
        }

        ShowWindow(handle, SW_NORMAL);
    }

    init_renderer();

    {
        decoder.connect();
        decoder.init_decoder();

        while (true) {
            decoder.handle_packet();
        }
    }
}