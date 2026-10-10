#include "hft/core/control.hpp"
#include "hft/core/engine.hpp"
#include "hft/feed/recovery.hpp"
#include "hft/log/sink.hpp"
#include <arpa/inet.h>
#include <csignal>
#include <iostream>
#include <memory>
namespace {
volatile std::sig_atomic_t running = 1;
void stop(int) {
    running = 0;
}
template <class S>
int run(unsigned port, const char *output, const char *control_path, std::uint64_t count,
        const char *group, const char *recovery, hft::Config config) {
    auto clock = std::make_unique<hft::Clock>();
    auto metrics = std::make_unique<hft::Metrics>();
    auto e = std::make_unique<hft::Engine<S>>(config, clock.get(), metrics.get());
    auto logger = std::make_unique<hft::AsyncLogger>(output, config.core);
    hft::BinarySink sink{*logger};
    auto control = std::make_unique<hft::Control>(control_path, config.core);
    if (config.core >= 0) {
        if (!hft::pin_core(static_cast<unsigned>(config.core)))
            return 2;
        *clock = hft::Clock{};
    }
    if (!control->good() || !logger->good())
        return 2;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return 2;
    int receive_buffer = 4 * 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer, sizeof(receive_buffer));
    timeval timeout{0, 10000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return 2;
    }
    if (group) {
        ip_mreq membership{};
        if (inet_pton(AF_INET, group, &membership.imr_multiaddr) != 1 ||
            !IN_MULTICAST(ntohl(membership.imr_multiaddr.s_addr))) {
            close(fd);
            return 2;
        }
        membership.imr_interface.s_addr = htonl(INADDR_ANY);
        if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership, sizeof(membership)) != 0) {
            close(fd);
            return 2;
        }
    }
    std::array<unsigned char, 65536> b{};
    hft::Sequence seq;
    std::array<unsigned char, 10> session{};
    bool session_set = false;
    int result = 0;
    while (running && (!count || e->messages < count)) {
        hft::Command command;
        while (control->pop(command)) {
            if (command.type == hft::Command::Kill) {
                e->kill(sink); // drain cancellations even when feed is idle
                if (!e->finish(sink))
                    result = 4;
            } else if (command.type == hft::Command::LimitsChange)
                e->risk.update(command.limits);
        }
        control->stats.killed.store(e->risk.killed());
        auto n = recv(fd, b.data(), b.size(), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                continue;
            result = 3;
            break;
        }
        if (n < 20) {
            result = 4;
            break;
        }
        if (!session_set) {
            std::copy_n(b.begin(), 10, session.begin());
            session_set = true;
        } else if (!std::equal(session.begin(), session.end(), b.begin())) {
            result = 4;
            break;
        }
        if (!hft::mold({b.data(), static_cast<std::size_t>(n)}, seq,
                       [&](auto packet) { return e->process(packet, sink); })) {
            e->kill(sink);
            e->finish(sink);
            std::cerr << "Feed gap/invalid datagram; expected sequence " << seq.next()
                      << "; trading killed\n";
            if (recovery && hft::load_snapshot(recovery, e->book, seq)) {
                std::cerr << "Snapshot restored at sequence " << seq.next()
                          << "; kill remains latched\n";
                continue;
            }
            result = 4;
            break;
        }
        control->stats.messages.store(e->messages);
        control->stats.orders.store(e->oms.submitted);
        control->stats.position.store(e->oms.account(config.target).position);
        control->stats.killed.store(e->risk.killed());
        if (!e->healthy()) {
            result = 4;
            break;
        }
    }
    close(fd);
    if (!e->finish(sink) && !result)
        result = 5;
    logger->close();
    if (!logger->good())
        result = 6;
    std::ofstream latency(std::string(output) + ".latency.json");
    metrics->json(latency);
    std::ofstream counts(std::string(output) + ".latency.csv");
    metrics->csv(counts);
    std::cout << "{\"messages\":" << e->messages << ",\"orders\":" << e->oms.submitted
              << ",\"next_sequence\":" << seq.next()
              << ",\"killed\":" << (e->risk.killed() ? "true" : "false") << "}\n";
    return result;
}
} // namespace
int main(int argc, char **argv) {
    if (argc < 6) {
        std::cerr << "usage: hft_live maker|taker udp_port output.orders control.sock "
                     "message_count [--group multicast_IP] [--recovery snapshot] [--core N] "
                     "[--locate N] [--orders N] [--levels N]\n";
        return 1;
    }
    std::uint64_t port = 0, count = 0;
    auto parse = [](const char *s, std::uint64_t &v) {
        auto n = std::strlen(s);
        auto r = std::from_chars(s, s + n, v);
        return r.ec == std::errc{} && r.ptr == s + n;
    };
    if (!parse(argv[2], port) || !port || port > 65535 || !parse(argv[5], count))
        return 1;
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    const char *group = nullptr;
    const char *recovery = nullptr;
    hft::Config config;
    for (int i = 6; i < argc; ++i) {
        std::string_view key = argv[i];
        if (i + 1 >= argc)
            return 1;
        auto value = argv[++i];
        if (key == "--group")
            group = value;
        else if (key == "--recovery")
            recovery = value;
        else {
            std::uint64_t n;
            if (!parse(value, n))
                return 1;
            if (key == "--core" && n < 1024)
                config.core = static_cast<int>(n);
            else if (key == "--locate" && n < 65536)
                config.target = static_cast<hft::Locate>(n);
            else if (key == "--orders" && n > 0 && n <= 100000000)
                config.order_capacity = static_cast<std::uint32_t>(n);
            else if (key == "--levels" && n > 0 && n <= 10000000)
                config.level_capacity = static_cast<std::uint32_t>(n);
            else
                return 1;
        }
    }
    if (std::string_view(argv[1]) == "maker")
        return run<hft::Maker>(static_cast<unsigned>(port), argv[3], argv[4], count, group,
                               recovery);
    if (std::string_view(argv[1]) == "taker")
        return run<hft::Taker>(static_cast<unsigned>(port), argv[3], argv[4], count, group,
                               recovery);
    return 1;
}
