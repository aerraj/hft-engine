#pragma once
#include <array>
#include <atomic>
#include <cstddef>
namespace hft {
template <class T, std::size_t N> class Ring {
    static_assert(N > 1 && (N & (N - 1)) == 0);
    std::array<T, N> data{};
    alignas(64) std::atomic<std::size_t> head{0};
    alignas(64) std::atomic<std::size_t> tail{0};

  public:
    std::size_t free_space() const noexcept {
        return N - (head.load(std::memory_order_relaxed) - tail.load(std::memory_order_acquire));
    }
    bool push(const T &v) noexcept {
        auto h = head.load(std::memory_order_relaxed);
        if (h - tail.load(std::memory_order_acquire) == N)
            return false;
        data[h & (N - 1)] = v;
        head.store(h + 1, std::memory_order_release);
        return true;
    }
    bool pop(T &v) noexcept {
        auto t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire))
            return false;
        v = data[t & (N - 1)];
        tail.store(t + 1, std::memory_order_release);
        return true;
    }
};
} // namespace hft
