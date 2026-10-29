#include "server/net/connection.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

#include "server/util/metrics.h"

namespace chatbox {

namespace {
constexpr std::size_t kWriteBufferHardLimit = 2 * 1024 * 1024;
}  // namespace

Connection::Connection(int fd, uint64_t id, int io_thread_id, std::string peer_ip,
                       bool proxy_protocol)
    : fd_(fd),
      id_(id),
      io_thread_id_(io_thread_id),
      peer_ip_(std::move(peer_ip)),
      proxy_header_pending_(proxy_protocol),
      last_active_(std::chrono::steady_clock::now()) {}

Connection::~Connection() {
    close();
}

int Connection::fd() const {
    return fd_;
}

uint64_t Connection::id() const {
    return id_;
}

int Connection::ioThreadId() const {
    return io_thread_id_;
}

const std::string& Connection::peerIp() const {
    return peer_ip_;
}

bool Connection::proxyHeaderPending() const {
    return proxy_header_pending_;
}

bool Connection::tryParseProxyHeader(std::string& error) {
    if (!proxy_header_pending_) {
        return true;
    }
    const char* data = input_.data();
    const std::size_t size = input_.size();
    if (data == nullptr || size == 0) {
        return true;
    }

    static const unsigned char kV2Signature[12] = {0x0D, 0x0A, 0x0D, 0x0A, 0x00,
                                                   0x0D, 0x0A, 0x51, 0x55, 0x49,
                                                   0x54, 0x0A};
    if (size < 12) {
        return true;
    }

    if (std::memcmp(data, kV2Signature, sizeof(kV2Signature)) == 0) {
        if (size < 16) {
            return true;
        }
        const auto* bytes = reinterpret_cast<const unsigned char*>(data);
        const unsigned char version_command = bytes[12];
        const unsigned char family = bytes[13];
        const std::uint16_t address_length =
            static_cast<std::uint16_t>((bytes[14] << 8) | bytes[15]);
        const std::size_t total = 16 + address_length;
        if (size < total) {
            return true;
        }
        const unsigned char command = version_command & 0x0F;
        if (command != 0 && command != 1) {
            error = "invalid PROXY v2 command";
            return false;
        }
        if (command == 1) {
            char text[INET6_ADDRSTRLEN] = {0};
            if (family == 0x11 && address_length >= 12) {
                if (::inet_ntop(AF_INET, bytes + 16, text, sizeof(text)) != nullptr) {
                    peer_ip_ = text;
                }
            } else if (family == 0x21 && address_length >= 36) {
                if (::inet_ntop(AF_INET6, bytes + 16, text, sizeof(text)) != nullptr) {
                    peer_ip_ = text;
                }
            }
        }
        input_.consume(total);
        proxy_header_pending_ = false;
        error.clear();
        return true;
    }

    // PROXY v1: "PROXY TCP4 src dst sport dport\r\n"
    if (size >= 6 && std::memcmp(data, "PROXY ", 6) != 0) {
        error = "invalid PROXY header";
        return false;
    }
    const std::size_t search_size = std::min<std::size_t>(size, 108);
    const char* line_end = static_cast<const char*>(
        std::memchr(data, '\n', search_size));
    if (line_end == nullptr) {
        if (size >= 108) {
            error = "PROXY header too long";
            return false;
        }
        return true;
    }
    const std::size_t line_length = static_cast<std::size_t>(line_end - data) + 1;
    std::string line(data, line_length);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    std::istringstream stream(line);
    std::string prefix;
    std::string protocol;
    std::string source;
    stream >> prefix >> protocol >> source;
    if (prefix != "PROXY" || (protocol != "TCP4" && protocol != "TCP6" &&
                              protocol != "UNKNOWN")) {
        error = "invalid PROXY v1 header";
        return false;
    }
    if (protocol != "UNKNOWN" && !source.empty()) {
        unsigned char address[16] = {0};
        const int family = protocol == "TCP4" ? AF_INET : AF_INET6;
        if (::inet_pton(family, source.c_str(), address) == 1) {
            peer_ip_ = source;
        }
    }
    input_.consume(line_length);
    proxy_header_pending_ = false;
    error.clear();
    return true;
}

bool Connection::allowMessage() {
    return message_bucket_.consume();
}

Buffer& Connection::input() {
    return input_;
}

bool Connection::enqueue(std::string frame) {
    if (closed_ || frame.empty()) {
        return !closed_;
    }
    if (output_bytes_ + frame.size() > kWriteBufferHardLimit) {
        return false;
    }
    output_bytes_ += frame.size();
    output_.push_back(Outbound{std::move(frame), 0});
    metrics().frames_out.fetch_add(1, std::memory_order_relaxed);
    return true;
}

bool Connection::flush() {
    while (!output_.empty()) {
        Outbound& front = output_.front();
        const char* data = front.data.data() + front.sent;
        const std::size_t length = front.data.size() - front.sent;
        const ssize_t written = ::send(fd_, data, length, MSG_NOSIGNAL);
        if (written > 0) {
            front.sent += static_cast<std::size_t>(written);
            output_bytes_ -= static_cast<std::size_t>(written);
            if (front.sent == front.data.size()) {
                output_.pop_front();
            }
            continue;
        }
        if (written == 0) {
            return false;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }
        return false;
    }
    return true;
}

void Connection::close() {
    if (closed_) {
        return;
    }
    closed_ = true;
    if (fd_ >= 0) {
        ::shutdown(fd_, SHUT_RDWR);
        ::close(fd_);
        fd_ = -1;
    }
}

bool Connection::closed() const {
    return closed_;
}

void Connection::touch() {
    last_active_ = std::chrono::steady_clock::now();
}

std::chrono::steady_clock::time_point Connection::lastActive() const {
    return last_active_;
}

std::size_t Connection::pendingOutputBytes() const {
    return output_bytes_;
}

void Connection::setAuth(uint64_t user_id, std::string nickname,
                         std::string token_hash, std::string session_id,
                         std::string device_id, uint32_t caps) {
    user_id_ = user_id;
    setNickname(std::move(nickname));
    token_hash_ = std::move(token_hash);
    session_id_ = std::move(session_id);
    device_id_ = std::move(device_id);
    caps_ = caps;
}

void Connection::setNickname(std::string nickname) {
    nickname_ = std::move(nickname);
}

void Connection::clearAuth() {
    user_id_ = 0;
    nickname_.clear();
    token_hash_.clear();
    session_id_.clear();
    device_id_.clear();
    caps_ = 0;
}

uint64_t Connection::userId() const {
    return user_id_;
}

const std::string& Connection::nickname() const {
    return nickname_;
}

const std::string& Connection::tokenHash() const {
    return token_hash_;
}

const std::string& Connection::sessionId() const {
    return session_id_;
}

const std::string& Connection::deviceId() const {
    return device_id_;
}

uint32_t Connection::caps() const {
    return caps_;
}

bool Connection::authenticated() const {
    return user_id_ != 0 && !token_hash_.empty();
}

void Connection::setCloseAfterFlush(bool value) {
    close_after_flush_ = value;
}

bool Connection::closeAfterFlush() const {
    return close_after_flush_;
}

}  // namespace chatbox
