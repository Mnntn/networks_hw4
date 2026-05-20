#pragma once

#include "common/messages.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace mydrive {

std::string trim_copy(std::string value);
bool is_valid_relative_path(const std::string& path);
std::filesystem::path make_safe_storage_path(
    const std::filesystem::path& root,
    const std::string& client_id,
    const std::string& relative_path);
std::filesystem::path temp_file_path(const std::filesystem::path& final_path);
void ensure_directory(const std::filesystem::path& path);
std::vector<FileEntry> scan_top_level_files(const std::filesystem::path& directory);
void replace_atomically(const std::filesystem::path& source, const std::filesystem::path& target);
std::vector<std::string> list_relative_filenames(const std::filesystem::path& directory);
void remove_missing_server_files(
    const std::filesystem::path& client_root,
    const std::vector<FileEntry>& expected_files);

}  // namespace mydrive
