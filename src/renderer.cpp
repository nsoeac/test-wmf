#include "renderer.hpp"

#include "lib/lib.hpp"

using Microsoft::WRL::ComPtr;

Renderer::Renderer(Config &config) :
    decoder(config, this) {

    std::println("Initialising rendering resources");

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
    hresult(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT, &feature_level, 1, D3D11_SDK_VERSION, &device, nullptr, &context));

    decoder.connect();
    decoder.init_decoder();

    while (true) {
        decoder.handle_packet();
    }
}