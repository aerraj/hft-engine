#pragma once
#include "hft/core/clock.hpp"
#include "hft/core/ring.hpp"
#include "hft/gateway/encoder.hpp"
#include <atomic>
#include <fstream>
#include <thread>
namespace hft {
struct LogRecord {
    Time time{};
    unsigned char kind{};
    Wire wire{};
};
// One producer only. Overflow is explicit and must stop the engine, never silently drop.
class AsyncLogger {
    Ring<LogRecord, 65536> ring_;
    std::ofstream out_;
    std::thread worker_;
    std::atomic<bool> stop_{false}, failed_{false};
    void write(const LogRecord &r) {
        std::array<unsigned char, 11> header{};
        put_be(header, 0, r.time, 8);
        header[8] = r.kind;
        put_be(header, 9, r.wire.size, 2);
        out_.write(reinterpret_cast<const char *>(header.data()), header.size());
        out_.write(reinterpret_cast<const char *>(r.wire.bytes.data()), r.wire.size);
        if (!out_)
            failed_.store(true, std::memory_order_release);
    }
    void drain() {
        LogRecord r;
        while (!stop_.load(std::memory_order_acquire)) {
            if (ring_.pop(r))
                write(r);
            else
                std::this_thread::yield();
        }
        while (ring_.pop(r))
            write(r);
        out_.flush();
        if (!out_)
            failed_.store(true, std::memory_order_release);
    }

  public:
    explicit AsyncLogger(const char *path, int hot_core = -1) : out_(path, std::ios::binary) {
        if (!out_)
            failed_ = true;
        worker_ = std::thread([this, hot_core] {
            if (hot_core >= 0 && !exclude_core(static_cast<unsigned>(hot_core)))
                failed_.store(true);
            drain();
        });
    }
    ~AsyncLogger() {
        close();
    }
    bool ready(std::size_t reserve = 32) const noexcept {
        return ring_.free_space() >= reserve;
    }
    bool push(const LogRecord &r) noexcept {
        return !failed_.load(std::memory_order_acquire) && ring_.push(r);
    }
    void close() {
        if (worker_.joinable()) {
            stop_.store(true, std::memory_order_release);
            worker_.join();
        }
    }
    bool good() const noexcept {
        return !failed_.load(std::memory_order_acquire);
    }
};
} // namespace hft
