#pragma once

std::string get_win32_error_from_code(DWORD error_code);
std::string get_win32_error();
std::string get_wsa_error();
void check_hresult(HRESULT result);

#define THROW_WIN32(function)                                                                                                            \
    do {                                                                                                                                 \
        DWORD error_code = GetLastError();                                                                                               \
        throw std::runtime_error(std::format(#function " failed with error {}: {}", error_code, get_win32_error_from_code(error_code))); \
    } while (false)

#define THROW_WSA(function)                                                                                                              \
    do {                                                                                                                                 \
        int error_code = WSAGetLastError();                                                                                              \
        throw std::runtime_error(std::format(#function " failed with error {}: {}", error_code, get_win32_error_from_code(error_code))); \
    } while (false)