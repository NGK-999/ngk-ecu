#pragma once

#include "settings.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>

namespace ecu {
// Maximum file size, excluding the encoder's convenience NUL terminator.
inline constexpr std::size_t kMaxSettingsEncodedSize = 512;

namespace settings_codec_detail {
inline constexpr std::string_view header = "NGK_UI_SETTINGS=1\n";
inline constexpr std::array<std::string_view, 8> keys{{
    "brightness", "refresh_mode", "speed_unit", "temp_unit", "rpm_scale",
    "reduced_motion", "rpm_yellow_percent", "rpm_red_percent"}};

// FNV-1a detects accidental corruption; it does not authenticate a profile.
inline std::uint32_t checksum(std::string_view bytes) {
    std::uint32_t result = 2166136261U;
    for (unsigned char byte : bytes) {
        result ^= byte;
        result *= 16777619U;
    }
    return result;
}

inline bool decimal(std::string_view text, std::uint32_t& output) {
    if (text.empty()) return false;
    std::uint32_t value = 0;
    for (const char character : text) {
        if (character < '0' || character > '9') return false;
        const auto digit = static_cast<std::uint32_t>(character - '0');
        if (value > ((std::numeric_limits<std::uint32_t>::max)() - digit) / 10U) return false;
        value = value * 10U + digit;
    }
    output = value;
    return true;
}

inline bool hexadecimal(std::string_view text, std::uint32_t& output) {
    if (text.size() != 8) return false;
    std::uint32_t value = 0;
    for (const char character : text) {
        std::uint32_t digit;
        if (character >= '0' && character <= '9') digit = character - '0';
        else if (character >= 'A' && character <= 'F') digit = character - 'A' + 10U;
        else if (character >= 'a' && character <= 'f') digit = character - 'a' + 10U;
        else return false;
        value = value * 16U + digit;
    }
    output = value;
    return true;
}
}

// Returns the bytes to persist, excluding NUL. Capacity must also fit that NUL.
// On failure neither the caller's buffer nor any persistent storage is touched.
inline std::size_t encodeSettings(const UiSettings& settings, char* output, std::size_t capacity) {
    if (!output || !validSettings(settings)) return 0;
    char buffer[kMaxSettingsEncodedSize + 1];
    const int payloadLength = std::snprintf(buffer, sizeof(buffer),
        "NGK_UI_SETTINGS=1\nbrightness=%u\nrefresh_mode=%u\nspeed_unit=%u\ntemp_unit=%u\n"
        "rpm_scale=%u\nreduced_motion=%u\nrpm_yellow_percent=%u\nrpm_red_percent=%u\n",
        static_cast<unsigned>(settings.brightness), static_cast<unsigned>(settings.refreshMode),
        static_cast<unsigned>(settings.speedUnit), static_cast<unsigned>(settings.tempUnit),
        static_cast<unsigned>(settings.rpmScale), static_cast<unsigned>(settings.reducedMotion),
        static_cast<unsigned>(settings.rpmYellowPercent), static_cast<unsigned>(settings.rpmRedPercent));
    if (payloadLength <= 0 || static_cast<std::size_t>(payloadLength) >= sizeof(buffer)) return 0;
    const auto payloadSize = static_cast<std::size_t>(payloadLength);
    const auto integrity = settings_codec_detail::checksum(std::string_view(buffer, payloadSize));
    const auto remaining = sizeof(buffer) - payloadSize;
    const int trailerLength = std::snprintf(buffer + payloadSize, remaining, "checksum=%08lX\n",
                                          static_cast<unsigned long>(integrity));
    if (trailerLength <= 0 || static_cast<std::size_t>(trailerLength) >= remaining) return 0;
    const auto size = payloadSize + static_cast<std::size_t>(trailerLength);
    if (size > kMaxSettingsEncodedSize || capacity <= size) return 0;
    std::memcpy(output, buffer, size + 1);
    return size;
}

// Reads only the supplied bytes. Every key is required exactly once; checksum
// is the final line and covers the header and all preceding LF-terminated lines.
// Unknown fields, trailing bytes and invalid values leave output unchanged.
inline bool decodeSettings(const char* input, std::size_t size, UiSettings& output) {
    using namespace settings_codec_detail;
    if (!input || size == 0 || size > kMaxSettingsEncodedSize) return false;
    const std::string_view profile(input, size);
    if (profile.substr(0, header.size()) != header) return false;

    UiSettings candidate;
    unsigned seen = 0;
    std::size_t cursor = header.size();
    while (cursor < size) {
        const auto end = profile.find('\n', cursor);
        if (end == std::string_view::npos) return false;
        const auto line = profile.substr(cursor, end - cursor);
        const auto separator = line.find('=');
        if (separator == std::string_view::npos) return false;
        const auto key = line.substr(0, separator);
        const auto text = line.substr(separator + 1);
        if (key == "checksum") {
            std::uint32_t integrity = 0;
            if (seen != 0xFFU || end + 1 != size || !hexadecimal(text, integrity) ||
                checksum(profile.substr(0, cursor)) != integrity || !validSettings(candidate)) return false;
            output = candidate;
            return true;
        }
        std::size_t field = 0;
        while (field < keys.size() && key != keys[field]) ++field;
        if (field == keys.size() || (seen & (1U << field))) return false;
        std::uint32_t value = 0;
        if (!decimal(text, value)) return false;
        constexpr std::array<std::uint32_t, 8> maxima{{100, 2, 1, 1, 12000, 1, 100, 100}};
        if (value > maxima[field]) return false;
        switch (field) {
            case 0: candidate.brightness = static_cast<std::uint8_t>(value); break;
            case 1: candidate.refreshMode = static_cast<RefreshMode>(value); break;
            case 2: candidate.speedUnit = static_cast<SpeedUnit>(value); break;
            case 3: candidate.tempUnit = static_cast<TempUnit>(value); break;
            case 4: candidate.rpmScale = static_cast<std::uint16_t>(value); break;
            case 5: candidate.reducedMotion = value != 0; break;
            case 6: candidate.rpmYellowPercent = static_cast<std::uint8_t>(value); break;
            case 7: candidate.rpmRedPercent = static_cast<std::uint8_t>(value); break;
        }
        seen |= 1U << field;
        cursor = end + 1;
    }
    return false;
}
}
