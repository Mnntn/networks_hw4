#include "client/client_app.hpp"

#include "common/file_utils.hpp"
#include "common/protocol.hpp"

#include <boost/asio.hpp>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <thread>

#if defined(__linux__)
#include <poll.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace mydrive {

using boost::asio::ip::tcp;

namespace {

#if defined(__linux__)

bool wait_until_socket_writable(int socket_fd) {
    struct pollfd descriptor {
        socket_fd, POLLOUT, 0
    };

    while (true) {
        const int result = ::poll(&descriptor, 1, -1);
        if (result > 0) {
            return (descriptor.revents & (POLLOUT | POLLERR | POLLHUP)) != 0;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
}

std::string strerror_string(int error_number) {
    return std::strerror(error_number);
}

#endif

class FramedClient : public std::enable_shared_from_this<FramedClient> {
public:
    explicit FramedClient(boost::asio::io_context& io_context) : socket_(io_context), resolver_(io_context) {}

    tcp::socket& socket() { return socket_; }

    template <typename Handler>
    void async_connect(const std::string& host, std::uint16_t port, Handler&& handler) {
        auto self = shared_from_this();
        resolver_.async_resolve(
            host,
            std::to_string(port),
            [this, self, handler = std::forward<Handler>(handler)](
                const boost::system::error_code& error,
                tcp::resolver::results_type endpoints) mutable {
                if (error) {
                    handler(error);
                    return;
                }
                boost::asio::async_connect(
                    socket_,
                    endpoints,
                    [self, handler = std::move(handler)](
                        const boost::system::error_code& connect_error,
                        const tcp::endpoint&) mutable { handler(connect_error); });
            });
    }

    template <typename Handler>
    void async_write_message(MessageType type, const std::vector<std::uint8_t>& payload, Handler&& handler) {
        outbound_ = encode_frame(type, payload);
        auto self = shared_from_this();
        boost::asio::async_write(
            socket_,
            boost::asio::buffer(outbound_),
            [self, handler = std::forward<Handler>(handler)](
                const boost::system::error_code& error, std::size_t bytes) mutable {
                handler(error, bytes);
            });
    }

    template <typename Handler>
    void async_read_frame(Handler&& handler) {
        auto self = shared_from_this();
        boost::asio::async_read(
            socket_,
            boost::asio::buffer(header_),
            [this, self, handler = std::forward<Handler>(handler)](
                const boost::system::error_code& error,
                std::size_t /*header_bytes*/) mutable {
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
                            handler(boost::asio::error::operation_aborted, DecodedFrame{});
                        }
                    });
            });
    }

private:
    tcp::socket socket_;
    tcp::resolver resolver_;
    std::array<std::uint8_t, kFrameHeaderSize> header_{};
    std::vector<std::uint8_t> payload_;
    std::vector<std::uint8_t> outbound_;
};

std::string random_transfer_id() {
    std::ostringstream builder;
    builder << std::hex << std::chrono::steady_clock::now().time_since_epoch().count();
    static std::mt19937_64 engine{std::random_device{}()};
    builder << '-' << engine();
    return builder.str();
}

struct UploadTask {
    FileEntry file;
};

struct UploadResult {
    std::string path;
    bool ok = false;
    std::string details;
    double elapsed_seconds = 0.0;
};

class UploadConnection : public std::enable_shared_from_this<UploadConnection> {
public:
    UploadConnection(
        boost::asio::io_context& io_context,
        ClientConfig config,
        std::filesystem::path directory,
        UploadTask task,
        std::function<void(UploadResult)> on_done)
        : client_(std::make_shared<FramedClient>(io_context)),
          config_(std::move(config)),
          directory_(std::move(directory)),
          task_(std::move(task)),
          on_done_(std::move(on_done)),
          started_at_(std::chrono::steady_clock::now()) {}

    void start() {
        client_->async_connect(config_.server_host, config_.server_port, [self = shared_from_this()](
                                                                          const boost::system::error_code& error) {
            if (error) {
                self->finish(false, "Failed to connect upload socket");
                return;
            }
            self->send_request();
        });
    }

private:
    void send_request() {
        request_ = TransferRequestMessage{
            random_transfer_id(),
            config_.client_id,
            task_.file.relative_path,
            task_.file.size,
            task_.file.checksum,
            config_.dma_enabled};
        client_->async_write_message(
            MessageType::transfer_request,
            serialize_transfer_request(request_),
            [self = shared_from_this()](const boost::system::error_code& error, std::size_t /*bytes*/) {
                if (error) {
                    self->finish(false, "Failed to send TransferRequest");
                    return;
                }
                if (self->config_.dma_enabled) {
                    self->send_file_dma_or_fallback();
                } else {
                    self->send_file_buffered();
                }
            });
    }

    void send_file_buffered() {
        input_.open(directory_ / task_.file.relative_path, std::ios::binary);
        if (!input_) {
            finish(false, "Failed to open source file");
            return;
        }
        write_next_chunk();
    }

    void write_next_chunk() {
        input_.read(chunk_.data(), static_cast<std::streamsize>(chunk_.size()));
        const std::streamsize bytes = input_.gcount();
        if (bytes <= 0) {
            read_transfer_result();
            return;
        }

        auto self = shared_from_this();
        boost::asio::async_write(
            client_->socket(),
            boost::asio::buffer(chunk_.data(), static_cast<std::size_t>(bytes)),
            [this, self](const boost::system::error_code& error, std::size_t /*written*/) {
                if (error) {
                    finish(false, "Buffered upload failed");
                    return;
                }
                write_next_chunk();
            });
    }

    void send_file_dma_or_fallback() {
#if defined(__linux__)
        auto self = shared_from_this();
        std::thread([self]() {
            try {
                const std::filesystem::path path = self->directory_ / self->task_.file.relative_path;
                const int fd = ::open(path.c_str(), O_RDONLY);
                if (fd < 0) {
                    boost::asio::post(self->client_->socket().get_executor(), [self]() {
                        self->finish(false, "Failed to open file for sendfile");
                    });
                    return;
                }

                off_t offset = 0;
                std::uint64_t remaining = self->task_.file.size;
                while (remaining > 0) {
                    errno = 0;
                    const ssize_t sent = ::sendfile(
                        self->client_->socket().native_handle(),
                        fd,
                        &offset,
                        static_cast<size_t>(std::min<std::uint64_t>(remaining, 8 * 1024 * 1024)));

                    if (sent > 0) {
                        remaining -= static_cast<std::uint64_t>(sent);
                        continue;
                    }

                    if (sent == 0) {
                        ::close(fd);
                        boost::asio::post(self->client_->socket().get_executor(), [self]() {
                            self->finish(false, "sendfile returned 0 before transfer completion");
                        });
                        return;
                    }

                    const int error_number = errno;
                    if (error_number == EINTR) {
                        continue;
                    }
                    if (error_number == EAGAIN || error_number == EWOULDBLOCK) {
                        if (wait_until_socket_writable(self->client_->socket().native_handle())) {
                            continue;
                        }
                    }

                    const std::string details =
                        "sendfile failed: errno=" + std::to_string(error_number) +
                        " (" + strerror_string(error_number) + ")";
                    ::close(fd);
                    boost::asio::post(self->client_->socket().get_executor(), [self, details]() {
                        self->finish(false, details);
                    });
                    return;
                }
                ::close(fd);
                boost::asio::post(self->client_->socket().get_executor(), [self]() {
                    self->read_transfer_result();
                });
            } catch (...) {
                boost::asio::post(self->client_->socket().get_executor(), [self]() {
                    self->finish(false, "DMA path crashed");
                });
            }
        }).detach();
#else
        send_file_buffered();
#endif
    }

    void read_transfer_result() {
        client_->async_read_frame([self = shared_from_this()](
                                      const boost::system::error_code& error,
                                      const DecodedFrame& frame) {
            if (error) {
                self->finish(false, "Missing TransferResult");
                return;
            }
            if (frame.type != MessageType::transfer_result) {
                self->finish(false, "Unexpected response to upload");
                return;
            }
            const TransferResultMessage result = deserialize_transfer_result(frame.payload);
            self->finish(result.status == TransferStatus::ok, result.details);
        });
    }

    void finish(bool ok, const std::string& details) {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_at_);
        on_done_(UploadResult{
            task_.file.relative_path,
            ok,
            details,
            static_cast<double>(elapsed.count()) / 1000.0});
    }

    std::shared_ptr<FramedClient> client_;
    ClientConfig config_;
    std::filesystem::path directory_;
    UploadTask task_;
    TransferRequestMessage request_;
    std::function<void(UploadResult)> on_done_;
    std::ifstream input_;
    std::array<char, 64 * 1024> chunk_{};
    std::chrono::steady_clock::time_point started_at_;
};

class UploadManager : public std::enable_shared_from_this<UploadManager> {
public:
    UploadManager(
        boost::asio::io_context& io_context,
        ClientConfig config,
        std::filesystem::path directory,
        std::vector<FileEntry> files)
        : io_context_(io_context),
          config_(std::move(config)),
          directory_(std::move(directory)) {
        for (auto& file : files) {
            queue_.push_back(UploadTask{std::move(file)});
        }
    }

    std::future<std::vector<UploadResult>> run() {
        auto future = promise_.get_future();
        if (queue_.empty()) {
            promise_.set_value({});
            return future;
        }

        const std::size_t concurrency =
            std::min<std::size_t>(queue_.size(), static_cast<std::size_t>(config_.max_connections));
        for (std::size_t i = 0; i < concurrency; ++i) {
            launch_next();
        }
        return future;
    }

private:
    void launch_next() {
        if (next_index_ >= queue_.size()) {
            if (in_flight_ == 0) {
                promise_.set_value(results_);
            }
            return;
        }

        ++in_flight_;
        auto connection = std::make_shared<UploadConnection>(
            io_context_,
            config_,
            directory_,
            queue_[next_index_++],
            [self = shared_from_this()](UploadResult result) {
                self->results_.push_back(std::move(result));
                --self->in_flight_;
                self->launch_next();
            });
        connection->start();
    }

    boost::asio::io_context& io_context_;
    ClientConfig config_;
    std::filesystem::path directory_;
    std::vector<UploadTask> queue_;
    std::vector<UploadResult> results_;
    std::promise<std::vector<UploadResult>> promise_;
    std::size_t next_index_ = 0;
    std::size_t in_flight_ = 0;
};

class SyncClient : public std::enable_shared_from_this<SyncClient> {
public:
    explicit SyncClient(ClientConfig config)
        : config_(std::move(config)),
          io_context_(1),
          work_guard_(boost::asio::make_work_guard(io_context_)),
          control_(std::make_shared<FramedClient>(io_context_)) {}

    int run() {
        local_files_ = scan_top_level_files(config_.directory);
        control_->async_connect(config_.server_host, config_.server_port, [self = shared_from_this()](
                                                                          const boost::system::error_code& error) {
            if (error) {
                self->fail("Failed to connect control socket");
                return;
            }
            self->send_hello();
        });

        io_context_.run();
        return success_ ? 0 : 1;
    }

private:
    void send_hello() {
        control_->async_write_message(
            MessageType::hello,
            serialize_hello(HelloMessage{kProtocolVersion, config_.client_id}),
            [self = shared_from_this()](const boost::system::error_code& error, std::size_t /*bytes*/) {
                if (error) {
                    self->fail("Failed to send Hello");
                    return;
                }
                self->send_file_list();
            });
    }

    void send_file_list() {
        control_->async_write_message(
            MessageType::file_list,
            serialize_file_list(FileListMessage{local_files_}),
            [self = shared_from_this()](const boost::system::error_code& error, std::size_t /*bytes*/) {
                if (error) {
                    self->fail("Failed to send FileList");
                    return;
                }
                self->read_sync_plan();
            });
    }

    void read_sync_plan() {
        control_->async_read_frame([self = shared_from_this()](
                                       const boost::system::error_code& error,
                                       const DecodedFrame& frame) {
            if (error) {
                self->fail("Failed to read SyncPlan");
                return;
            }
            if (frame.type == MessageType::error) {
                self->fail(deserialize_error(frame.payload).details);
                return;
            }
            if (frame.type != MessageType::sync_plan) {
                self->fail("Unexpected control response");
                return;
            }
            self->sync_plan_ = deserialize_sync_plan(frame.payload);
            self->start_uploads();
        });
    }

    void start_uploads() {
        auto manager = std::make_shared<UploadManager>(
            io_context_,
            config_,
            config_.directory,
            sync_plan_.files_to_upload);

        auto future = manager->run();
        std::thread([self = shared_from_this(), future = std::move(future)]() mutable {
            const auto results = future.get();
            boost::asio::post(self->io_context_, [self, results]() {
                for (const auto& result : results) {
                    std::cout << "upload " << result.path << ": "
                              << (result.ok ? "ok" : "failed")
                              << " (" << result.elapsed_seconds << "s) "
                              << result.details << '\n';
                    if (!result.ok) {
                        self->success_ = false;
                    }
                }
                self->send_sync_complete();
            });
        }).detach();
    }

    void send_sync_complete() {
        const bool ok = success_;
        control_->async_write_message(
            MessageType::sync_complete,
            serialize_sync_complete(SyncCompleteMessage{
                ok,
                ok ? "All uploads finished" : "At least one upload failed"}),
            [self = shared_from_this()](const boost::system::error_code& error, std::size_t /*bytes*/) {
                if (error) {
                    self->fail("Failed to send SyncComplete");
                    return;
                }
                self->read_sync_complete_ack();
            });
    }

    void read_sync_complete_ack() {
        control_->async_read_frame([self = shared_from_this()](
                                       const boost::system::error_code& error,
                                       const DecodedFrame& frame) {
            if (error) {
                self->fail("Failed to read sync completion ack");
                return;
            }
            if (frame.type == MessageType::error) {
                self->fail(deserialize_error(frame.payload).details);
                return;
            }
            if (frame.type != MessageType::sync_complete) {
                self->fail("Unexpected final control frame");
                return;
            }
            const SyncCompleteMessage ack = deserialize_sync_complete(frame.payload);
            self->success_ = self->success_ && ack.ok;
            if (!ack.ok) {
                self->failure_reason_ = ack.details;
            }
            self->work_guard_.reset();
            self->io_context_.stop();
        });
    }

    void fail(const std::string& reason) {
        success_ = false;
        failure_reason_ = reason;
        std::cerr << "sync failed: " << reason << '\n';
        work_guard_.reset();
        io_context_.stop();
    }

    ClientConfig config_;
    boost::asio::io_context io_context_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_;
    std::shared_ptr<FramedClient> control_;
    std::vector<FileEntry> local_files_;
    SyncPlanMessage sync_plan_;
    bool success_ = true;
    std::string failure_reason_;
};

}  // namespace

int run_client_sync(const ClientConfig& config) {
    std::cout << "Scanning " << config.directory << '\n';
    auto client = std::make_shared<SyncClient>(config);
    return client->run();
}

}  // namespace mydrive
