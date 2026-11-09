#pragma once

#include "board_profile.hpp"
#include "ui_layout.hpp"
#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <algorithm>
#include <array>
#include <driver/gpio.h>

namespace ecu {
class Display final : public lgfx::LGFX_Device {
public:
    Display() {
        auto spi = bus_.config();
        spi.spi_host = SPI2_HOST;
        spi.spi_mode = 0;
        spi.freq_write = BoardProfile::lcdSpiHz;
        spi.freq_read = 16000000;
        spi.spi_3wire = false;
        spi.use_lock = true;
        spi.dma_channel = SPI_DMA_CH_AUTO;
        spi.pin_sclk = BoardProfile::lcdSclk;
        spi.pin_mosi = BoardProfile::lcdMosi;
        spi.pin_miso = BoardProfile::lcdMiso;
        spi.pin_dc = BoardProfile::lcdDc;
        bus_.config(spi);
        panel_.setBus(&bus_);

        auto panel = panel_.config();
        panel.pin_cs = BoardProfile::lcdCs;
        panel.pin_rst = -1;
        panel.pin_busy = -1;
        panel.panel_width = BoardProfile::nativeWidth;
        panel.panel_height = BoardProfile::nativeHeight;
        panel.memory_width = BoardProfile::nativeWidth;
        panel.memory_height = BoardProfile::nativeHeight;
        panel.offset_rotation = 0;
        panel.invert = false;
        panel.rgb_order = false;
        panel.readable = false;
        panel.bus_shared = false;
        panel_.config(panel);

        auto light = light_.config();
        light.pin_bl = BoardProfile::backlight;
        light.invert = false;
        light.freq = 12000;
        light.pwm_channel = 7;
        light_.config(light);
        panel_.setLight(&light_);
        setPanel(&panel_);
    }
private:
    lgfx::Panel_ST7789 panel_;
    lgfx::Bus_SPI bus_;
    lgfx::Light_PWM light_;
};

// XPT2046 has a dedicated software SPI bus. It never reconfigures SD's VSPI.
class ResistiveTouch final {
public:
    void begin() {
        pinMode(BoardProfile::touchSclk, OUTPUT);
        pinMode(BoardProfile::touchMosi, OUTPUT);
        pinMode(BoardProfile::touchMiso, INPUT);
        pinMode(BoardProfile::touchCs, OUTPUT);
        pinMode(BoardProfile::touchIrq, INPUT);
        digitalWrite(BoardProfile::touchCs, HIGH);
        digitalWrite(BoardProfile::touchSclk, LOW);
    }
    bool sample(ui::Point& point) {
        if (digitalRead(BoardProfile::touchIrq) != LOW) return false;
        gpio_set_level(gpio_num_t(BoardProfile::touchCs), 0);
        const auto z1 = convert(0xB0);
        const auto z2 = convert(0xC0);
        if (z1 < 150 || z1 + 4095 - z2 < 400) {
            gpio_set_level(gpio_num_t(BoardProfile::touchCs), 1);
            return false;
        }
        std::array<std::uint16_t, 3> x{}, y{};
        for (std::size_t i = 0; i < x.size(); ++i) {
            x[i] = convert(0xD0);
            y[i] = convert(0x90);
        }
        gpio_set_level(gpio_num_t(BoardProfile::touchCs), 1);
        std::sort(x.begin(), x.end());
        std::sort(y.begin(), y.end());
        point = {float(x[1]), float(y[1])};
        return point.x > 100 && point.x < 4000 && point.y > 100 && point.y < 4000;
    }
private:
    std::uint8_t transfer(std::uint8_t value) {
        std::uint8_t received = 0;
        for (int bit = 7; bit >= 0; --bit) {
            gpio_set_level(gpio_num_t(BoardProfile::touchMosi), (value >> bit) & 1);
            gpio_set_level(gpio_num_t(BoardProfile::touchSclk), 1);
            delayMicroseconds(1);
            received = std::uint8_t((received << 1) |
                gpio_get_level(gpio_num_t(BoardProfile::touchMiso)));
            gpio_set_level(gpio_num_t(BoardProfile::touchSclk), 0);
            delayMicroseconds(1);
        }
        return received;
    }
    std::uint16_t convert(std::uint8_t command) {
        transfer(command);
        const auto high = std::uint16_t(transfer(0));
        const auto low = std::uint16_t(transfer(0));
        return std::uint16_t(((high << 8) | low) >> 3) & 0x0FFF;
    }
};
}
