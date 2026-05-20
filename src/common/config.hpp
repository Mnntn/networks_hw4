#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace mydrive {

struct ClientConfig {
    std::string client_id;
    std::filesystem::path directory;
    std::string server_host;
    std::uint16_t server_port = 0;
    std::uint32_t max_connections = 1;
    bool dma_enabled = false;
};

struct ServerConfig {
    std::string listen_host = "0.0.0.0";
    std::uint16_t listen_port = 0;
    std::filesystem::path storage_root;
    std::uint32_t io_threads = 1;
};

ClientConfig load_client_config(const std::filesystem::path& path);
ServerConfig load_server_config(const std::filesystem::path& path);

}  // namespace mydrive
