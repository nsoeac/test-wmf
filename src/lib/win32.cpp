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

std::string get_working_directory() {
    DWORD length = GetCurrentDirectoryW(0, NULL);
    if (length == 0) {
        std::println("GetCurrentDirectoryW failed: {}", get_win32_error());
        abort();
    }

    std::wstring cwd(length, L'\0');
    DWORD characters_returned = GetCurrentDirectoryW(length, cwd.data());
    assert(length == (characters_returned + 1));

    return convert(cwd);
}

void compile_shader(std::string_view source_path, std::string_view entrypoint, std::string_view target, std::string_view output_path) {
    if (!std::filesystem::exists(source_path)) {
        std::println("Source path {} does not exist (cwd is {})", source_path, get_working_directory());
        abort();
    }

    if (std::filesystem::exists(output_path)) {
        std::filesystem::file_time_type last_update = std::filesystem::last_write_time(source_path);
        std::filesystem::file_time_type last_compile = std::filesystem::last_write_time(output_path);
        if (last_update <= last_compile) {
            return;
        }
    }

    std::string command = std::format("dxc {} -T {} -E {} -Zi -Qembed_debug -Od -Fo {}", source_path, target, entrypoint, output_path);
    if (system(command.data()) != 0) {
        std::println("Command failed: {}", command);
        abort();
    } else {
        std::println("{} compiled", output_path);
    }
}

Microsoft::WRL::ComPtr<ID3DBlob> load_shader(std::string_view filepath) {
    Microsoft::WRL::ComPtr<ID3DBlob> shader;
    std::vector<char> shader_code = read_file(filepath.data());
    hresult(D3DCreateBlob(shader_code.size(), &shader));
    std::span<char> data_buffer((char *)shader->GetBufferPointer(), shader->GetBufferSize());
    std::ranges::copy(shader_code, data_buffer.begin());
    return shader;
}