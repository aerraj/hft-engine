#include "hft/feed/parser.hpp"
#include "hft/gateway/encoder.hpp"
#include "hft/gateway/session.hpp"
extern "C" int LLVMFuzzerTestOneInput(const unsigned char *data, std::size_t size) {
    hft::Event e;
    hft::parse({data, size}, e);
    hft::Order o;
    hft::Symbol s;
    hft::decode_order({data, size}, o, s);
    hft::Sequence seq;
    hft::mold({data, size}, seq, [](auto) { return true; });
    hft::SoupStream stream;
    stream.feed({data, size}, [](auto) { return true; });
    return 0;
}
