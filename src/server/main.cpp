#include "server/server_app.hpp"

#include "common/config.hpp"

#include <exception>
#include <iostream>
#include <string_view>

int main(int argc, char* argv[]) {
    try {
        if (argc != 3 || std::string_view(argv[1]) != "--config") {
            std::cerr << "Usage: mydrive_server --config <server.yaml>\n";
            return 1;
        }
        const mydrive::ServerConfig config = mydrive::load_server_config(argv[2]);
        return mydrive::run_server(config);
    } catch (const std::exception& ex) {
        std::cerr << "Server failed: " << ex.what() << '\n';
        return 1;
    }
}
