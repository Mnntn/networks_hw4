#include "common/checksum.hpp"

#include <array>
#include <fstream>
#include <stdexcept>

namespace mydrive {

namespace {

constexpr std::uint32_t kPolynomial = 0xedb88320u;

std::array<std::uint32_t, 256> make_crc_table() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < table.size(); ++i) {
        std::uint32_t value = i;
        for (int bit = 0; bit < 8; ++bit) {
            if ((value & 1u) != 0u) {
                value = (value >> 1u) ^ kPolynomial;
            } else {
                value >>= 1u;
            }
        }
        table[i] = value;
    }
    return table;
}

const std::array<std::uint32_t, 256>& crc_table() {
    static const std::array<std::uint32_t, 256> table = make_crc_table();
    return table;
}

}  // namespace

std::uint32_t crc32_stream(std::istream& input) {
    std::array<char, 64 * 1024> buffer{};
    std::uint32_t crc = 0xffffffffu;
    while (input.good()) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize bytes = input.gcount();
        for (std::streamsize i = 0; i < bytes; ++i) {
            const std::uint8_t value = static_cast<std::uint8_t>(buffer[static_cast<std::size_t>(i)]);
            crc = (crc >> 8u) ^ crc_table()[(crc ^ value) & 0xffu];
        }
    }
    return crc ^ 0xffffffffu;
}

std::uint32_t crc32_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Failed to open file for checksum: " + path.string());
    }
    return crc32_stream(input);
}

}  // namespace mydrive
