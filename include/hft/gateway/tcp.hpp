#pragma once
#include "hft/gateway/session.hpp"
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
namespace hft {
// POSIX loopback-only transport for simulator sessions. Actual exchange connectivity
// deliberately excluded. Login and reconnect preserve Soup sequence and caller tokens.
class TcpGateway {
    int fd_ = -1;
    SoupSession session_;
    SoupStream stream_;
    std::array<Wire, 512> tx_{};
    std::size_t head_ = 0, tail_ = 0, offset_ = 0;
    bool enqueue(Wire w) noexcept {
        if (!w.size || head_ - tail_ == tx_.size())
            return false;
        tx_[head_++ % tx_.size()] = w;
        return true;
    }

  public:
    ~TcpGateway() {
        disconnect();
    }
    TcpGateway() = default;
    TcpGateway(const TcpGateway &) = delete;
    TcpGateway &operator=(const TcpGateway &) = delete;
    void disconnect() noexcept {
        if (fd_ >= 0)
            close(fd_);
        fd_ = -1;
        session_.disconnected();
        stream_.reset();
        head_ = tail_ = offset_ = 0;
    }
    bool connect_loopback(std::uint16_t port, std::string_view user, std::string_view password,
                          Time now) noexcept {
        disconnect();
        fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0)
            return false;
#ifdef SO_NOSIGPIPE
        int yes = 1;
        setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(fd_, reinterpret_cast<sockaddr *>(&a), sizeof(a)) != 0) {
            disconnect();
            return false;
        }
        auto flags = fcntl(fd_, F_GETFL, 0);
        if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) != 0) {
            disconnect();
            return false;
        }
        return enqueue(session_.login(user, password, now));
    }
    bool send(const Wire &w) noexcept {
        return session_.state() == SoupSession::State::Active && enqueue(SoupSession::frame(w));
    }
    SoupSession::State state() const noexcept {
        return session_.state();
    }
    std::uint64_t next() const noexcept {
        return session_.next();
    }
    template <class F> bool poll(Time now, F &&consume) noexcept {
        if (fd_ < 0 || session_.timed_out(now))
            return false;
        auto heartbeat = session_.heartbeat(now);
        if (heartbeat.size && !enqueue(heartbeat))
            return false;
        while (tail_ < head_) {
            auto &w = tx_[tail_ % tx_.size()];
            int flags = 0;
#ifdef MSG_NOSIGNAL
            flags = MSG_NOSIGNAL;
#endif
            auto n = ::send(fd_, w.bytes.data() + offset_, w.size - offset_, flags);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                if (errno == EINTR)
                    continue;
                return false;
            }
            if (!n)
                return false;
            offset_ += static_cast<std::size_t>(n);
            if (offset_ == w.size) {
                ++tail_;
                offset_ = 0;
            }
        }
        std::array<unsigned char, 4096> buffer{};
        while (true) {
            auto n = recv(fd_, buffer.data(), buffer.size(), 0);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                if (errno == EINTR)
                    continue;
                return false;
            }
            if (!n)
                return false;
            if (!stream_.feed({buffer.data(), static_cast<std::size_t>(n)},
                              [&](auto frame) { return session_.receive(frame, now, consume); }))
                return false;
        }
        return true;
    }
};
} // namespace hft
