#include "common/file_utils.hpp"

#include "common/checksum.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <unordered_set>

namespace mydrive {

std::string trim_copy(std::string value) {
    auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) {
        return !is_space(static_cast<unsigned char>(ch));
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [&](char ch) {
        return !is_space(static_cast<unsigned char>(ch));
    }).base(), value.end());
    return value;
}

bool is_valid_relative_path(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    const std::filesystem::path fs_path(path);
    if (fs_path.is_absolute()) {
        return false;
    }
    for (const auto& part : fs_path) {
        if (part == "..") {
            return false;
        }
    }
    return fs_path.filename() == fs_path;
}

std::filesystem::path make_safe_storage_path(
    const std::filesystem::path& root,
    const std::string& client_id,
    const std::string& relative_path) {
    if (!is_valid_relative_path(relative_path)) {
        throw std::runtime_error("Invalid relative path: " + relative_path);
    }
    return root / client_id / relative_path;
}

std::filesystem::path temp_file_path(const std::filesystem::path& final_path) {
    return final_path.string() + ".part";
}

void ensure_directory(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error) {
        throw std::runtime_error("Failed to create directory: " + path.string());
    }
}

std::vector<FileEntry> scan_top_level_files(const std::filesystem::path& directory) {
    std::vector<FileEntry> files;
    if (!std::filesystem::exists(directory)) {
        throw std::runtime_error("Directory does not exist: " + directory.string());
    }

    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        FileEntry file;
        file.relative_path = entry.path().filename().string();
        file.size = entry.file_size();
        file.checksum = crc32_file(entry.path());
        files.push_back(std::move(file));
    }

    std::sort(files.begin(), files.end(), [](const FileEntry& lhs, const FileEntry& rhs) {
        return lhs.relative_path < rhs.relative_path;
    });
    return files;
}

void replace_atomically(const std::filesystem::path& source, const std::filesystem::path& target) {
    std::error_code error;
    std::filesystem::rename(source, target, error);
    if (!error) {
        return;
    }

    std::filesystem::remove(target, error);
    error.clear();
    std::filesystem::rename(source, target, error);
    if (error) {
        throw std::runtime_error("Failed to replace file atomically: " + target.string());
    }
}

std::vector<std::string> list_relative_filenames(const std::filesystem::path& directory) {
    std::vector<std::string> names;
    if (!std::filesystem::exists(directory)) {
        return names;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file()) {
            names.push_back(entry.path().filename().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

void remove_missing_server_files(
    const std::filesystem::path& client_root,
    const std::vector<FileEntry>& expected_files) {
    std::unordered_set<std::string> expected;
    for (const auto& entry : expected_files) {
        expected.insert(entry.relative_path);
    }

    if (!std::filesystem::exists(client_root)) {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(client_root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string filename = entry.path().filename().string();
        if (expected.find(filename) == expected.end()) {
            std::filesystem::remove(entry.path());
        }
    }
}

}  // namespace mydrive
