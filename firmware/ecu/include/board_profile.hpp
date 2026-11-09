#pragma once
#include <cstdint>

namespace ecu {
// Bench-verified readable LCD profile; exact ST7789 suffix remains unverified.
// No motor input or actuator pin is assigned in this profile.
struct BoardProfile {
    static constexpr int width = 320, height = 240, nativeWidth = 240, nativeHeight = 320;
    static constexpr int bufferLines = 16;
    static constexpr int lcdSclk = 14, lcdMosi = 13, lcdMiso = -1;
    static constexpr int lcdCs = 15, lcdDc = 2, backlight = 21;
    static constexpr int touchSclk = 25, touchMosi = 32, touchMiso = 39;
    static constexpr int touchCs = 33, touchIrq = 36;
    // SD is owned by its separate worker on VSPI; the UI never touches that bus.
    static constexpr int sdSclk = 18, sdMiso = 19, sdMosi = 23, sdCs = 5;
    static constexpr std::uint32_t lcdSpiHz = 40000000;
    static constexpr std::uint32_t bufferBytes = width * bufferLines * 2 * 2;
};
static_assert(BoardProfile::bufferBytes == 20480, "Two 16-row RGB565 buffers require 20 KiB");
}
