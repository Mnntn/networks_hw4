#pragma once

#include "common/messages.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mydrive {

constexpr std::size_t kFrameHeaderSize = 12;
constexpr std::uint32_t kMaxControlPayloadSize = 16u * 1024u * 1024u;

std::vector<std::uint8_t> encode_frame(MessageType type, const std::vector<std::uint8_t>& payload);
DecodedFrame decode_frame(const std::vector<std::uint8_t>& bytes);

std::vector<std::uint8_t> serialize_hello(const HelloMessage& message);
HelloMessage deserialize_hello(const std::vector<std::uint8_t>& payload);

std::vector<std::uint8_t> serialize_file_list(const FileListMessage& message);
FileListMessage deserialize_file_list(const std::vector<std::uint8_t>& payload);

std::vector<std::uint8_t> serialize_sync_plan(const SyncPlanMessage& message);
SyncPlanMessage deserialize_sync_plan(const std::vector<std::uint8_t>& payload);

std::vector<std::uint8_t> serialize_transfer_request(const TransferRequestMessage& message);
TransferRequestMessage deserialize_transfer_request(const std::vector<std::uint8_t>& payload);

std::vector<std::uint8_t> serialize_transfer_result(const TransferResultMessage& message);
TransferResultMessage deserialize_transfer_result(const std::vector<std::uint8_t>& payload);

std::vector<std::uint8_t> serialize_sync_complete(const SyncCompleteMessage& message);
SyncCompleteMessage deserialize_sync_complete(const std::vector<std::uint8_t>& payload);

std::vector<std::uint8_t> serialize_error(const ErrorMessage& message);
ErrorMessage deserialize_error(const std::vector<std::uint8_t>& payload);

}  // namespace mydrive
