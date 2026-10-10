#pragma once
#include "hft/core/clock.hpp"
#include "hft/core/ring.hpp"
#include "hft/risk/risk.hpp"
#include <atomic>
#include <charconv>
#include <cstring>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
namespace hft {
struct Command {
    enum Type { Kill, LimitsChange, Stats } type = Stats;
    Limits limits{};
};
struct ControlStats {
    std::atomic<std::uint64_t> messages{0}, orders{0};
    std::atomic<std::int64_t> position{0};
    std::atomic<bool> killed{false};
};
class Control {
    int fd_ = -1;
    std::string path_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    Ring<Command, 128> queue_;
    void run() {
        while (!stop_.load(std::memory_order_acquire)) {
            pollfd p{fd_, POLLIN, 0};
            if (poll(&p, 1, 50) <= 0)
                continue;
            std::array<char, 256> b{};
            sockaddr_un peer{};
            socklen_t n = sizeof(peer);
            auto count =
                recvfrom(fd_, b.data(), b.size() - 1, 0, reinterpret_cast<sockaddr *>(&peer), &n);
            if (count <= 0)
                continue;
            std::string_view request(b.data(), static_cast<std::size_t>(count));
            std::string reply;
            if (request == "stats")
                reply = "{\"messages\":" + std::to_string(stats.messages.load()) +
                        ",\"orders\":" + std::to_string(stats.orders.load()) +
                        ",\"position\":" + std::to_string(stats.position.load()) +
                        ",\"killed\":" + (stats.killed.load() ? "true" : "false") + "}";
            else {
                Command c;
                bool valid = false;
                if (request == "kill") {
                    c.type = Command::Kill;
                    valid = true;
                } else if (request.starts_with("limits ")) {
                    c.type = Command::LimitsChange;
                    std::array<std::uint64_t, 5> values{};
                    auto text = request.substr(7);
                    valid = true;
                    for (auto &v : values) {
                        auto r = std::from_chars(text.data(), text.data() + text.size(), v);
                        if (r.ec != std::errc{}) {
                            valid = false;
                            break;
                        }
                        text.remove_prefix(static_cast<std::size_t>(r.ptr - text.data()));
                        if (!text.empty() && text.front() == ' ')
                            text.remove_prefix(1);
                    }
                    if (!text.empty())
                        valid = false;
                    if (values[0] >= 1000000 || values[1] > 100000000 || values[2] > INT64_MAX ||
                        values[3] > max_price || values[4] > UINT32_MAX)
                        valid = false;
                    c.limits = {static_cast<Qty>(values[0]), static_cast<std::int64_t>(values[1]),
                                static_cast<std::int64_t>(values[2]), static_cast<Price>(values[3]),
                                static_cast<std::uint32_t>(values[4])};
                    Risk validator;
                    if (!validator.update(c.limits))
                        valid = false;
                }
                reply = !valid ? "invalid command" : queue_.push(c) ? "queued" : "queue full";
            }
            sendto(fd_, reply.data(), reply.size(), 0, reinterpret_cast<sockaddr *>(&peer), n);
        }
    }

  public:
    ControlStats stats;
    explicit Control(std::string path, int hot_core = -1) : path_(std::move(path)) {
        if (path_.size() >= sizeof(sockaddr_un::sun_path))
            return;
        fd_ = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (fd_ < 0)
            return;
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::memcpy(addr.sun_path, path_.c_str(), path_.size() + 1);
        if (bind(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
            close(fd_);
            fd_ = -1;
            return;
        }
        thread_ = std::thread([this, hot_core] {
            if (hot_core >= 0 && !exclude_core(static_cast<unsigned>(hot_core))) {
                stop_ = true;
                return;
            }
            run();
        });
    }
    ~Control() {
        stop_.store(true, std::memory_order_release);
        if (thread_.joinable())
            thread_.join();
        if (fd_ >= 0) {
            close(fd_);
            unlink(path_.c_str());
        }
    }
    bool good() const noexcept {
        return fd_ >= 0;
    }
    bool pop(Command &c) noexcept {
        return queue_.pop(c);
    }
};
} // namespace hft
