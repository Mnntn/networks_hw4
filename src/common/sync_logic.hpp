#pragma once

#include "common/messages.hpp"

#include <vector>

namespace mydrive {

std::vector<FileEntry> compute_upload_plan(
    const std::vector<FileEntry>& client_files,
    const std::vector<FileEntry>& server_files);

}  // namespace mydrive
