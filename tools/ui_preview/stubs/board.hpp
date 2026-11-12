#pragma once
// Host only. No LovyanGFX, SPI, USB, GPIO or touch device is opened.
#include "Arduino.h"
#include "board_profile.hpp"
#include "ui_layout.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace lgfx { using swap565_t = std::uint16_t; }
class PreviewSerialSink {
public:
    int availableForWrite() { return 128; }
    std::size_t write(const std::uint8_t*, std::size_t count) { return count; }
};
inline PreviewSerialSink Serial;
namespace ecu {
extern std::uint16_t framebuffer[320 * 240];
class Display {
public:
    bool init() { return true; }
    void setRotation(int) {}
    void setBrightness(std::uint8_t) {}
    void initDMA() {}
    bool dmaBusy() { return false; }
    void waitDMA() {}
    void startWrite() {}
    void endWrite() {}
    void pushImageDMA(int x, int y, int w, int h, lgfx::swap565_t* pixels) {
        if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > 320 || y + h > 240)
            std::abort();
        for (int row = 0; row < h; ++row)
            std::memcpy(framebuffer + (y + row) * 320 + x, pixels + row * w,
                        std::size_t(w) * sizeof(*pixels));
    }
};
class ResistiveTouch {
public:
    void begin() {}
    bool sample(ui::Point&) { return false; }
};
}
