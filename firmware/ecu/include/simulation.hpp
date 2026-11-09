#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace ecu::simulation {
inline constexpr std::uint32_t protocolVersion = 1;
inline constexpr std::uint32_t ttlMs = 750;

enum Fault : std::uint8_t {
    None = 0, NoRpm = 1, NoSpeed = 2, TempOpen = 4, TempShort = 8,
    LowOil = 16, Overheat = 32, TpsInvalid = 64, GearInvalid = 128
};

struct Frame {
    std::uint32_t session = 0, sequence = 0;
    std::int32_t rpm = 0, speed10 = 0, temp10 = 0, tps10 = 0, gear = 0, oil = 0;
    std::uint8_t faults = None;
};

struct Snapshot {
    bool enabled = false;
    bool live = false;
    Frame frame{};
};

namespace detail {
inline bool lineEnd(const char* line, const char*& end) {
    if (!line) return false;
    std::size_t length = 0;
    while (length <= 95 && line[length]) {
        const auto byte = static_cast<unsigned char>(line[length]);
        if (byte < 32 || byte > 126) return false;
        ++length;
    }
    if (!length || length > 95) return false;
    end = line + length;
    return true;
}

struct Number {
    std::uint32_t magnitude = 0;
    bool negative = false;
};

inline bool number(const char*& cursor, const char* end, Number& output) {
    Number result;
    if (cursor != end && *cursor == '-') {
        result.negative = true;
        ++cursor;
    }
    if (cursor == end || *cursor < '0' || *cursor > '9') return false;
    while (cursor != end && *cursor >= '0' && *cursor <= '9') {
        const auto digit = static_cast<std::uint32_t>(*cursor - '0');
        if (result.magnitude > (std::numeric_limits<std::uint32_t>::max() - digit) / 10) return false;
        result.magnitude = result.magnitude * 10 + digit;
        ++cursor;
    }
    output = result;
    return true;
}

inline bool signedRange(const Number& value, std::int32_t minimum, std::int32_t maximum,
                        std::int32_t& output) {
    const auto wide = value.negative ? -std::int64_t(value.magnitude) : std::int64_t(value.magnitude);
    if (wide < minimum || wide > maximum) return false;
    output = static_cast<std::int32_t>(wide);
    return true;
}

inline bool valid(const Frame& frame) {
    return frame.session != 0 && frame.sequence != 0 &&
        frame.rpm >= 0 && frame.rpm <= 12000 && frame.speed10 >= 0 && frame.speed10 <= 2500 &&
        frame.temp10 >= -400 && frame.temp10 <= 2000 && frame.tps10 >= 0 && frame.tps10 <= 1000 &&
        frame.gear >= -1 && frame.gear <= 4 && frame.oil >= 0 && frame.oil <= 2 &&
        !((frame.faults & TempOpen) && (frame.faults & TempShort));
}
}  // namespace detail

// A single bare ASCII line: CommandLine owns LF/CR framing and rejects NUL.
// All fields in output remain untouched if parsing fails.
inline bool parseFrame(const char* line, Frame& output) {
    const char* end = nullptr;
    if (!detail::lineEnd(line, end) || end - line < 5 || std::memcmp(line, "SIM1 ", 5) != 0) return false;
    auto* cursor = line + 5;
    detail::Number fields[9];
    for (std::size_t index = 0; index < 9; ++index) {
        if (!detail::number(cursor, end, fields[index])) return false;
        if (index < 8) {
            if (cursor == end || *cursor != ' ') return false;
            ++cursor;
        } else if (cursor != end) return false;
    }
    if (fields[0].negative || fields[1].negative || fields[8].negative || fields[8].magnitude > 255) return false;
    Frame candidate;
    candidate.session = fields[0].magnitude;
    candidate.sequence = fields[1].magnitude;
    candidate.faults = static_cast<std::uint8_t>(fields[8].magnitude);
    std::int32_t* values[]{&candidate.rpm, &candidate.speed10, &candidate.temp10,
                           &candidate.tps10, &candidate.gear, &candidate.oil};
    constexpr std::int32_t minimum[]{0, 0, -400, 0, -1, 0};
    constexpr std::int32_t maximum[]{12000, 2500, 2000, 1000, 4, 2};
    for (std::size_t index = 0; index < 6; ++index) {
        if (!detail::signedRange(fields[index + 2], minimum[index], maximum[index], *values[index])) return false;
    }
    if (!detail::valid(candidate)) return false;
    output = candidate;
    return true;
}

// Exact decimal argument for sim-stop. No spaces, sign, suffix or partial parse.
inline bool parseSessionToken(const char* line, std::uint32_t& output) {
    const char* end = nullptr;
    if (!detail::lineEnd(line, end)) return false;
    auto* cursor = line;
    detail::Number value;
    if (!detail::number(cursor, end, value) || cursor != end || value.negative || !value.magnitude) return false;
    output = value.magnitude;
    return true;
}

class State {
public:
    // A new session uses a fresh nonzero caller-provided token. Repeating the
    // active token cannot reset its sequence watermark or extend its lifetime.
    bool start(std::uint32_t token, std::uint32_t now) {
        if (!token || (enabled_ && token == frame_.session)) return false;
        frame_ = {};
        frame_.session = token;
        receivedAt_ = now;
        enabled_ = true;
        received_ = false;
        expired_ = false;
        return true;
    }

    bool stop(std::uint32_t token) {
        if (!enabled_ || token != frame_.session) return false;
        *this = State{};
        return true;
    }

    bool accept(const Frame& frame, std::uint32_t now) {
        if (!enabled_ || frame.session != frame_.session || !detail::valid(frame) ||
            frame.sequence <= frame_.sequence) return false;
        frame_ = frame;
        receivedAt_ = now;
        received_ = true;
        expired_ = false;
        return true;
    }

    Snapshot snapshot(std::uint32_t now) const {
        // Retain sequence/fault metadata when stale for sim-status. Measurement
        // fields are usable only while live; the UI must show -- otherwise.
        // Latch observed expiration so a complete millis cycle cannot revive
        // an old frame. The single owner observes this clock continuously.
        if (enabled_ && received_ && std::uint32_t(now - receivedAt_) >= ttlMs) expired_ = true;
        return {enabled_, enabled_ && received_ && !expired_, frame_};
    }

private:
    Frame frame_{};
    std::uint32_t receivedAt_ = 0;
    bool enabled_ = false;
    bool received_ = false;
    mutable bool expired_ = false;
};
}  // namespace ecu::simulation
