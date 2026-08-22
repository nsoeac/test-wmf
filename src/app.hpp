#pragma once

#include "config.hpp"
#include "window.hpp"
#include "renderer.hpp"
#include "connection.hpp"
#include "decoder.hpp"

struct App {
    HANDLE shutdown_event = NULL;
    Window window;
    Renderer renderer;
    Decoder decoder;
    Connection connection;
    App(Config &config);
};