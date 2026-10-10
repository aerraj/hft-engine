#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <hdr/hdr_histogram.h>
#include <ostream>
#include <string_view>
namespace hft {
// Upstream HdrHistogram_c, three significant figures; allocation occurs at startup.
class Histogram {
    hdr_histogram *histogram_ = nullptr;
    std::uint64_t max_ = 0, dropped_ = 0;

  public:
    Histogram() {
        if (hdr_init(1, 3'600'000'000'000LL, 3, &histogram_) != 0)
            std::abort();
    }
    ~Histogram() {
        hdr_close(histogram_);
    }
    Histogram(const Histogram &) = delete;
    Histogram &operator=(const Histogram &) = delete;
    void reset() noexcept {
        hdr_reset(histogram_);
        max_ = dropped_ = 0;
    }
    void copy_from(const Histogram &other) noexcept {
        hdr_reset(histogram_);
        hdr_add(histogram_, other.histogram_);
        max_ = other.max_;
        dropped_ = other.dropped_;
    }
    void record(std::uint64_t n) noexcept {
        if (n > 3'600'000'000'000ULL || !hdr_record_value(histogram_, static_cast<std::int64_t>(n)))
            ++dropped_;
        if (n > max_)
            max_ = n;
    }
    std::uint64_t count() const noexcept {
        return static_cast<std::uint64_t>(histogram_->total_count);
    }
    std::uint64_t quantile(double p) const noexcept {
        return static_cast<std::uint64_t>(hdr_value_at_percentile(histogram_, p * 100));
    }
    void json(std::ostream &out) const {
        out << "{\"samples\":" << count() << ",\"p50_ns\":" << quantile(.5)
            << ",\"p90_ns\":" << quantile(.9) << ",\"p99_ns\":" << quantile(.99)
            << ",\"p999_ns\":" << quantile(.999) << ",\"max_ns\":" << max_
            << ",\"out_of_range\":" << dropped_ << '}';
    }
    void csv(std::ostream &out, std::string_view stage) const {
        for (int i = 0; i < histogram_->counts_len; ++i) {
            auto count = hdr_count_at_index(histogram_, i);
            if (count)
                out << stage << ',' << hdr_value_at_index(histogram_, i) << ',' << count << '\n';
        }
    }
};
struct Metrics {
    enum Stage { Parse, BookUpdate, Signal, RiskCheck, Encode, TickToTrade, Total, Count };
    static constexpr std::array<std::string_view, Count> names = {
        "parse", "book", "signal", "risk", "encode", "tick_to_trade", "total_event"};
    std::array<Histogram, Count> hist{};
    void reset() noexcept {
        for (auto &h : hist)
            h.reset();
    }
    void copy_from(const Metrics &other) noexcept {
        for (unsigned i = 0; i < Count; ++i)
            hist[i].copy_from(other.hist[i]);
    }
    void json(std::ostream &o) const {
        o << '{';
        for (unsigned i = 0; i < Count; ++i) {
            if (i)
                o << ',';
            o << '"' << names[i] << "\":";
            hist[i].json(o);
        }
        o << '}';
    }
    void csv(std::ostream &o) const {
        o << "stage,lower_ns,count\n";
        for (unsigned i = 0; i < Count; ++i)
            hist[i].csv(o, names[i]);
    }
};
} // namespace hft
