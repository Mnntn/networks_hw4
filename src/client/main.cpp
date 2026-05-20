#include "client/client_app.hpp"

#include "common/config.hpp"

#include <exception>
#include <iostream>
#include <string_view>

int main(int argc, char* argv[]) {
    try {
        if (argc != 4 || std::string_view(argv[1]) != "--config" || std::string_view(argv[3]) != "sync") {
            std::cerr << "Usage: mydrive_client --config <client.yaml> sync\n";
            return 1;
        }
        const mydrive::ClientConfig config = mydrive::load_client_config(argv[2]);
        return mydrive::run_client_sync(config);
    } catch (const std::exception& ex) {
        std::cerr << "Client failed: " << ex.what() << '\n';
        return 1;
    }
}
