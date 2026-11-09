#pragma once
#include <cstddef>

namespace ecu {
class CommandLine {
public:
    enum Result { Pending, Ready, Rejected };
    Result push(char character) {
        if (completed_) {
            used_ = 0;
            buffer_[0] = '\0';
            invalid_ = false;
            trailingCr_ = false;
            completed_ = false;
        }
        const auto byte = static_cast<unsigned char>(character);
        if (byte == '\n') {
            completed_ = true;
            if (invalid_) return Rejected;
            return used_ ? Ready : Pending;
        }
        // Once invalid, consume everything through LF. A suffix never gets
        // another chance to become an executable command on the same line.
        if (invalid_) return Pending;
        if (byte == '\r' && !trailingCr_) {
            trailingCr_ = true;
            return Pending;
        }
        if (trailingCr_ || byte < 32 || byte > 126 || used_ == capacity) {
            invalid_ = true;
            used_ = 0;
            buffer_[0] = '\0';
            return Pending;
        }
        buffer_[used_++] = character;
        buffer_[used_] = '\0';
        return Pending;
    }
    const char* value() const { return buffer_; }
private:
    static constexpr std::size_t capacity = 95;
    char buffer_[capacity + 1]{};
    std::size_t used_ = 0;
    bool invalid_ = false;
    bool trailingCr_ = false;
    bool completed_ = false;
};
}
