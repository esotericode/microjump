#pragma once

#include <cstdio>
#include <string>
#include <string_view>

namespace mj {

struct Error {
    std::string message;
};

inline void log(std::string_view message) {
    std::fwrite(message.data(), 1, message.size(), stderr);
    std::fputc('\n', stderr);
}

} // namespace mj
