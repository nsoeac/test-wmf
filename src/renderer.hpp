#pragma once

#include "decoder.hpp"

struct Vertex {
    float x;
    float y;
};

struct Renderer {
    HWND handle = {};
    static constexpr int width = 1280;
    static constexpr int height = 720;
    static constexpr std::string_view class_name = "Window";
    static constexpr std::string_view window_name = "Renderer";
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swap_chain;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertex_buffer;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader;
    Renderer(Config &config);
    static LRESULT window_procedure(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle_message(UINT, WPARAM, LPARAM);
    Decoder decoder;
};