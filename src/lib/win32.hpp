#pragma once

std::string get_win32_error_from_code(DWORD error_code);
std::string get_win32_error();
std::string get_wsa_error();
void hresult(HRESULT result);
void compile_shader(std::string_view source_path, std::string_view entrypoint, std::string_view target, std::string_view output_path);
std::string get_working_directory();
Microsoft::WRL::ComPtr<ID3DBlob> load_shader(std::string_view filepath);

HANDLE get_module_handle();

#define THROW_WIN32(function)                                                                                                      \
    do {                                                                                                                           \
        DWORD error_code = GetLastError();                                                                                         \
        std::string error = std::format(#function " failed with error {}: {}", error_code, get_win32_error_from_code(error_code)); \
        std::println("{}", error);                                                                                                 \
        throw std::runtime_error(error);                                                                                           \
    } while (false)

#define THROW_WSA(function)                                                                                                        \
    do {                                                                                                                           \
        int error_code = WSAGetLastError();                                                                                        \
        std::string error = std::format(#function " failed with error {}: {}", error_code, get_win32_error_from_code(error_code)); \
        std::println("{}", error);                                                                                                 \
        throw std::runtime_error(error);                                                                                           \
    } while (false)

#define THROW_WSA_CODE(function, error_code)                                                                                       \
    do {                                                                                                                           \
        std::string error = std::format(#function " failed with error {}: {}", (error_code), get_win32_error_from_code(error_code)); \
        std::println("{}", error);                                                                                                 \
        throw std::runtime_error(error);                                                                                           \
    } while (false)