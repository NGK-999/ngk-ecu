#pragma once
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace ecu {
template<std::size_t Capacity = 2048> class SerialTxBuffer {
    static_assert(Capacity > 1, "TX storage includes a trailing NUL");
public:
    static constexpr std::size_t capacity = Capacity - 1;
    bool append(const char* format, ...) {
        std::va_list arguments;
        va_start(arguments, format);
        const bool appended = appendV(format, arguments);
        va_end(arguments);
        return appended;
    }
    // Measure first: a rejected append cannot truncate an earlier JSON/message.
    // Compaction reuses sent bytes, keeping only one fixed payload allocation.
    bool appendV(const char* format, std::va_list arguments) {
        if (!format) return false;
        std::va_list measured;
        va_copy(measured, arguments);
        const int needed = std::vsnprintf(nullptr, 0, format, measured);
        va_end(measured);
        if (needed < 0 || static_cast<std::size_t>(needed) > free()) return false;
        const auto retained = pending();
        if (begin_) std::memmove(bytes_.data(), data(), retained);
        begin_ = 0;
        end_ = retained;
        bytes_[end_] = 0;
        std::va_list written;
        va_copy(written, arguments);
        const int result = std::vsnprintf(bytes_.data() + end_, Capacity - end_, format, written);
        va_end(written);
        if (result != needed) { bytes_[end_] = 0; return false; }
        end_ += static_cast<std::size_t>(result);
        return true;
    }
    const char* data() const { return bytes_.data() + begin_; }
    std::size_t pending() const { return end_ - begin_; }
    std::size_t free() const { return capacity - pending(); }
    std::size_t chunkSize(std::size_t available) const {
        return std::min({pending(), available, std::size_t(64)});
    }
    void consume(std::size_t sent) {
        begin_ += std::min(sent, pending());
        if (begin_ == end_) { begin_ = end_ = 0; bytes_[0] = 0; }
    }
private:
    std::array<char, Capacity> bytes_{};
    std::size_t begin_ = 0, end_ = 0;
};
}
