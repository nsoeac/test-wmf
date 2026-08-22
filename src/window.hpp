#pragma once

struct Window {
    Window(unsigned width = 1280, unsigned height = 720, std::string class_name = "Window Class", std::string window_name = "Main Window");

    void init(HANDLE shutdown_event);
    void finish_shutting_down();

    HANDLE shutdown_event_;
    std::mutex dimensions_mutex;
    std::condition_variable dimensions_condition_variable;
    unsigned width;
    unsigned height;
    std::wstring class_name;
    std::wstring window_name;
    std::thread thread;

    HWND handle = NULL;

    std::mutex initialised_mutex;
    std::condition_variable initialised_condition_variable;
    bool initialised = false;

    static LRESULT window_procedure(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle_message(UINT, WPARAM, LPARAM);

    std::mutex shutting_down_mutex;
    std::condition_variable shutting_down_condition_variable;
    bool started_shutting_down = false;
    bool can_finish_shutting_down = false;
    void shutdown();
private:
    void initialise_window();
};