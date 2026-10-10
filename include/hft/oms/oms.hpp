#pragma once
#include "hft/core/types.hpp"
#include <algorithm>
#include <vector>
namespace hft {
enum class State : std::uint8_t {
    New,
    PendingAck,
    Live,
    PartiallyFilled,
    Filled,
    Cancelled,
    Rejected
};
inline bool terminal(State s) noexcept {
    return s == State::Filled || s == State::Cancelled || s == State::Rejected;
}
struct Account {
    std::int64_t position = 0, cash = 0, basis = 0, realized = 0;
    std::int64_t equity(Price mark) const noexcept {
        return cash + position * mark;
    }
    std::int64_t unrealized(Price mark) const noexcept {
        return position >= 0 ? position * mark - basis : position * mark + basis;
    }
    void fill(Side side, Qty qty, Price price) noexcept {
        auto signed_qty = side == Side::Buy ? std::int64_t(qty) : -std::int64_t(qty);
        cash -= signed_qty * price;
        if (!position || (position > 0) == (signed_qty > 0)) {
            basis += std::int64_t(qty) * price;
            position += signed_qty;
            return;
        }
        auto abs_pos = position > 0 ? position : -position;
        auto close = std::min(abs_pos, std::int64_t(qty));
        auto cost = (basis / abs_pos) * close + (basis % abs_pos) * close / abs_pos;
        realized += position > 0 ? close * price - cost : cost - close * price;
        basis -= cost;
        auto new_pos = position + signed_qty;
        if (std::int64_t(qty) > abs_pos)
            basis = (std::int64_t(qty) - abs_pos) * price;
        position = new_pos;
    }
};
class Oms {
  public:
    static constexpr std::size_t capacity = 256;
    struct Record {
        Order order{};
        State state = State::Cancelled;
        Qty filled{};
        bool cancel_pending = false;
        Time sent{};
        std::uint16_t prev = 256, next = 256;
        OrderId replaces{};
    };

  private:
    std::array<Record, capacity> records_{};
    std::vector<Account> accounts_;
    OrderId next_ = 1;
    std::uint16_t head_ = 256;
    std::size_t active_ = 0;
    void retire(Record &r) noexcept {
        auto idx = static_cast<std::uint16_t>(&r - records_.data());
        if (r.prev < 256)
            records_[r.prev].next = r.next;
        else
            head_ = r.next;
        if (r.next < 256)
            records_[r.next].prev = r.prev;
        r.prev = r.next = 256;
        --active_;
        r.cancel_pending = false;
        (void)idx;
    }

  public:
    std::uint64_t submitted = 0, filled_orders = 0, fill_count = 0, cancelled = 0, rejected = 0;
    std::uint64_t fill_qty = 0;
    Oms() : accounts_(65536) {}
    Record *get(OrderId id) noexcept {
        auto &r = records_[id % capacity];
        return r.order.id == id ? &r : nullptr;
    }
    const Record *get(OrderId id) const noexcept {
        auto &r = records_[id % capacity];
        return r.order.id == id ? &r : nullptr;
    }
    OrderId submit(Order o, Time now) noexcept {
        if (active_ == capacity)
            return 0;
        while (!terminal(records_[next_ % capacity].state))
            ++next_;
        o.id = next_++;
        auto idx = static_cast<std::uint16_t>(o.id % capacity);
        auto &r = records_[idx];
        r = {o, State::PendingAck, 0, false, now, 256, head_};
        if (head_ < 256)
            records_[head_].prev = idx;
        head_ = idx;
        ++active_;
        ++submitted;
        return o.id;
    }
    OrderId begin_replace(OrderId old, Order replacement, Time now) noexcept {
        auto *r = get(old);
        if (!r || terminal(r->state) || r->cancel_pending ||
            replacement.locate != r->order.locate || replacement.side != r->order.side)
            return 0;
        auto id = submit(replacement, now);
        if (!id)
            return 0;
        r->cancel_pending = true;
        get(id)->replaces = old;
        return id;
    }
    bool ack(OrderId id) noexcept {
        auto *r = get(id);
        if (!r || terminal(r->state))
            return false;
        if (r->state == State::PendingAck)
            r->state = r->filled ? State::PartiallyFilled : State::Live;
        return true;
    }
    bool request_cancel(OrderId id) noexcept {
        auto *r = get(id);
        if (!r || terminal(r->state) || r->cancel_pending)
            return false;
        r->cancel_pending = true;
        return true;
    }
    bool cancel(OrderId id) noexcept {
        auto *r = get(id);
        if (!r || terminal(r->state))
            return false;
        r->state = State::Cancelled;
        retire(*r);
        ++cancelled;
        return true;
    }
    bool reject(OrderId id) noexcept {
        auto *r = get(id);
        if (!r || r->state != State::PendingAck || r->filled)
            return false;
        r->state = State::Rejected;
        retire(*r);
        ++rejected;
        return true;
    }
    bool fill(const Fill &f) noexcept {
        auto *r = get(f.id);
        if (!r || terminal(r->state) || !f.qty || f.qty > r->order.qty - r->filled ||
            f.price <= 0 || f.price > max_price)
            return false;
        r->filled += f.qty;
        accounts_[r->order.locate].fill(r->order.side, f.qty, f.price);
        ++fill_count;
        fill_qty += f.qty;
        if (r->filled == r->order.qty) {
            r->state = State::Filled;
            retire(*r);
            ++filled_orders;
        } else
            r->state = State::PartiallyFilled;
        return true;
    }
    Account &account(Locate loc) noexcept {
        return accounts_[loc];
    }
    const Account &account(Locate loc) const noexcept {
        return accounts_[loc];
    }
    std::size_t active() const noexcept {
        return active_;
    }
    template <class F> void each(F &&f) noexcept {
        auto i = head_;
        while (i < 256) {
            auto next = records_[i].next;
            f(records_[i]);
            i = next;
        }
    }
    template <class F> void each(F &&f) const noexcept {
        auto i = head_;
        while (i < 256) {
            auto next = records_[i].next;
            f(records_[i]);
            i = next;
        }
    }
    std::uint64_t pending(Locate loc, Side s) const noexcept {
        std::uint64_t q = 0;
        each([&](const Record &r) {
            if (r.order.locate == loc && r.order.side == s)
                q += r.order.qty - r.filled;
        });
        return q;
    }
};
} // namespace hft
