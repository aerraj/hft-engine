#pragma once
#include "hft/core/types.hpp"
#include <cstdint>
#include <vector>
namespace hft {
class Index {
    struct Entry {
        OrderId key{};
        std::uint32_t value{};
    };
    std::vector<Entry> slots_;
    std::size_t mask_;

  public:
    explicit Index(std::size_t capacity)
        : slots_([&] {
              std::size_t n = 2;
              while (n < capacity * 2)
                  n *= 2;
              return n;
          }()),
          mask_(slots_.size() - 1) {}
    std::uint32_t get(OrderId id) const noexcept {
        auto k = hash64(id) & mask_;
        while (slots_[k].value) {
            if (slots_[k].key == id)
                return slots_[k].value;
            k = (k + 1) & mask_;
        }
        return 0;
    }
    void insert(OrderId id, std::uint32_t v) noexcept {
        auto k = hash64(id) & mask_;
        while (slots_[k].value)
            k = (k + 1) & mask_;
        slots_[k] = {id, v};
    }
    void erase(OrderId id) noexcept {
        auto hole = hash64(id) & mask_;
        while (slots_[hole].value && slots_[hole].key != id)
            hole = (hole + 1) & mask_;
        if (!slots_[hole].value)
            return;
        auto k = (hole + 1) & mask_;
        while (slots_[k].value) {
            auto home = hash64(slots_[k].key) & mask_;
            if (((k - home) & mask_) >= ((k - hole) & mask_)) {
                slots_[hole] = slots_[k];
                hole = k;
            }
            k = (k + 1) & mask_;
        }
        slots_[hole] = {};
    }
    void reset() noexcept {
        for (auto &s : slots_)
            s = {};
    }
};
} // namespace hft
