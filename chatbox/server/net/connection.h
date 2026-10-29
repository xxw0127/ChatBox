#ifndef CHATBOX_SERVER_NET_CONNECTION_H
#define CHATBOX_SERVER_NET_CONNECTION_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>

#include "server/net/buffer.h"
#include "server/util/token_bucket.h"

namespace chatbox {

class Connection : public std::enable_shared_from_this<Connection> {
public:
    using Ptr = std::shared_ptr<Connection>;

    Connection(int fd, uint64_t id, int io_thread_id, std::string peer_ip,
               bool proxy_protocol);
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    int fd() const;
    uint64_t id() const;
    int ioThreadId() const;
    const std::string& peerIp() const;
    bool proxyHeaderPending() const;
    bool tryParseProxyHeader(std::string& error);
    bool allowMessage();

    Buffer& input();

    // Returns false when the per-connection write buffer hard limit is reached.
    bool enqueue(std::string frame);
    // Returns false on a fatal send error; the caller owns the close decision.
    bool flush();

    void close();
    bool closed() const;

    void touch();
    std::chrono::steady_clock::time_point lastActive() const;
    std::size_t pendingOutputBytes() const;

    void setAuth(uint64_t user_id, std::string nickname, std::string token_hash,
                 std::string session_id = std::string(),
                 std::string device_id = std::string(), uint32_t caps = 0);
    void setNickname(std::string nickname);
    void clearAuth();
    uint64_t userId() const;
    const std::string& nickname() const;
    const std::string& tokenHash() const;
    const std::string& sessionId() const;
    const std::string& deviceId() const;
    uint32_t caps() const;
    bool authenticated() const;

    void setCloseAfterFlush(bool value);
    bool closeAfterFlush() const;

private:
    struct Outbound {
        std::string data;
        std::size_t sent = 0;
    };

    int fd_;
    uint64_t id_;
    int io_thread_id_;
    std::string peer_ip_;
    bool proxy_header_pending_ = false;
    Buffer input_;
    std::deque<Outbound> output_;
    std::size_t output_bytes_ = 0;
    bool closed_ = false;
    std::chrono::steady_clock::time_point last_active_;
    uint64_t user_id_ = 0;
    std::string nickname_;
    std::string token_hash_;
    std::string session_id_;
    std::string device_id_;
    uint32_t caps_ = 0;
    bool close_after_flush_ = false;
    TokenBucket message_bucket_{10.0, 20.0};
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_NET_CONNECTION_H
