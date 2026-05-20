#include "common/checksum.hpp"
#include "common/file_utils.hpp"
#include "common/protocol.hpp"
#include "common/sync_logic.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_protocol_roundtrip() {
    const mydrive::TransferRequestMessage source{
        "transfer-1", "client-a", "example.bin", 1024, 0x12345678u, true};
    const auto payload = mydrive::serialize_transfer_request(source);
    const auto frame = mydrive::encode_frame(mydrive::MessageType::transfer_request, payload);
    const auto decoded = mydrive::decode_frame(frame);
    const auto target = mydrive::deserialize_transfer_request(decoded.payload);

    expect(decoded.type == mydrive::MessageType::transfer_request, "Decoded frame type mismatch");
    expect(target.transfer_id == source.transfer_id, "Transfer id mismatch");
    expect(target.client_id == source.client_id, "Client id mismatch");
    expect(target.relative_path == source.relative_path, "Relative path mismatch");
    expect(target.size == source.size, "File size mismatch");
    expect(target.checksum == source.checksum, "Checksum mismatch");
    expect(target.use_dma == source.use_dma, "DMA flag mismatch");
}

void test_upload_plan() {
    const std::vector<mydrive::FileEntry> client_files{
        {"a.bin", 10, 1},
        {"b.bin", 20, 2},
        {"c.bin", 30, 3},
    };
    const std::vector<mydrive::FileEntry> server_files{
        {"a.bin", 10, 1},
        {"b.bin", 20, 9},
    };

    const auto plan = mydrive::compute_upload_plan(client_files, server_files);
    expect(plan.size() == 2, "Upload plan size must be 2");
    expect(plan[0].relative_path == "b.bin", "Changed file must be scheduled");
    expect(plan[1].relative_path == "c.bin", "Missing file must be scheduled");
}

void test_paths() {
    expect(mydrive::is_valid_relative_path("hello.txt"), "Flat file should be valid");
    expect(!mydrive::is_valid_relative_path("../escape.txt"), "Traversal path must be rejected");
    expect(!mydrive::is_valid_relative_path("/tmp/file"), "Absolute path must be rejected");
    expect(!mydrive::is_valid_relative_path("nested/child.txt"), "Nested path must be rejected");
}

void test_checksum() {
    const auto temp_dir = std::filesystem::temp_directory_path() / "mydrive_tests";
    mydrive::ensure_directory(temp_dir);
    const auto path = temp_dir / "checksum.bin";

    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "abcdef";
    }

    const std::uint32_t checksum = mydrive::crc32_file(path);
    expect(checksum == 0x4b8e39efu, "CRC32 for test content is unexpected");
    std::filesystem::remove(path);
}

}  // namespace

int main() {
    try {
        test_protocol_roundtrip();
        test_upload_plan();
        test_paths();
        test_checksum();
        std::cout << "All tests passed\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Test failure: " << ex.what() << '\n';
        return 1;
    }
}
