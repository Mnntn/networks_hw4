#include "common/protocol.hpp"

#include <cstring>
#include <stdexcept>

namespace mydrive {

namespace {

class Writer {
public:
    void u8(std::uint8_t value) { data_.push_back(value); }

    void u16(std::uint16_t value) {
        data_.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
        data_.push_back(static_cast<std::uint8_t>(value & 0xffu));
    }

    void u32(std::uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            data_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
        }
    }

    void u64(std::uint64_t value) {
        for (int shift = 56; shift >= 0; shift -= 8) {
            data_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
        }
    }

    void string(const std::string& value) {
        u32(static_cast<std::uint32_t>(value.size()));
        data_.insert(data_.end(), value.begin(), value.end());
    }

    const std::vector<std::uint8_t>& data() const { return data_; }

private:
    std::vector<std::uint8_t> data_;
};

class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& data) : data_(data) {}

    std::uint8_t u8() { return read_integer<std::uint8_t>(1); }
    std::uint16_t u16() { return read_integer<std::uint16_t>(2); }
    std::uint32_t u32() { return read_integer<std::uint32_t>(4); }
    std::uint64_t u64() { return read_integer<std::uint64_t>(8); }

    std::string string() {
        const std::uint32_t size = u32();
        if (offset_ + size > data_.size()) {
            throw std::runtime_error("String exceeds payload size");
        }
        std::string result(reinterpret_cast<const char*>(data_.data() + offset_), size);
        offset_ += size;
        return result;
    }

    void ensure_consumed() const {
        if (offset_ != data_.size()) {
            throw std::runtime_error("Payload contains trailing bytes");
        }
    }

private:
    template <typename T>
    T read_integer(std::size_t size) {
        if (offset_ + size > data_.size()) {
            throw std::runtime_error("Unexpected end of payload");
        }
        T value = 0;
        for (std::size_t i = 0; i < size; ++i) {
            value = static_cast<T>((value << 8u) | data_[offset_++]);
        }
        return value;
    }

    const std::vector<std::uint8_t>& data_;
    std::size_t offset_ = 0;
};

void write_file_entry(Writer& writer, const FileEntry& entry) {
    writer.string(entry.relative_path);
    writer.u64(entry.size);
    writer.u32(entry.checksum);
}

FileEntry read_file_entry(Reader& reader) {
    FileEntry entry;
    entry.relative_path = reader.string();
    entry.size = reader.u64();
    entry.checksum = reader.u32();
    return entry;
}

std::vector<std::uint8_t> serialize_file_entries(const std::vector<FileEntry>& files) {
    Writer writer;
    writer.u32(static_cast<std::uint32_t>(files.size()));
    for (const auto& entry : files) {
        write_file_entry(writer, entry);
    }
    return writer.data();
}

std::vector<FileEntry> deserialize_file_entries(const std::vector<std::uint8_t>& payload) {
    Reader reader(payload);
    const std::uint32_t count = reader.u32();
    std::vector<FileEntry> files;
    files.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        files.push_back(read_file_entry(reader));
    }
    reader.ensure_consumed();
    return files;
}

}  // namespace

std::vector<std::uint8_t> encode_frame(MessageType type, const std::vector<std::uint8_t>& payload) {
    Writer writer;
    writer.u32(kProtocolMagic);
    writer.u16(kProtocolVersion);
    writer.u16(static_cast<std::uint16_t>(type));
    writer.u32(static_cast<std::uint32_t>(payload.size()));
    std::vector<std::uint8_t> frame = writer.data();
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

DecodedFrame decode_frame(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < kFrameHeaderSize) {
        throw std::runtime_error("Frame is shorter than header");
    }
    Reader reader(bytes);
    const std::uint32_t magic = reader.u32();
    const std::uint16_t version = reader.u16();
    const auto type = static_cast<MessageType>(reader.u16());
    const std::uint32_t payload_size = reader.u32();
    if (magic != kProtocolMagic) {
        throw std::runtime_error("Invalid protocol magic");
    }
    if (version != kProtocolVersion) {
        throw std::runtime_error("Unsupported protocol version");
    }
    if (payload_size != bytes.size() - kFrameHeaderSize) {
        throw std::runtime_error("Frame payload length mismatch");
    }
    DecodedFrame frame{type, std::vector<std::uint8_t>(bytes.begin() + static_cast<long>(kFrameHeaderSize), bytes.end())};
    return frame;
}

std::vector<std::uint8_t> serialize_hello(const HelloMessage& message) {
    Writer writer;
    writer.u16(message.protocol_version);
    writer.string(message.client_id);
    return writer.data();
}

HelloMessage deserialize_hello(const std::vector<std::uint8_t>& payload) {
    Reader reader(payload);
    HelloMessage message;
    message.protocol_version = reader.u16();
    message.client_id = reader.string();
    reader.ensure_consumed();
    return message;
}

std::vector<std::uint8_t> serialize_file_list(const FileListMessage& message) {
    return serialize_file_entries(message.files);
}

FileListMessage deserialize_file_list(const std::vector<std::uint8_t>& payload) {
    return FileListMessage{deserialize_file_entries(payload)};
}

std::vector<std::uint8_t> serialize_sync_plan(const SyncPlanMessage& message) {
    return serialize_file_entries(message.files_to_upload);
}

SyncPlanMessage deserialize_sync_plan(const std::vector<std::uint8_t>& payload) {
    return SyncPlanMessage{deserialize_file_entries(payload)};
}

std::vector<std::uint8_t> serialize_transfer_request(const TransferRequestMessage& message) {
    Writer writer;
    writer.string(message.transfer_id);
    writer.string(message.client_id);
    writer.string(message.relative_path);
    writer.u64(message.size);
    writer.u32(message.checksum);
    writer.u8(message.use_dma ? 1 : 0);
    return writer.data();
}

TransferRequestMessage deserialize_transfer_request(const std::vector<std::uint8_t>& payload) {
    Reader reader(payload);
    TransferRequestMessage message;
    message.transfer_id = reader.string();
    message.client_id = reader.string();
    message.relative_path = reader.string();
    message.size = reader.u64();
    message.checksum = reader.u32();
    message.use_dma = reader.u8() != 0;
    reader.ensure_consumed();
    return message;
}

std::vector<std::uint8_t> serialize_transfer_result(const TransferResultMessage& message) {
    Writer writer;
    writer.string(message.transfer_id);
    writer.u8(static_cast<std::uint8_t>(message.status));
    writer.u64(message.bytes_received);
    writer.string(message.details);
    return writer.data();
}

TransferResultMessage deserialize_transfer_result(const std::vector<std::uint8_t>& payload) {
    Reader reader(payload);
    TransferResultMessage message;
    message.transfer_id = reader.string();
    message.status = static_cast<TransferStatus>(reader.u8());
    message.bytes_received = reader.u64();
    message.details = reader.string();
    reader.ensure_consumed();
    return message;
}

std::vector<std::uint8_t> serialize_sync_complete(const SyncCompleteMessage& message) {
    Writer writer;
    writer.u8(message.ok ? 1 : 0);
    writer.string(message.details);
    return writer.data();
}

SyncCompleteMessage deserialize_sync_complete(const std::vector<std::uint8_t>& payload) {
    Reader reader(payload);
    SyncCompleteMessage message;
    message.ok = reader.u8() != 0;
    message.details = reader.string();
    reader.ensure_consumed();
    return message;
}

std::vector<std::uint8_t> serialize_error(const ErrorMessage& message) {
    Writer writer;
    writer.string(message.details);
    return writer.data();
}

ErrorMessage deserialize_error(const std::vector<std::uint8_t>& payload) {
    Reader reader(payload);
    ErrorMessage message;
    message.details = reader.string();
    reader.ensure_consumed();
    return message;
}

}  // namespace mydrive
