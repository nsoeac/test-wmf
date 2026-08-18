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

void hresult(HRESULT result) {
    if (!SUCCEEDED(result)) {
        _com_error error(result);
        const wchar_t *error_message = error.ErrorMessage();
        const wchar_t *description = error.Description();
        const wchar_t *help_file = error.HelpFile();
        const wchar_t *source = error.Source();
        GUID guid = error.GUID();

        auto print_valid_string = [](const wchar_t *string) -> void {
            if (string == nullptr) {
                return;
            }

            size_t length = wcslen(string);
            bool valid = (length != -1) && (length > 0);

            if (valid) {
                std::println("{}", convert(string));
            }
        };

        std::println("HRESULT {}: {}", result, convert(error_message));
        print_valid_string(description);
        print_valid_string(help_file);
        print_valid_string(source);

        abort();
    }
}

HANDLE get_module_handle() {
    HANDLE handle = GetModuleHandleW(NULL);

    if (handle == NULL) {
        THROW_WIN32(GetModuleHandleW);
    }

    return handle;
}