#include "window.hpp"

#include "lib/lib.hpp"

Window::Window(unsigned width, unsigned height, std::string class_name, std::string window_name) :
    width(width),
    height(height),
    class_name(convert(class_name)),
    window_name(convert(window_name)) {}

void Window::init(HANDLE shutdown_event) {
    shutdown_event_ = shutdown_event;

    std::unique_lock lock(initialised_mutex);

    thread = std::thread(&Window::initialise_window, this);

    initialised_condition_variable.wait(lock, [this]() { return initialised; });
}

void Window::initialise_window() {
    std::println("Initialising window");

    HINSTANCE instance = (HINSTANCE)get_module_handle();
    WNDCLASSW window_class = {};
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.lpszClassName = class_name.data();

    if (RegisterClassW(&window_class) == 0) {
        THROW_WIN32(RegisterClassW);
    }

    handle = CreateWindowExW(0, class_name.data(), window_name.data(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, NULL, NULL, instance, this);
    if (handle == NULL) {
        THROW_WIN32(CreateWindowExW);
    }

    ShowWindow(handle, SW_NORMAL);

    {
        std::unique_lock lock(initialised_mutex);
        initialised = true;
    }

    initialised_condition_variable.notify_all();

    MSG message = {};
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

LRESULT Window::window_procedure(HWND handle, UINT message, WPARAM w_param, LPARAM l_param) {
    Window *window = nullptr;

    if (message == WM_NCCREATE) {
        CREATESTRUCTW *create_struct = (CREATESTRUCTW *)l_param;
        window = (Window *)create_struct->lpCreateParams;

        window->handle = handle;

        SetLastError(0);
        if (SetWindowLongPtrW(handle, GWLP_USERDATA, (LONG_PTR)window) == 0) {
            DWORD error_code = GetLastError();
            if (error_code != 0) {
                std::println("SetWindowLongPtrW failed: {}", get_win32_error_from_code(error_code));
                abort();
            }
        }
    } else {
        window = (Window *)GetWindowLongPtrW(handle, GWLP_USERDATA);
        if (window == nullptr) {
            DWORD error_code = GetLastError();
            if (error_code != 0) {
                std::println("GetWindowLongPtrW failed: {}", get_win32_error_from_code(error_code));
                abort();
            }
        }
    }

    if (window) {
        return window->handle_message(message, w_param, l_param);
    } else {
        return DefWindowProcW(handle, message, w_param, l_param);
    }
}

void Window::shutdown() {
    started_shutting_down = true;

    if (SetEvent(shutdown_event_) == 0) {
        THROW_WIN32(SetEvent);
    }

    std::unique_lock lock(shutting_down_mutex);

    if (!can_finish_shutting_down) {
        shutting_down_condition_variable.wait(lock, [this]() { return can_finish_shutting_down; });
    }
}

LRESULT Window::handle_message(UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case WM_CLOSE: {
        shutdown();
        DestroyWindow(handle);
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_SIZE: {
        {
            std::unique_lock lock(dimensions_mutex);
            width = LOWORD(l_param);
            height = HIWORD(l_param);
        }

        dimensions_condition_variable.notify_all();
        break;
    }
    default:
        return DefWindowProcW(handle, message, w_param, l_param);
    }

    return 0;
}

void Window::finish_shutting_down() {
    {
        std::unique_lock lock(shutting_down_mutex);
        can_finish_shutting_down = true;
    }

    shutting_down_condition_variable.notify_all();
}