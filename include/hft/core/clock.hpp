#pragma once
#include <chrono>
#include <cstdint>
#include <thread>
#if defined(__x86_64__)
#include <cpuid.h>
#include <x86intrin.h>
#endif
#if defined(__linux__)
#include <sched.h>
#include <sys/mman.h>
#endif
namespace hft {
inline std::uint64_t monotonic_ns() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}
class Clock {
    double ns_per_tick_ = 1;
    bool tsc_ = false;
    static std::uint64_t cycles() noexcept {
#if defined(__x86_64__)
        _mm_lfence();
        auto t = __rdtsc();
        _mm_lfence();
        return t;
#else
        return monotonic_ns();
#endif
    }

  public:
    Clock() {
#if defined(__x86_64__)
        unsigned a, b, c, d;
        tsc_ = __get_cpuid(0x80000007, &a, &b, &c, &d) && (d & (1u << 8));
        if (tsc_) {
            auto n = monotonic_ns(), t = cycles();
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            auto end = cycles(), m = monotonic_ns();
            ns_per_tick_ = double(m - n) / double(end - t);
        }
#endif
    }
    std::uint64_t stamp() const noexcept {
        return tsc_ ? cycles() : monotonic_ns();
    }
    std::uint64_t elapsed(std::uint64_t a, std::uint64_t b) const noexcept {
        return static_cast<std::uint64_t>(double(b - a) * ns_per_tick_);
    }
    bool tsc() const noexcept {
        return tsc_;
    }
    double ns_per_tick() const noexcept {
        return ns_per_tick_;
    }
};
inline bool pin_core(unsigned core) noexcept {
#if defined(__linux__)
    if (core >= CPU_SETSIZE)
        return false;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    return sched_setaffinity(0, sizeof(set), &set) == 0;
#else
    (void)core;
    return false;
#endif
}
inline bool exclude_core(unsigned core) noexcept {
#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) != 0 || core >= CPU_SETSIZE)
        return false;
    CPU_CLR(core, &set);
    if (CPU_COUNT(&set) == 0)
        return false;
    return sched_setaffinity(0, sizeof(set), &set) == 0;
#else
    (void)core;
    return false;
#endif
}
inline bool lock_memory() noexcept {
#if defined(__linux__)
    return mlockall(MCL_CURRENT | MCL_FUTURE) == 0;
#else
    return false;
#endif
}
} // namespace hft
