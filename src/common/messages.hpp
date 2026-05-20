#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mydrive {

constexpr std::uint32_t kProtocolMagic = 0x4d445256;  // MDRV
constexpr std::uint16_t kProtocolVersion = 1;

enum class MessageType : std::uint16_t {
    hello = 1,
    file_list = 2,
    sync_plan = 3,
    transfer_request = 4,
    transfer_result = 5,
    sync_complete = 6,
    error = 7,
};

struct FileEntry {
    std::string relative_path;
    std::uint64_t size = 0;
    std::uint32_t checksum = 0;
};

struct HelloMessage {
    std::uint16_t protocol_version = kProtocolVersion;
    std::string client_id;
};

struct FileListMessage {
    std::vector<FileEntry> files;
};

struct SyncPlanMessage {
    std::vector<FileEntry> files_to_upload;
};

struct TransferRequestMessage {
    std::string transfer_id;
    std::string client_id;
    std::string relative_path;
    std::uint64_t size = 0;
    std::uint32_t checksum = 0;
    bool use_dma = false;
};

enum class TransferStatus : std::uint8_t {
    ok = 0,
    checksum_mismatch = 1,
    invalid_path = 2,
    write_failed = 3,
    protocol_error = 4,
};

struct TransferResultMessage {
    std::string transfer_id;
    TransferStatus status = TransferStatus::ok;
    std::uint64_t bytes_received = 0;
    std::string details;
};

struct SyncCompleteMessage {
    bool ok = true;
    std::string details;
};

struct ErrorMessage {
    std::string details;
};

struct DecodedFrame {
    MessageType type;
    std::vector<std::uint8_t> payload;
};

}  // namespace mydrive
