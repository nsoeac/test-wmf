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

    {
        std::println("Initialising rendering resources");

        {
            ComPtr<IDXGIFactory4> factory;
            hresult(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));

            ComPtr<IDXGIAdapter1> adapter;
            for (UINT adapter_index = 0;; adapter_index++) {
                HRESULT result = factory->EnumAdapters1(adapter_index, &adapter);
                if (SUCCEEDED(result)) {
                    break;
                } else if (result != DXGI_ERROR_INVALID_CALL) {
                    hresult(result);
                }
            }

            D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
            DXGI_SWAP_CHAIN_DESC swap_chain_desc = {};
            swap_chain_desc.BufferCount = 1;
            swap_chain_desc.BufferDesc.Width = width;
            swap_chain_desc.BufferDesc.Height = height;
            swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            swap_chain_desc.BufferDesc.RefreshRate.Numerator = 60;
            swap_chain_desc.BufferDesc.RefreshRate.Denominator = 1;
            swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swap_chain_desc.OutputWindow = handle;
            swap_chain_desc.SampleDesc.Count = 1;
            swap_chain_desc.Windowed = TRUE;
            hresult(D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_DEBUG, &feature_level, 1, D3D11_SDK_VERSION, &swap_chain_desc, &swap_chain, &device, nullptr, &context));
        }

        {
            std::array<Vertex, 6> vertices = { {
                { -1.0f, -1.0f },
                { -1.0f, 1.0f },
                { 1.0f, 1.0f },
                { -1.0f, -1.0f },
                { 1.0f, 1.0f },
                { 1.0f, -1.0f },
            } };

            D3D11_BUFFER_DESC desc = {};
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.ByteWidth = sizeof(Vertex) * 6;
            desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

            D3D11_SUBRESOURCE_DATA initial_data = {};
            initial_data.pSysMem = vertices.data();

            hresult(device->CreateBuffer(&desc, &initial_data, &vertex_buffer));
        }

        compile_shader("texture.hlsl", "VSMain", "vs_5_0", "texture_vs.cso");
        compile_shader("texture.hlsl", "PSMain", "ps_5_0", "texture_ps.cso");
        std::vector<char> vs = read_file("texture_vs.cso");
        std::vector<char> ps = read_file("texture_ps.cso");
        hresult(device->CreateVertexShader(vs.data(), vs.size(), nullptr, &vertex_shader));
        hresult(device->CreatePixelShader(ps.data(), ps.size(), nullptr, &pixel_shader));
    }

    {
        decoder.connect();
        decoder.init_decoder();

        while (true) {
            decoder.handle_packet();
        }
    }
}