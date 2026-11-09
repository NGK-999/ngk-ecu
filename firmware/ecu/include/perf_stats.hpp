#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace ecu::ui {
template <std::size_t Capacity = 256> class PerfWindow {
    static_assert(Capacity > 0, "A performance window needs retained samples");
public:
    void add(std::uint32_t value) {
        samples_[next_] = value;
        next_ = (next_ + 1) % Capacity;
        if (size_ < Capacity) ++size_;
        if (!count_ || value < min_) min_ = value;
        if (!count_ || value > max_) max_ = value;
        ++count_;
    }
    std::size_t size() const { return size_; }
    std::uint32_t count() const { return count_; }
    std::uint32_t min() const { return min_; }
    std::uint32_t max() const { return max_; }
    // Nearest-rank p95 over the most recent bounded window; extrema/count are total.
    std::uint32_t p95() const {
        if (!size_) return 0;
        auto sorted = samples_;
        std::sort(sorted.begin(), sorted.begin() + size_);
        return sorted[(size_ * 95 + 99) / 100 - 1];
    }
private:
    std::array<std::uint32_t, Capacity> samples_{};
    std::size_t next_ = 0, size_ = 0;
    std::uint32_t count_ = 0, min_ = 0, max_ = 0;
};
constexpr std::uint32_t elapsedUs(std::uint32_t now, std::uint32_t before) {
    return now - before;
}
}
