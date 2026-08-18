#include "renderer.hpp"

#include "lib/lib.hpp"

constexpr bool print_debug_strings = false;

int main() {
    {
        if (print_debug_strings) {
            std::println("Initialising COM");
        }

        hresult(CoInitialize(NULL));
    }

    {
        if (print_debug_strings) {
            std::println("Initialising WSA");
        }

        WSADATA wsa_data = {};
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            THROW_WSA(WSAStartup);
        }
    }

    Config config = read_config();

    Decoder decoder(config);
    decoder.connect();
    decoder.init_decoder();
    decoder.handle_packets();
}