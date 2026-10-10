#pragma once
#include "hft/gateway/encoder.hpp"
#include <algorithm>
#include <charconv>
#include <string_view>
namespace hft {
class SoupSession {
  public:
    enum class State { Disconnected, LoginSent, Active, Rejected, Ended };

  private:
    State state_ = State::Disconnected;
    std::array<char, 10> session_{};
    std::uint64_t next_ = 1;
    Time received_ = 0, sent_ = 0;
    static bool decimal(std::span<const unsigned char> b, std::uint64_t &v) noexcept {
        v = 0;
        bool digit = false;
        for (auto c : b) {
            if (c == ' ' && !digit)
                continue;
            if (c < '0' || c > '9' || v > (UINT64_MAX - (c - '0')) / 10)
                return false;
            digit = true;
            v = v * 10 + (c - '0');
        }
        return digit;
    }

  public:
    State state() const noexcept {
        return state_;
    }
    std::uint64_t next() const noexcept {
        return next_;
    }
    void disconnected() noexcept {
        state_ = State::Disconnected;
    }
    Wire login(std::string_view user, std::string_view password, Time now) noexcept {
        Wire w;
        if (user.size() > 6 || password.size() > 10)
            return w;
        w.size = 49;
        put_be(w.bytes, 0, 47, 2);
        w.bytes[2] = 'L';
        std::fill(w.bytes.begin() + 3, w.bytes.begin() + 49, ' ');
        std::copy(user.begin(), user.end(), w.bytes.begin() + 3);
        std::copy(password.begin(), password.end(), w.bytes.begin() + 9);
        if (session_[0])
            std::copy(session_.begin(), session_.end(), w.bytes.begin() + 19);
        std::array<char, 20> s{};
        auto r = std::to_chars(s.data(), s.data() + 20, next_);
        auto n = r.ptr - s.data();
        std::copy_n(s.begin(), n, w.bytes.begin() + 49 - n);
        state_ = State::LoginSent;
        received_ = sent_ = now;
        return w;
    }
    Wire heartbeat(Time now) noexcept {
        Wire w;
        if (state_ != State::Active || now - sent_ < 1'000'000'000)
            return w;
        w.size = 3;
        put_be(w.bytes, 0, 1, 2);
        w.bytes[2] = 'R';
        sent_ = now;
        return w;
    }
    bool timed_out(Time now) const noexcept {
        return state_ == State::Active && now - received_ > 5'000'000'000;
    }
    static Wire frame(const Wire &message) noexcept {
        Wire w;
        if (message.size > 125)
            return w;
        w.size = message.size + 3;
        put_be(w.bytes, 0, message.size + 1, 2);
        w.bytes[2] = 'U';
        std::copy_n(message.bytes.begin(), message.size, w.bytes.begin() + 3);
        return w;
    }
    template <class Consumer>
    bool receive(std::span<const unsigned char> b, Time now, Consumer &&consume) noexcept {
        if (b.size() < 3 || be(b, 0, 2) != b.size() - 2)
            return false;
        switch (b[2]) {
        case '+':
            received_ = now;
            return true;
        case 'A': {
            std::uint64_t n;
            if (state_ != State::LoginSent || b.size() != 33 || !decimal(b.subspan(13, 20), n) ||
                n != next_)
                return false;
            std::copy_n(b.begin() + 3, 10, session_.begin());
            state_ = State::Active;
            break;
        }
        case 'J':
            if (state_ != State::LoginSent || b.size() != 4)
                return false;
            state_ = State::Rejected;
            break;
        case 'H':
            if (state_ != State::Active || b.size() != 3)
                return false;
            break;
        case 'Z':
            if (state_ != State::Active || b.size() != 3)
                return false;
            state_ = State::Ended;
            break;
        case 'S':
            if (state_ != State::Active || !consume(b.subspan(3)))
                return false;
            ++next_;
            break;
        default:
            return false;
        }
        received_ = now;
        return true;
    }
};
// Framing supports TCP fragmentation/coalescing without heap allocation.
class SoupStream {
    std::array<unsigned char, 65537> buffer_{};
    std::size_t used_ = 0;

  public:
    template <class F> bool feed(std::span<const unsigned char> b, F &&consume) noexcept {
        for (auto c : b) {
            if (used_ == buffer_.size())
                return false;
            buffer_[used_++] = c;
            if (used_ >= 2) {
                auto n = be(buffer_, 0, 2) + 2;
                if (n < 3)
                    return false;
                if (used_ == n) {
                    if (!consume(std::span<const unsigned char>(buffer_.data(), used_)))
                        return false;
                    used_ = 0;
                }
            }
        }
        return true;
    }
    void reset() noexcept {
        used_ = 0;
    }
};
} // namespace hft
