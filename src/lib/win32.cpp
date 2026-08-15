#include "lib/win32.hpp"
#include "lib/lib.hpp"

std::string get_win32_error_from_code(DWORD error_code) {
    LPWSTR buffer = NULL;
    if (FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, error_code, 0, (LPWSTR)&buffer, 0, nullptr) == 0) {
        std::println("FormatMessageW failed for error {}", error_code);
        abort();
    }

    size_t length = wcslen(buffer);
    std::string message = convert({ buffer, buffer + length });

    return message;
}

std::string get_win32_error() {
    DWORD error_code = GetLastError();
    return get_win32_error_from_code(error_code);
}

std::string get_wsa_error() {
    int code = WSAGetLastError();
    std::string error = get_win32_error_from_code(code);
    return error;
}

void check_hresult(HRESULT result) {
    if (!SUCCEEDED(result)) {
        _com_error error(result);
        const wchar_t *buffer = error.ErrorMessage();
        size_t length = wcslen(buffer);

        std::string message = convert({ buffer, buffer + length });
        std::println("HRESULT {}: {}", result, message);

        abort();
    }
}