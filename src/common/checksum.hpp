#pragma once

#include <cstdint>
#include <filesystem>
#include <istream>

namespace mydrive {

std::uint32_t crc32_stream(std::istream& input);
std::uint32_t crc32_file(const std::filesystem::path& path);

}  // namespace mydrive
