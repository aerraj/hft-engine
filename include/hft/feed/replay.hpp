#pragma once
#include "hft/feed/parser.hpp"
#include <array>
#include <fstream>
#include <string>
namespace hft {
class ReplayReader {
    std::ifstream file_;
    std::array<unsigned char, 65535> buffer_{};
    bool good_ = true;
    std::uint64_t records_ = 0;

  public:
    explicit ReplayReader(const std::string &path) : file_(path, std::ios::binary) {
        good_ = bool(file_);
    }
    bool next(std::span<const unsigned char> &packet) {
        unsigned char len[2];
        file_.read(reinterpret_cast<char *>(len), 2);
        if (file_.gcount() == 0 && file_.eof())
            return false;
        if (file_.gcount() != 2) {
            good_ = false;
            return false;
        }
        auto size = (unsigned(len[0]) << 8) | len[1];
        if (!size) {
            good_ = false;
            return false;
        }
        file_.read(reinterpret_cast<char *>(buffer_.data()), size);
        if (static_cast<unsigned>(file_.gcount()) != size) {
            good_ = false;
            return false;
        }
        packet = {buffer_.data(), size};
        ++records_;
        return true;
    }
    bool good() const noexcept {
        return good_;
    }
    std::uint64_t records() const noexcept {
        return records_;
    }
};
} // namespace hft
