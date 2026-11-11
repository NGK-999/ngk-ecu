#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

namespace ecu::sdbench {
struct Request { std::uint32_t sectors = 0, id = 0; std::uint8_t mode = 0; };

// Exact bounded wire grammar; a rejected line cannot modify the output.
inline bool parseRequest(const char* line, Request& output) {
    if (!line) return false;
    std::size_t length = 0;
    while (length <= 95 && line[length]) ++length;
    if (length > 95) return false;
    constexpr char prefix[] = "sd-io-bench ";
    constexpr std::size_t prefixSize = sizeof(prefix) - 1;
    if (length <= prefixSize) return false;
    for (std::size_t i = 0; i < prefixSize; ++i) if (line[i] != prefix[i]) return false;
    std::size_t cursor = prefixSize;
    std::uint32_t values[3]{};
    for (unsigned field = 0; field < 3; ++field) {
        const auto start = cursor;
        while (cursor < length && line[cursor] >= '0' && line[cursor] <= '9') {
            const auto digit = std::uint32_t(line[cursor++] - '0');
            if (cursor - start > 10 || values[field] > (std::numeric_limits<std::uint32_t>::max() - digit) / 10)
                return false;
            values[field] = values[field] * 10 + digit;
        }
        if (cursor == start || !values[field]) return false;
        if (field < 2 && (cursor == length || line[cursor++] != ' ')) return false;
    }
    if (cursor != length || values[2] > 4) return false;
    output = Request{values[0], values[1], std::uint8_t(values[2])};
    return true;
}
}
