#pragma once

#include "decoder.hpp"

struct Renderer {
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Renderer(Config &config);
    Decoder decoder;
};