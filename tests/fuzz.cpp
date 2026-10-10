#include "hft/feed/parser.hpp"
extern "C" int LLVMFuzzerTestOneInput(const unsigned char*data,std::size_t size){hft::Event e;hft::parse({data,size},e);return 0;}
