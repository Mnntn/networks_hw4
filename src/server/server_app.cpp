#include "server/server_app.hpp"

#include "common/checksum.hpp"
#include "common/file_utils.hpp"
#include "common/protocol.hpp"
#include "common/sync_logic.hpp"

#include <boost/asio.hpp>

#include <array>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace mydrive {

using boost::asio::ip::tcp;

namespace {

class FramedSession : public std::enable_shared_from_this<FramedSession> {
public:
    explicit FramedSession(tcp::socket socket) : socket_(std::move(socket)) {}

    template <typename Handler>
    void async_read_frame(Handler&& handler) {
        auto self = shared_from_this();
        boost::asio::async_read(
            socket_,
            boost::asio::buffer(header_),
            [this, self, handler = std::forward<Handler>(handler)](
                const boost::system::error_code& error,
                std::size_t /*bytes*/) mutable {
                if (error) {
                    handler(error, DecodedFrame{});
                    return;
                }

                const std::uint32_t payload_size =
                    (static_cast<std::uint32_t>(header_[8]) << 24u) |
                    (static_cast<std::uint32_t>(header_[9]) << 16u) |
                    (static_cast<std::uint32_t>(header_[10]) << 8u) |
                    static_cast<std::uint32_t>(header_[11]);
                if (payload_size > kMaxControlPayloadSize) {
                    handler(boost::asio::error::message_size, DecodedFrame{});
                    return;
                }

                payload_.assign(kFrameHeaderSize + payload_size, 0);
                std::copy(header_.begin(), header_.end(), payload_.begin());

                boost::asio::async_read(
                    socket_,
                    boost::asio::buffer(payload_.data() + kFrameHeaderSize, payload_size),
                    [this, self, handler = std::move(handler)](
                        const boost::system::error_code& read_error,
                        std::size_t /*payload_bytes*/) mutable {
                        if (read_error) {
                            handler(read_error, DecodedFrame{});
                            return;
                        }
                        try {
                            handler(read_error, decode_frame(payload_));
                        } catch (...) {
                            handler(
                                boost::asio::error::operation_aborted,
                                DecodedFrame{});
                        }
                    });
            });
    }

    template <typename Handler>
    void async_write_message(
        MessageType type,
        const std::vector<std::uint8_t>& payload,
        Handler&& handler) {
        outbound_ = encode_frame(type, payload);
        auto self = shared_from_this();
        boost::asio::async_write(
            socket_,
            boost::asio::buffer(outbound_),
            [self, handler = std::forward<Handler>(handler)](
                const boost::system::error_code& error,
                std::size_t bytes) mutable { handler(error, bytes); });
    }

    tcp::socket& socket() { return socket_; }

private:
    tcp::socket socket_;
    std::array<std::uint8_t, kFrameHeaderSize> header_{};
    std::vector<std::uint8_t> payload_;
    std::vector<std::uint8_t> outbound_;
};

struct TransferContext {
    std::string client_id;
    std::vector<FileEntry> expected_files;
};

class SessionRegistry {
public:
    void update(const std::string& client_id, const std::vector<FileEntry>& files) {
        std::scoped_lock lock(mutex_);
        contexts_[client_id] = TransferContext{client_id, files};
    }

    std::vector<FileEntry> expected(const std::string& client_id) const {
        std::scoped_lock lock(mutex_);
        const auto it = contexts_.find(client_id);
        if (it == contexts_.end()) {
            return {};
        }
        return it->second.expected_files;
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, TransferContext> contexts_;
};

class ControlSession : public FramedSession {
public:
    ControlSession(tcp::socket socket, const ServerConfig& config, SessionRegistry& registry)
        : FramedSession(std::move(socket)), config_(config), registry_(registry) {}

    void start_with_hello(HelloMessage hello) {
        hello_ = std::move(hello);
        if (hello_.protocol_version != kProtocolVersion) {
            write_error("Protocol version mismatch");
            return;
        }
        read_file_list();
    }

private:
    void read_file_list() {
        async_read_frame([self = std::static_pointer_cast<ControlSession>(shared_from_this())](
                             const boost::system::error_code& error,
                             const DecodedFrame& frame) {
            if (error) {
                return;
            }
            if (frame.type != MessageType::file_list) {
                self->write_error("Expected FileList");
                return;
            }
            try {
                self->file_list_ = deserialize_file_list(frame.payload);
                self->registry_.update(self->hello_.client_id, self->file_list_.files);
                const auto client_root = self->config_.storage_root / self->hello_.client_id;
                ensure_directory(client_root);
                const auto server_files = scan_top_level_files(client_root);
                SyncPlanMessage plan{compute_upload_plan(self->file_list_.files, server_files)};
                self->async_write_message(
                    MessageType::sync_plan,
                    serialize_sync_plan(plan),
                    [self](const boost::system::error_code& write_error, std::size_t /*bytes*/) {
                        if (write_error) {
                            return;
                        }
                        self->read_sync_complete();
                    });
            } catch (const std::exception& ex) {
                self->write_error(ex.what());
            }
        });
    }

    void read_sync_complete() {
        async_read_frame([self = std::static_pointer_cast<ControlSession>(shared_from_this())](
                             const boost::system::error_code& error,
                             const DecodedFrame& frame) {
            if (error) {
                return;
            }
            if (frame.type != MessageType::sync_complete) {
                self->write_error("Expected SyncComplete");
                return;
            }
            try {
                const SyncCompleteMessage completion = deserialize_sync_complete(frame.payload);
                if (!completion.ok) {
                    self->write_error("Client reported unsuccessful sync: " + completion.details);
                    return;
                }
                remove_missing_server_files(
                    self->config_.storage_root / self->hello_.client_id,
                    self->file_list_.files);
                self->async_write_message(
                    MessageType::sync_complete,
                    serialize_sync_complete(SyncCompleteMessage{true, "Sync finished"}),
                    [](const boost::system::error_code&, std::size_t) {});
            } catch (const std::exception& ex) {
                self->write_error(ex.what());
            }
        });
    }

    void write_error(const std::string& details) {
        async_write_message(
            MessageType::error,
            serialize_error(ErrorMessage{details}),
            [](const boost::system::error_code&, std::size_t) {});
    }

    ServerConfig config_;
    SessionRegistry& registry_;
    HelloMessage hello_;
    FileListMessage file_list_;
};

class TransferSession : public FramedSession {
public:
    TransferSession(tcp::socket socket, const ServerConfig& config)
        : FramedSession(std::move(socket)), config_(config) {}

    void start_with_request(TransferRequestMessage request) {
        request_ = std::move(request);
        receive_file_bytes();
    }

private:
    void receive_file_bytes() {
        try {
            final_path_ =
                make_safe_storage_path(config_.storage_root, request_.client_id, request_.relative_path);
            ensure_directory(final_path_.parent_path());
            temp_path_ = temp_file_path(final_path_);
            output_.open(temp_path_, std::ios::binary | std::ios::trunc);
            if (!output_) {
                write_result(TransferStatus::write_failed, "Failed to open temp file");
                return;
            }
        } catch (const std::exception& ex) {
            write_result(TransferStatus::invalid_path, ex.what());
            return;
        }
        read_chunk();
    }

    void read_chunk() {
        if (bytes_received_ >= request_.size) {
            finish_transfer();
            return;
        }

        const std::size_t chunk_size = static_cast<std::size_t>(
            std::min<std::uint64_t>(buffer_.size(), request_.size - bytes_received_));
        auto self = std::static_pointer_cast<TransferSession>(shared_from_this());
        socket().async_read_some(
            boost::asio::buffer(buffer_.data(), chunk_size),
            [this, self](const boost::system::error_code& error, std::size_t bytes) {
                if (error) {
                    write_result(TransferStatus::write_failed, "Connection interrupted during upload");
                    return;
                }
                output_.write(buffer_.data(), static_cast<std::streamsize>(bytes));
                if (!output_) {
                    write_result(TransferStatus::write_failed, "Failed to write incoming bytes");
                    return;
                }
                bytes_received_ += bytes;
                read_chunk();
            });
    }

    void finish_transfer() {
        output_.close();
        try {
            const std::uint32_t actual_checksum = crc32_file(temp_path_);
            if (actual_checksum != request_.checksum) {
                std::filesystem::remove(temp_path_);
                write_result(TransferStatus::checksum_mismatch, "Checksum mismatch");
                return;
            }
            replace_atomically(temp_path_, final_path_);
            write_result(TransferStatus::ok, "Upload complete");
        } catch (const std::exception& ex) {
            write_result(TransferStatus::write_failed, ex.what());
        }
    }

    void write_result(TransferStatus status, const std::string& details) {
        async_write_message(
            MessageType::transfer_result,
            serialize_transfer_result(TransferResultMessage{
                request_.transfer_id,
                status,
                bytes_received_,
                details}),
            [](const boost::system::error_code&, std::size_t) {});
    }

    ServerConfig config_;
    TransferRequestMessage request_;
    std::filesystem::path final_path_;
    std::filesystem::path temp_path_;
    std::ofstream output_;
    std::array<char, 64 * 1024> buffer_{};
    std::uint64_t bytes_received_ = 0;
};

class Server {
public:
    explicit Server(const ServerConfig& config)
        : config_(config),
          io_context_(static_cast<int>(config.io_threads)),
          acceptor_(io_context_) {}

    void run() {
        ensure_directory(config_.storage_root);
        tcp::resolver resolver(io_context_);
        const auto endpoints = resolver.resolve(config_.listen_host, std::to_string(config_.listen_port));
        const tcp::endpoint endpoint = *endpoints.begin();
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(tcp::acceptor::reuse_address(true));
        acceptor_.bind(endpoint);
        acceptor_.listen();

        do_accept();

        std::vector<std::thread> workers;
        workers.reserve(config_.io_threads);
        for (std::uint32_t i = 0; i < config_.io_threads; ++i) {
            workers.emplace_back([this]() { io_context_.run(); });
        }
        for (auto& worker : workers) {
            worker.join();
        }
    }

private:
    void do_accept() {
        acceptor_.async_accept([this](const boost::system::error_code& error, tcp::socket socket) {
            if (!error) {
                route_connection(std::move(socket));
            }
            do_accept();
        });
    }

    void route_connection(tcp::socket socket) {
        auto session = std::make_shared<FramedSession>(std::move(socket));
        session->async_read_frame(
            [this, session](const boost::system::error_code& error, const DecodedFrame& frame) {
                if (error) {
                    return;
                }

                try {
                    switch (frame.type) {
                        case MessageType::hello: {
                            const HelloMessage hello = deserialize_hello(frame.payload);
                            auto control = std::make_shared<ControlSession>(
                                std::move(session->socket()),
                                config_,
                                registry_);
                            control->start_with_hello(hello);
                            break;
                        }
                        case MessageType::transfer_request: {
                            auto transfer = std::make_shared<TransferSession>(
                                std::move(session->socket()),
                                config_);
                            transfer->start_with_request(deserialize_transfer_request(frame.payload));
                            break;
                        }
                        default:
                            break;
                    }
                } catch (const std::exception& ex) {
                    auto error_session = session;
                    error_session->async_write_message(
                        MessageType::error,
                        serialize_error(ErrorMessage{ex.what()}),
                        [](const boost::system::error_code&, std::size_t) {});
                }
            });
    }

    ServerConfig config_;
    boost::asio::io_context io_context_;
    tcp::acceptor acceptor_;
    SessionRegistry registry_;
};

}  // namespace

int run_server(const ServerConfig& config) {
    std::cout << "Starting server on " << config.listen_host << ':' << config.listen_port << '\n';
    Server server(config);
    server.run();
    return 0;
}

}  // namespace mydrive
