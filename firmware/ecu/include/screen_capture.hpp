#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace ecu {
// Metadata only. Pixel storage always remains in the existing LVGL DMA tile.
class ScreenCaptureFrame {
public:
    static constexpr int width = 320, height = 240, maxTileLines = 16;
    static constexpr std::size_t frameBytes = width * height * 2;
    bool beginTile(int x1, int y1, int x2, int y2) {
        if (remaining_ || x1 != 0 || x2 != width - 1 || y1 != nextRow_ ||
            y1 < 0 || y2 < y1 || y2 >= height || y2 - y1 + 1 > maxTileLines) return false;
        nextRow_ = y2 + 1;
        remaining_ = static_cast<std::size_t>(y2 - y1 + 1) * width * 2;
        return true;
    }
    bool consume(const std::uint8_t* data, std::size_t size) {
        if (size > remaining_ || (size && !data)) return false;
        for (std::size_t index = 0; index < size; ++index) {
            crc_ ^= data[index];
            for (unsigned bit = 0; bit < 8; ++bit)
                crc_ = (crc_ >> 1U) ^ (0xedb88320U & (0U - (crc_ & 1U)));
        }
        bytes_ += size;
        remaining_ -= size;
        return true;
    }
    bool complete() const { return nextRow_ == height && !remaining_ && bytes_ == frameBytes; }
    std::size_t bytes() const { return bytes_; }
    std::uint32_t crc32() const { return crc_ ^ 0xffffffffU; }
private:
    int nextRow_ = 0;
    std::size_t bytes_ = 0, remaining_ = 0;
    std::uint32_t crc_ = 0xffffffffU;
};

// Return only bytes acknowledged by the transport. A single absolute deadline
// covers header, all tiles and trailer; no UART call exceeds available capacity.
template<class Available, class Write, class Clock, class Yield>
std::size_t writeCaptureBytes(const std::uint8_t* data, std::size_t size,
    std::uint32_t started, std::uint32_t timeoutMs, Available available,
    Write write, Clock clock, Yield yield) {
    std::size_t sent = 0;
    while (sent < size && std::uint32_t(clock() - started) < timeoutMs) {
        const auto capacity = available();
        if (capacity > 0) {
            const auto count = std::min({size - sent, std::size_t(capacity), std::size_t(512)});
            const auto accepted = write(data + sent, count);
            if (accepted > count) break;
            sent += accepted;
        }
        yield();
    }
    return sent;
}
}
