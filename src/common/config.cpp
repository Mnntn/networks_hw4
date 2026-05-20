#include "common/config.hpp"

#include "common/file_utils.hpp"

#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace mydrive {

namespace {

std::unordered_map<std::string, std::string> parse_key_values(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Failed to open config: " + path.string());
    }

    std::unordered_map<std::string, std::string> values;
    std::string line;
    while (std::getline(input, line)) {
        line = trim_copy(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const std::size_t separator = line.find(':');
        if (separator == std::string::npos) {
            continue;
        }

        std::string key = trim_copy(line.substr(0, separator));
        std::string value = trim_copy(line.substr(separator + 1));
        values.emplace(std::move(key), std::move(value));
    }
    return values;
}

std::string require_value(
    const std::unordered_map<std::string, std::string>& values,
    const std::string& key) {
    const auto it = values.find(key);
    if (it == values.end() || it->second.empty()) {
        throw std::runtime_error("Missing required config key: " + key);
    }
    return it->second;
}

std::uint16_t parse_port(const std::string& value, const std::string& key) {
    const unsigned long parsed = std::stoul(value);
    if (parsed > 65535ul) {
        throw std::runtime_error("Invalid port for key: " + key);
    }
    return static_cast<std::uint16_t>(parsed);
}

std::uint32_t parse_uint(const std::string& value, const std::string& key) {
    const unsigned long parsed = std::stoul(value);
    if (parsed > static_cast<unsigned long>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::runtime_error("Invalid integer for key: " + key);
    }
    return static_cast<std::uint32_t>(parsed);
}

bool parse_bool(const std::string& value) {
    return value == "true" || value == "1" || value == "yes" || value == "on";
}

}  // namespace

ClientConfig load_client_config(const std::filesystem::path& path) {
    const auto values = parse_key_values(path);
    ClientConfig config;
    config.client_id = require_value(values, "client_id");
    config.directory = require_value(values, "directory");
    config.server_host = require_value(values, "server_host");
    config.server_port = parse_port(require_value(values, "server_port"), "server_port");
    config.max_connections = parse_uint(require_value(values, "max_connections"), "max_connections");
    if (config.max_connections < 1 || config.max_connections > 32) {
        throw std::runtime_error("client.max_connections must be in range 1..32");
    }
    const auto it = values.find("dma_enabled");
    config.dma_enabled = (it != values.end()) ? parse_bool(it->second) : false;
    return config;
}

ServerConfig load_server_config(const std::filesystem::path& path) {
    const auto values = parse_key_values(path);
    ServerConfig config;
    if (const auto it = values.find("listen_host"); it != values.end()) {
        config.listen_host = it->second;
    }
    config.listen_port = parse_port(require_value(values, "listen_port"), "listen_port");
    config.storage_root = require_value(values, "storage_root");
    if (const auto it = values.find("io_threads"); it != values.end()) {
        config.io_threads = parse_uint(it->second, "io_threads");
    }
    if (config.io_threads == 0) {
        throw std::runtime_error("server.io_threads must be positive");
    }
    return config;
}

}  // namespace mydrive
