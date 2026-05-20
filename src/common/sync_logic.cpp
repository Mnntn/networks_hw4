#include "common/sync_logic.hpp"

#include <unordered_map>

namespace mydrive {

std::vector<FileEntry> compute_upload_plan(
    const std::vector<FileEntry>& client_files,
    const std::vector<FileEntry>& server_files) {
    std::unordered_map<std::string, FileEntry> by_name;
    for (const auto& entry : server_files) {
        by_name.emplace(entry.relative_path, entry);
    }

    std::vector<FileEntry> plan;
    for (const auto& entry : client_files) {
        const auto it = by_name.find(entry.relative_path);
        if (it == by_name.end() ||
            it->second.size != entry.size ||
            it->second.checksum != entry.checksum) {
            plan.push_back(entry);
        }
    }
    return plan;
}

}  // namespace mydrive
