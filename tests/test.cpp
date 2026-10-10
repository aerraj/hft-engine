#include "hft/core/engine.hpp"
#include "hft/core/ring.hpp"
#include "hft/feed/recovery.hpp"
#include "hft/gateway/reports.hpp"
#include "hft/gateway/session.hpp"
#include "hft/gateway/tcp.hpp"
#include "hft/log/logger.hpp"
#include "support.hpp"
#include <filesystem>
#include <gtest/gtest.h>
#include <map>
#include <random>
#include <thread>
using namespace hft;
TEST(Parser, AllLengthsAndTruncations) {
    for (auto t : std::string_view("SRHYL VWKJhAFECXDUPQBIN")) {
        auto n = itch_size(static_cast<unsigned char>(t));
        if (!n)
            continue;
        auto b = test::packet(t);
        Event e;
        for (std::size_t i = 0; i < n; ++i)
            EXPECT_FALSE(parse({b.data(), i}, e));
        b.push_back(0);
        EXPECT_FALSE(parse(b, e));
    }
    Event e;
    std::array<unsigned char, 11> x{};
    x[0] = '?';
    EXPECT_FALSE(parse(x, e));
}
TEST(Parser, AddEndianAndAttribution) {
    auto b = test::add(0x123456789abcdefULL, Side::Sell, 200000, 42, 12345, 12);
    Event e;
    ASSERT_TRUE(parse(b, e));
    EXPECT_EQ(e.id, 0x123456789abcdefULL);
    EXPECT_EQ(e.locate, 12);
    EXPECT_EQ(e.time, 12345);
    EXPECT_EQ(e.qty, 42);
    EXPECT_EQ(e.side, Side::Sell);
    b[0] = 'F';
    b.resize(40);
    EXPECT_TRUE(parse(b, e));
    b[19] = '?';
    EXPECT_FALSE(parse(b, e));
}
TEST(Parser, ExecuteCancelDeleteReplaceTrade) {
    Event e;
    for (auto t : {'X', 'E', 'C', 'D'}) {
        auto b = test::remove(t, 99, 5);
        if (t == 'C') {
            b[31] = 'Y';
            put_be(b, 32, 1000, 4);
        }
        ASSERT_TRUE(parse(b, e));
        EXPECT_EQ(e.id, 99);
    }
    auto b = test::packet('U');
    put_be(b, 11, 1, 8);
    put_be(b, 19, 2, 8);
    put_be(b, 27, 10, 4);
    put_be(b, 31, 100, 4);
    ASSERT_TRUE(parse(b, e));
    EXPECT_EQ(e.replacement, 2);
    auto p = test::packet('P');
    put_be(p, 20, 10, 4);
    put_be(p, 32, 100, 4);
    ASSERT_TRUE(parse(p, e));
    EXPECT_EQ(e.kind, Kind::Trade);
    auto c = test::packet('Q');
    put_be(c, 27, 100, 4);
    EXPECT_TRUE(parse(c, e));
    EXPECT_FALSE(e.printable);
    auto broken = test::packet('B');
    put_be(broken, 11, 123, 8);
    ASSERT_TRUE(parse(broken, e));
    EXPECT_EQ(e.match, 123);
}
TEST(Parser, AdministrativeMessages) {
    for (char t : std::string_view("SRHYL VWKJhIN")) {
        if (!itch_size(static_cast<unsigned char>(t)))
            continue;
        auto p = test::packet(t);
        Event e;
        EXPECT_TRUE(parse(p, e)) << t;
    }
    auto b = test::add(1, Side::Buy, 100, 1);
    put_be(b, 5, 86400000000000ULL, 6);
    Event e;
    EXPECT_FALSE(parse(b, e));
}
TEST(Feed, GapLatchAndRecovery) {
    Sequence s;
    EXPECT_TRUE(s.accept(1));
    EXPECT_FALSE(s.accept(3));
    EXPECT_FALSE(s.accept(2));
    s.recover(4);
    EXPECT_TRUE(s.accept(4));
}
TEST(Feed, MoldAtomicValidation) {
    auto a = test::add(1, Side::Buy, 100, 1);
    std::vector<unsigned char> b(22 + a.size());
    put_be(b, 10, 1, 8);
    put_be(b, 18, 1, 2);
    put_be(b, 20, a.size(), 2);
    std::copy(a.begin(), a.end(), b.begin() + 22);
    Sequence s;
    int count = 0;
    EXPECT_TRUE(mold(b, s, [&](auto) {
        ++count;
        return true;
    }));
    EXPECT_EQ(count, 1);
    EXPECT_FALSE(mold(b, s, [&](auto) {
        ++count;
        return true;
    }));
    EXPECT_EQ(count, 1);
    s.recover(1);
    b.pop_back();
    EXPECT_FALSE(mold(b, s, [&](auto) {
        ++count;
        return true;
    }));
    EXPECT_EQ(s.next(), 1);
}
TEST(Book, FifoAndPartialCancellation) {
    Book b(8, 8);
    ASSERT_TRUE(b.apply(test::event(1, Side::Buy, 100, 10)));
    ASSERT_TRUE(b.apply(test::event(2, Side::Buy, 100, 5)));
    EXPECT_EQ(b.queue_ahead(2), 10);
    ASSERT_TRUE(b.apply(test::removal(Kind::Cancel, 1, 3)));
    EXPECT_EQ(b.queue_ahead(2), 7);
    EXPECT_FALSE(b.apply(test::removal(Kind::Execute, 1, 8)));
    EXPECT_EQ(b.depth(Side::Buy, 100), 12);
    ASSERT_TRUE(b.apply(test::removal(Kind::Delete, 1)));
    EXPECT_EQ(b.queue_ahead(2), 0);
    EXPECT_EQ(b.best(Side::Buy), 100);
}
TEST(Book, FarPricesRoutingAndPoolReuse) {
    Book b(2, 2);
    ASSERT_TRUE(b.apply(test::event(1, Side::Buy, 1, 10, 1)));
    ASSERT_TRUE(b.apply(test::event(2, Side::Sell, max_price, 10, 2)));
    EXPECT_FALSE(b.apply(test::event(3, Side::Buy, 2, 1)));
    EXPECT_EQ(b.best(Side::Sell, 2), max_price);
    EXPECT_FALSE(b.apply(test::removal(Kind::Delete, 1, 0, 0, 2)));
    ASSERT_TRUE(b.apply(test::removal(Kind::Delete, 1)));
    ASSERT_TRUE(b.apply(test::event(3, Side::Buy, 2, 1)));
    EXPECT_EQ(b.size(), 2);
}
TEST(Book, ReplaceLosesPriorityAndIsAtomic) {
    Book b(3, 1);
    ASSERT_TRUE(b.apply(test::event(1, Side::Buy, 100, 10)));
    ASSERT_TRUE(b.apply(test::event(2, Side::Buy, 100, 10)));
    auto e = test::removal(Kind::Replace, 1);
    e.replacement = 3;
    e.qty = 7;
    e.price = 101;
    EXPECT_FALSE(b.apply(e));
    EXPECT_NE(b.order(1), nullptr);
    e.price = 100;
    ASSERT_TRUE(b.apply(e));
    EXPECT_EQ(b.queue_ahead(3), 10);
    EXPECT_EQ(b.order(1), nullptr);
    e.id = 3;
    e.replacement = 2;
    EXPECT_FALSE(b.apply(e));
}
TEST(Book, PropertyAgainstMap) {
    Book b(1024, 1024);
    std::map<OrderId, Event> ref;
    std::mt19937 rng(12345);
    OrderId next = 1;
    for (int step = 0; step < 30000; ++step) {
        if (ref.empty() || (ref.size() < 500 && rng() % 3 == 0)) {
            auto e = test::event(next++, rng() % 2 ? Side::Buy : Side::Sell,
                                 100 + static_cast<Price>(rng() % 100), 1 + rng() % 100,
                                 static_cast<Locate>(1 + rng() % 3));
            ASSERT_TRUE(b.apply(e));
            ref.emplace(e.id, e);
        } else {
            auto it = ref.begin();
            std::advance(it, rng() % ref.size());
            auto q = 1 + rng() % it->second.qty;
            auto e = test::removal(rng() % 2 ? Kind::Execute : Kind::Cancel, it->first, q, 0,
                                   it->second.locate);
            ASSERT_TRUE(b.apply(e));
            it->second.qty -= q;
            if (!it->second.qty)
                ref.erase(it);
        }
        if (step % 100 == 0) {
            EXPECT_EQ(b.size(), ref.size());
            for (Locate loc = 1; loc <= 3; ++loc)
                for (auto side : {Side::Buy, Side::Sell}) {
                    Price best = 0;
                    std::map<Price, std::uint64_t> levels;
                    for (auto &[id, e] : ref) {
                        (void)id;
                        if (e.locate == loc && e.side == side) {
                            levels[e.price] += e.qty;
                            if (!best || (side == Side::Buy ? e.price > best : e.price < best))
                                best = e.price;
                        }
                    }
                    EXPECT_EQ(b.best(side, loc), best);
                    for (auto &[price, qty] : levels)
                        EXPECT_EQ(b.depth(side, price, loc), qty);
                }
        }
    }
}
TEST(Book, ConsumedLiquidityNotReused) {
    Book b(16, 16);
    ASSERT_TRUE(b.apply(test::event(1, Side::Sell, 100, 10)));
    EXPECT_EQ(b.consume(1, Side::Sell, 100, 8), 8);
    EXPECT_EQ(b.consume(1, Side::Sell, 100, 8), 2);
    EXPECT_EQ(b.available(Side::Sell, 100, 1), 0);
    ASSERT_TRUE(b.apply(test::removal(Kind::Execute, 1, 5)));
    EXPECT_EQ(b.available(Side::Sell, 100, 1), 0);
    ASSERT_TRUE(b.apply(test::event(2, Side::Sell, 100, 4)));
    EXPECT_EQ(b.available(Side::Sell, 100, 1), 4);
}
TEST(Oms, StateTransitionsAndCancelFillRace) {
    Oms o;
    auto id = o.submit({0, 1, Side::Buy, 100, 10}, 0);
    ASSERT_NE(id, 0);
    EXPECT_EQ(o.get(id)->state, State::PendingAck);
    ASSERT_TRUE(o.ack(id));
    ASSERT_TRUE(o.request_cancel(id));
    ASSERT_TRUE(o.fill({id, 4, 100, 1, false}));
    EXPECT_EQ(o.get(id)->state, State::PartiallyFilled);
    ASSERT_TRUE(o.ack(id));
    EXPECT_EQ(o.get(id)->state, State::PartiallyFilled);
    ASSERT_TRUE(o.cancel(id));
    EXPECT_FALSE(o.fill({id, 1, 100, 2, false}));
    EXPECT_EQ(o.account(1).position, 4);
    EXPECT_EQ(o.active(), 0);
}
TEST(Oms, FilledWinsAndRejectDoesNotReverseFill) {
    Oms o;
    auto id = o.submit({0, 1, Side::Buy, 100, 2}, 0);
    ASSERT_TRUE(o.fill({id, 1, 100, 1, false}));
    EXPECT_FALSE(o.reject(id));
    ASSERT_TRUE(o.fill({id, 1, 100, 2, false}));
    EXPECT_FALSE(o.cancel(id));
    EXPECT_FALSE(o.ack(id));
    EXPECT_EQ(o.get(id)->state, State::Filled);
    id = o.submit({0, 1, Side::Buy, 100, 2}, 0);
    EXPECT_TRUE(o.reject(id));
    EXPECT_EQ(o.get(id)->state, State::Rejected);
}
TEST(Oms, CapacityAndStaleIds) {
    Oms o;
    OrderId first = 0;
    for (unsigned i = 0; i < 256; ++i) {
        auto id = o.submit({0, 1, Side::Buy, 100, 1}, 0);
        ASSERT_NE(id, 0);
        if (!i)
            first = id;
    }
    EXPECT_EQ(o.submit({0, 1, Side::Buy, 100, 1}, 0), 0);
    ASSERT_TRUE(o.cancel(first));
    auto id = o.submit({0, 1, Side::Buy, 100, 1}, 0);
    EXPECT_GT(id, first);
    EXPECT_FALSE(o.fill({first, 1, 100, 1, false}));
}
TEST(Oms, LongShortRealizedAndUnrealized) {
    Account a;
    a.fill(Side::Buy, 10, 100);
    a.fill(Side::Sell, 4, 110);
    EXPECT_EQ(a.realized, 40);
    EXPECT_EQ(a.equity(110), 100);
    EXPECT_EQ(a.unrealized(110), 60);
    a.fill(Side::Sell, 10, 90);
    EXPECT_EQ(a.position, -4);
    EXPECT_EQ(a.realized, -20);
    EXPECT_EQ(a.basis, 360);
    a.fill(Side::Buy, 4, 80);
    EXPECT_EQ(a.position, 0);
    EXPECT_EQ(a.realized, 20);
    EXPECT_EQ(a.cash, 20);
}
TEST(Risk, OutstandingExposureKillAndSelfTrade) {
    Limits l;
    l.max_position = 10;
    l.max_notional = 100000;
    l.collar = 10;
    Risk r(l);
    Oms o;
    Order buy{0, 1, Side::Buy, 100, 6};
    EXPECT_EQ(r.check(buy, o, 100, 0), RiskResult::Pass);
    o.submit(buy, 0);
    EXPECT_EQ(r.check(buy, o, 100, 0), RiskResult::Position);
    Order sell{0, 1, Side::Sell, 100, 1};
    EXPECT_EQ(r.check(sell, o, 100, 0), RiskResult::SelfTrade);
    sell.price = 120;
    EXPECT_EQ(r.check(sell, o, 100, 0), RiskResult::Collar);
    r.kill();
    EXPECT_EQ(r.check(sell, o, 100, 0), RiskResult::Killed);
}
TEST(Risk, NotionalThrottleHaltAndLimits) {
    Limits l;
    l.max_notional = 99;
    Risk r(l);
    Oms o;
    Order x{0, 1, Side::Buy, 100, 1};
    EXPECT_EQ(r.check(x, o, 100, 0), RiskResult::Notional);
    l.max_notional = 10000;
    l.orders_per_second = 1;
    ASSERT_TRUE(r.update(l));
    EXPECT_EQ(r.check(x, o, 100, 0), RiskResult::Pass);
    r.committed();
    EXPECT_EQ(r.check(x, o, 100, 0), RiskResult::Rate);
    EXPECT_EQ(r.check(x, o, 100, 1000000000), RiskResult::Pass);
    EXPECT_EQ(r.check(x, o, 100, 1000000000, false), RiskResult::Halted);
    l.max_position = -1;
    EXPECT_FALSE(r.update(l));
}
TEST(Gateway, OuchGoldenOffsets) {
    Order o{123, 1, Side::Buy, 100001, 17, true};
    auto w = encode(o, test::symbol());
    EXPECT_EQ(w.size, 49);
    EXPECT_EQ(w.bytes[0], 'O');
    EXPECT_EQ(w.bytes[15], 'B');
    EXPECT_EQ(be(w.bytes, 16, 4), 17);
    EXPECT_EQ(be(w.bytes, 28, 4), 100001);
    EXPECT_EQ(be(w.bytes, 32, 4), 0);
    EXPECT_EQ(w.bytes[40], 'Y');
    Order parsed;
    Symbol symbol;
    ASSERT_TRUE(decode_order(w.view(), parsed, symbol));
    EXPECT_EQ(parsed.id, o.id);
    EXPECT_EQ(parsed.price, o.price);
    EXPECT_EQ(symbol, test::symbol());
    auto c = encode_cancel(123);
    EXPECT_EQ(c.size, 19);
    EXPECT_EQ(read_token(c.view(), 1), 123);
    auto r = encode_replace(123, {124, 1, Side::Buy, 100, 10});
    EXPECT_EQ(r.size, 47);
    EXPECT_EQ(read_token(r.view(), 15), 124);
    EXPECT_EQ(be(r.view(), 29, 4), 10);
}
TEST(Session, LoginSequenceRecoveryAndHeartbeat) {
    SoupSession s;
    auto login = s.login("user", "secret", 0);
    EXPECT_EQ(login.size, 49);
    std::array<unsigned char, 33> a{};
    put_be(a, 0, 31, 2);
    a[2] = 'A';
    std::fill(a.begin() + 3, a.end(), ' ');
    a[32] = '1';
    auto consume = [](auto) { return true; };
    ASSERT_TRUE(s.receive(a, 0, consume));
    std::array<unsigned char, 4> d{0, 2, 'S', 'x'};
    EXPECT_TRUE(s.receive(d, 1, consume));
    EXPECT_EQ(s.next(), 2);
    EXPECT_EQ(s.heartbeat(1000000001).size, 3);
    EXPECT_TRUE(s.timed_out(6000000002));
    s.disconnected();
    login = s.login("user", "secret", 10);
    EXPECT_EQ(login.bytes[48], '2');
    a[32] = '1';
    EXPECT_FALSE(s.receive(a, 11, consume));
    a[32] = '2';
    EXPECT_TRUE(s.receive(a, 11, consume));
}
TEST(Session, TcpFragmentationAndCoalescing) {
    SoupStream stream;
    std::array<unsigned char, 6> b{0, 1, 'H', 0, 1, 'H'};
    int frames = 0;
    for (auto c : b)
        ASSERT_TRUE(stream.feed({&c, 1}, [&](auto p) {
            ++frames;
            return p.size() == 3;
        }));
    EXPECT_EQ(frames, 2);
}
TEST(Simulator, AggressiveLiquidityAndLatency) {
    Book b(16, 16);
    ASSERT_TRUE(b.apply(test::event(1, Side::Sell, 100, 10)));
    Oms o;
    Exchange x({10, 20});
    auto id = o.submit({0, 1, Side::Buy, 100, 8, true}, 0);
    ASSERT_TRUE(x.submit(o.get(id)->order, 0));
    auto callback = [](const Fill &) {};
    EXPECT_TRUE(x.advance(29, b, o, callback));
    EXPECT_EQ(o.account(1).position, 0);
    EXPECT_TRUE(x.advance(40, b, o, callback));
    EXPECT_EQ(o.account(1).position, 8);
    id = o.submit({0, 1, Side::Buy, 100, 8, true}, 40);
    x.submit(o.get(id)->order, 40);
    ASSERT_TRUE(x.advance(80, b, o, callback));
    EXPECT_EQ(o.account(1).position, 10);
    EXPECT_EQ(o.get(id)->state, State::Cancelled);
}
TEST(Simulator, PassiveQueueAheadAndFifo) {
    Book b(16, 16);
    ASSERT_TRUE(b.apply(test::event(1, Side::Buy, 100, 10)));
    Oms o;
    Exchange x({0, 0});
    auto id = o.submit({0, 1, Side::Buy, 100, 3}, 0);
    x.submit(o.get(id)->order, 0);
    auto cb = [](const Fill &) {};
    ASSERT_TRUE(x.advance(0, b, o, cb));
    auto cancel = test::removal(Kind::Cancel, 1, 5, 1);
    x.on_event(cancel, b);
    ASSERT_TRUE(b.apply(cancel));
    auto exec = test::removal(Kind::Execute, 1, 5, 2);
    x.on_event(exec, b);
    ASSERT_TRUE(b.apply(exec));
    ASSERT_TRUE(x.advance(2, b, o, cb));
    EXPECT_EQ(o.account(1).position, 0);
    ASSERT_TRUE(b.apply(test::event(2, Side::Buy, 100, 10)));
    exec = test::removal(Kind::Execute, 2, 3, 3);
    x.on_event(exec, b);
    ASSERT_TRUE(b.apply(exec));
    ASSERT_TRUE(x.advance(3, b, o, cb));
    EXPECT_EQ(o.account(1).position, 3);
    EXPECT_EQ(o.get(id)->state, State::Filled);
}
TEST(Simulator, CancelBeforeFillAndFillBeforeCancel) {
    Book b(16, 16);
    b.apply(test::event(1, Side::Buy, 100, 1));
    Oms o;
    Exchange x({1, 1});
    auto id = o.submit({0, 1, Side::Buy, 100, 1}, 0);
    x.submit(o.get(id)->order, 0);
    auto cb = [](const Fill &) {};
    ASSERT_TRUE(x.advance(3, b, o, cb));
    o.request_cancel(id);
    x.cancel(o.get(id)->order, 3);
    ASSERT_TRUE(x.advance(6, b, o, cb));
    EXPECT_EQ(o.get(id)->state, State::Cancelled);
    EXPECT_EQ(o.active(), 0);
}
TEST(Strategy, MakerMicropriceSkewAndTakerEma) {
    Book b(16, 16);
    b.apply(test::event(1, Side::Buy, 100, 100));
    b.apply(test::event(2, Side::Sell, 104, 10));
    Maker m;
    auto a = m.on_book_update(b, 1, 0), skew = m.on_book_update(b, 1, 1000);
    EXPECT_EQ(a.count, 2);
    EXPECT_LT(skew.orders[0].price, a.orders[0].price);
    Taker t;
    Intent out;
    for (int i = 0; i < 10; ++i)
        out = t.on_book_update(b, 1, 0);
    EXPECT_EQ(out.count, 1);
    EXPECT_EQ(out.orders[0].side, Side::Buy);
}
TEST(Engine, ReplayOrderProductionAndKill) {
    Config c;
    c.latency = {0, 0};
    c.quote_interval = 0;
    Engine<Maker> e(c);
    test::start(e);
    ASSERT_TRUE(e.process(test::add(1, Side::Buy, 100, 100, 2)));
    ASSERT_TRUE(e.process(test::add(2, Side::Sell, 104, 100, 3)));
    EXPECT_EQ(e.oms.submitted, 2);
    NullSink s;
    ASSERT_TRUE(e.kill(s));
    ASSERT_TRUE(e.advance(3, s));
    EXPECT_EQ(e.oms.active(), 0);
    ASSERT_TRUE(e.process(test::add(3, Side::Buy, 99, 10, 4)));
    EXPECT_EQ(e.oms.submitted, 2);
}
TEST(Ring, ConcurrentOrderAndWrap) {
    auto q = std::make_unique<Ring<unsigned, 64>>();
    std::thread producer([&] {
        for (unsigned i = 0; i < 100000; ++i)
            while (!q->push(i)) {
            };
    });
    for (unsigned i = 0; i < 100000; ++i) {
        unsigned v;
        while (!q->pop(v)) {
        }
        EXPECT_EQ(v, i);
    }
    producer.join();
}
TEST(Logger, DrainAndPortableRecord) {
    auto path = std::filesystem::temp_directory_path() / "hft-test-log.bin";
    auto logger = std::make_unique<AsyncLogger>(path.c_str());
    LogRecord r;
    r.time = 42;
    r.kind = 'O';
    r.wire = encode({1, 1, Side::Buy, 100, 1}, test::symbol());
    ASSERT_TRUE(logger->push(r));
    logger->close();
    ASSERT_TRUE(logger->good());
    EXPECT_EQ(std::filesystem::file_size(path), 60);
    std::filesystem::remove(path);
}
TEST(Histogram, QuantilesBoundariesAndTail) {
    auto h = std::make_unique<Histogram>();
    h->record(0);
    h->record(1023);
    h->record(1024);
    h->record(2048);
    h->record(1000000);
    EXPECT_EQ(h->count(), 5);
    EXPECT_EQ(h->quantile(.5), 1024);
    EXPECT_LE(h->quantile(1), 1001000);
    EXPECT_GE(h->quantile(1), 999000);
}

TEST(Recovery, SnapshotFifoAndGapLatch) {
    Book b(16, 16);
    b.apply(test::event(10, Side::Buy, 100, 3));
    b.apply(test::event(11, Side::Buy, 100, 4));
    auto path = std::filesystem::temp_directory_path() / "hft-recovery.snapshot";
    ASSERT_TRUE(save_snapshot(path.c_str(), b, 42));
    Book restored(16, 16);
    Sequence seq;
    EXPECT_FALSE(seq.accept(3));
    ASSERT_TRUE(load_snapshot(path.c_str(), restored, seq));
    EXPECT_TRUE(seq.healthy());
    EXPECT_EQ(seq.next(), 42);
    EXPECT_EQ(restored.depth(Side::Buy, 100), 7);
    EXPECT_EQ(restored.queue_ahead(11), 3);
    std::filesystem::remove(path);
}
TEST(Logger, OpenFailureIsVisible) {
    auto logger = std::make_unique<AsyncLogger>("/nonexistent-hft-directory/log.bin");
    EXPECT_FALSE(logger->good());
    EXPECT_FALSE(logger->push({}));
    logger->close();
}
TEST(Oms, LargeCostBasisDoesNotOverflow) {
    Account a;
    a.position = 100000000;
    a.basis = 200000000000000000LL;
    a.fill(Side::Sell, 1000000, 2000000000);
    EXPECT_EQ(a.realized, 0);
    EXPECT_EQ(a.basis, 198000000000000000LL);
}

TEST(Gateway, ExecutionReportRoundTripAndPartialCancel) {
    Oms oms;
    Order o{0, 1, Side::Buy, 100, 10};
    o.id = oms.submit(o, 0);
    auto w = accepted(o, test::symbol(), 1);
    auto cb = [](const Fill &) {};
    ASSERT_TRUE(apply_report(w.view(), oms, cb));
    w = executed({o.id, 3, 100, 2, true}, 1);
    ASSERT_TRUE(apply_report(w.view(), oms, cb));
    EXPECT_EQ(oms.account(1).position, 3);
    w = cancelled_report(o.id, 2, 3);
    ASSERT_TRUE(apply_report(w.view(), oms, cb));
    EXPECT_EQ(oms.get(o.id)->order.qty, 8);
    w = cancelled_report(o.id, 5, 4);
    ASSERT_TRUE(apply_report(w.view(), oms, cb));
    EXPECT_EQ(oms.active(), 0);
}
TEST(Gateway, TcpLoopbackLoginAndExecution) {
    int server = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(server, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(bind(server, reinterpret_cast<sockaddr *>(&address), sizeof(address)), 0);
    ASSERT_EQ(listen(server, 1), 0);
    socklen_t length = sizeof(address);
    ASSERT_EQ(getsockname(server, reinterpret_cast<sockaddr *>(&address), &length), 0);
    std::atomic<bool> server_ok = true;
    std::thread peer([&] {
        int fd = accept(server, nullptr, nullptr);
        if (fd < 0) {
            server_ok = false;
            return;
        }
        timeval timeout{2, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        auto read_exact = [&](unsigned char *b, std::size_t n) {
            std::size_t done = 0;
            while (done < n) {
                auto got = recv(fd, b + done, n - done, 0);
                if (got <= 0)
                    return false;
                done += static_cast<std::size_t>(got);
            }
            return true;
        };
        std::array<unsigned char, 52> buffer{};
        if (!read_exact(buffer.data(), 49)) {
            server_ok = false;
            close(fd);
            return;
        }
        std::array<unsigned char, 33> a{};
        put_be(a, 0, 31, 2);
        a[2] = 'A';
        std::fill(a.begin() + 3, a.end(), ' ');
        a[32] = '1';
        if (::send(fd, a.data(), a.size(), 0) != 33 || !read_exact(buffer.data(), 52)) {
            server_ok = false;
            close(fd);
            return;
        }
        Order order;
        Symbol symbol;
        if (!decode_order({buffer.data() + 3, 49}, order, symbol)) {
            server_ok = false;
            close(fd);
            return;
        }
        auto ack = accepted(order, symbol, 1);
        auto fill = executed({order.id, order.qty, order.price, 2, false}, 1);
        for (auto wire : {ack, fill}) {
            std::array<unsigned char, 131> frame{};
            put_be(frame, 0, wire.size + 1, 2);
            frame[2] = 'S';
            std::copy_n(wire.bytes.begin(), wire.size, frame.begin() + 3);
            for (std::size_t i = 0; i < wire.size + 3u; ++i)
                if (::send(fd, frame.data() + i, 1, 0) != 1)
                    server_ok = false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        close(fd);
    });
    TcpGateway gateway;
    Oms oms;
    Order order{0, 1, Side::Buy, 100, 3};
    order.id = oms.submit(order, 0);
    bool connected = gateway.connect_loopback(ntohs(address.sin_port), "user", "secret", 0),
         sent = false;
    for (unsigned i = 0; i < 2000 && oms.account(1).position != 3; ++i) {
        if (!gateway.poll(i * 1000000ULL,
                          [&](auto b) { return apply_report(b, oms, [](const Fill &) {}); }))
            break;
        if (!sent && gateway.state() == SoupSession::State::Active) {
            sent = gateway.send(encode(order, test::symbol()));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    gateway.disconnect();
    peer.join();
    close(server);
    EXPECT_TRUE(connected);
    EXPECT_TRUE(sent);
    EXPECT_TRUE(server_ok.load());
    EXPECT_EQ(oms.account(1).position, 3);
    EXPECT_EQ(gateway.next(), 3);
}

TEST(Book, DenseSparseAndOutsideWindowAgree) {
    Book dense(1024, 1024, 1, true), sparse(1024, 1024, 1, false);
    for (unsigned i = 1; i <= 200; ++i) {
        auto e =
            test::event(i, i % 2 ? Side::Buy : Side::Sell, i % 4 ? 100000 + i : max_price - i, i);
        ASSERT_TRUE(dense.apply(e));
        ASSERT_TRUE(sparse.apply(e));
        EXPECT_EQ(dense.best(e.side), sparse.best(e.side));
        EXPECT_EQ(dense.depth(e.side, e.price), sparse.depth(e.side, e.price));
    }
    for (unsigned i = 1; i <= 200; ++i) {
        auto e = test::removal(Kind::Delete, i);
        ASSERT_TRUE(dense.apply(e));
        ASSERT_TRUE(sparse.apply(e));
        EXPECT_EQ(dense.best(Side::Buy), sparse.best(Side::Buy));
        EXPECT_EQ(dense.best(Side::Sell), sparse.best(Side::Sell));
    }
}

TEST(Simulator, AtomicReplaceAfterPartialFill) {
    Book b(16, 16);
    b.apply(test::event(1, Side::Buy, 100, 1));
    Oms oms;
    Exchange x({0, 0});
    auto id = oms.submit({0, 1, Side::Buy, 100, 10}, 0);
    x.submit(oms.get(id)->order, 0);
    auto cb = [](const Fill &) {};
    ASSERT_TRUE(x.advance(0, b, oms, cb));
    auto e = test::removal(Kind::Execute, 1, 1, 1);
    x.on_event(e, b);
    b.apply(e);
    b.apply(test::event(2, Side::Buy, 100, 10));
    e = test::removal(Kind::Execute, 2, 3, 2);
    x.on_event(e, b);
    b.apply(e);
    ASSERT_TRUE(x.advance(2, b, oms, cb));
    EXPECT_EQ(oms.get(id)->filled, 3);
    auto replacement = oms.begin_replace(id, {0, 1, Side::Buy, 99, 10}, 3);
    ASSERT_NE(replacement, 0);
    ASSERT_TRUE(x.replace(id, oms.get(replacement)->order, 3));
    ASSERT_TRUE(x.advance(3, b, oms, cb));
    EXPECT_EQ(oms.get(id)->state, State::Cancelled);
    EXPECT_EQ(oms.get(replacement)->state, State::Live);
    EXPECT_EQ(oms.get(replacement)->order.qty, 7);
    EXPECT_EQ(oms.account(1).position, 3);
}
TEST(Simulator, FilledOldOrderWinsReplaceRace) {
    Book b(16, 16);
    b.apply(test::event(1, Side::Sell, 100, 1));
    Oms oms;
    Exchange x({0, 0});
    auto id = oms.submit({0, 1, Side::Buy, 100, 1, true}, 0);
    x.submit(oms.get(id)->order, 0);
    auto replacement = oms.begin_replace(id, {0, 1, Side::Buy, 99, 2}, 0);
    ASSERT_NE(replacement, 0);
    x.replace(id, oms.get(replacement)->order, 0);
    ASSERT_TRUE(x.advance(1, b, oms, [](const Fill &) {}));
    EXPECT_EQ(oms.get(id)->state, State::Filled);
    EXPECT_EQ(oms.get(replacement)->state, State::Cancelled);
    EXPECT_EQ(oms.account(1).position, 1);
}
