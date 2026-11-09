#pragma once
#include <cstdint>

namespace ecu::ui::theme {
inline constexpr std::uint32_t background = 0x000000;
// These neutral levels also remain neutral after RGB565 quantization.
inline constexpr std::uint32_t card = 0x101010;
inline constexpr std::uint32_t buttonColor = card;
inline constexpr std::uint32_t text = 0xF5F5F5;
inline constexpr std::uint32_t secondary = 0xA0A0A0;
inline constexpr std::uint32_t accent = 0xFF6A00;
inline constexpr std::uint32_t warning = 0xFF9B32;
inline constexpr std::uint32_t danger = 0xFF3B30;
inline constexpr std::uint32_t border = 0x212121;
inline constexpr std::uint32_t pressed = 0x2A160B;
inline constexpr std::uint32_t inactive = 0x202020;
inline constexpr int buttonRadius = 12;
}
