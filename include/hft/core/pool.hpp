#pragma once
#include <cassert>
#include <cstdint>
#include <vector>
#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif
namespace hft {
// Index zero is null. Storage and freelist allocate once, before processing begins.
template <class T> class Pool {
    std::vector<T> data_;
    std::vector<std::uint32_t> free_;
    std::size_t used_ = 0;

  public:
    explicit Pool(std::uint32_t n) : data_(n + 1), free_(n) {
        for (std::uint32_t i = 0; i < n; ++i)
            free_[i] = n - i;
    }
    std::uint32_t acquire() noexcept {
        if (used_ == free_.size())
            return 0;
        auto i = free_[used_++];
        data_[i] = T{};
        return i;
    }
    void release(std::uint32_t i) noexcept {
        assert(i && used_);
        free_[--used_] = i;
    }
    T &operator[](std::uint32_t i) noexcept {
        return data_[i];
    }
    const T &operator[](std::uint32_t i) const noexcept {
        return data_[i];
    }
    bool huge_pages() noexcept {
#if defined(__linux__)
        auto page = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
        auto begin = reinterpret_cast<std::uintptr_t>(data_.data());
        auto aligned = begin & ~(page - 1);
        auto length = ((begin + data_.size() * sizeof(T) + page - 1) & ~(page - 1)) - aligned;
        return madvise(reinterpret_cast<void *>(aligned), length, MADV_HUGEPAGE) == 0;
#else
        return false;
#endif
    }
    std::size_t used() const noexcept {
        return used_;
    }
    std::size_t capacity() const noexcept {
        return free_.size();
    }
    void reset() noexcept {
        used_ = 0;
        for (std::uint32_t i = 0; i < free_.size(); ++i)
            free_[i] = static_cast<std::uint32_t>(free_.size() - i);
    }
};
} // namespace hft
