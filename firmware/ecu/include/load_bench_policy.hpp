#pragma once
#include <cstdint>
#include <cstddef>
#include <limits>

namespace ecu::loadbench {
struct Request { std::uint32_t id = 0, durationMs = 0; std::uint8_t mode = 0; };
struct Config { std::uint32_t channels = 0, rateHz = 0, iterations = 0; };
enum class State : std::uint8_t { Idle, Queued, Running, Stopping, Done, Stopped, Error };
inline bool valid(const Request& request) {
    return request.id && request.mode <= 2 && request.durationMs >= 1000 && request.durationMs <= 120000;
}
inline bool parseRequest(const char* line, Request& output) {
    if (!line) return false;
    std::size_t length = 0;
    while (length <= 95 && line[length]) ++length;
    if (length > 95) return false;
    constexpr char prefix[] = "load-start ";
    constexpr auto prefixSize = sizeof(prefix) - 1;
    if (length <= prefixSize) return false;
    for (std::size_t i = 0; i < prefixSize; ++i) if (line[i] != prefix[i]) return false;
    std::uint32_t values[3]{};
    auto cursor = prefixSize;
    for (unsigned field = 0; field < 3; ++field) {
        const auto start = cursor;
        while (cursor < length && line[cursor] >= '0' && line[cursor] <= '9') {
            const auto digit = std::uint32_t(line[cursor++] - '0');
            if (cursor - start > 10 || values[field] > (std::numeric_limits<std::uint32_t>::max() - digit) / 10)
                return false;
            values[field] = values[field] * 10 + digit;
        }
        if (cursor == start) return false;
        if (field < 2 && (cursor == length || line[cursor++] != ' ')) return false;
    }
    if (cursor != length || values[1] > 2) return false;
    const Request parsed{values[0], values[2], std::uint8_t(values[1])};
    if (!valid(parsed)) return false;
    output = parsed;
    return true;
}
inline Config config(std::uint8_t mode) {
    return mode == 1 ? Config{16, 100, 32} : mode == 2 ? Config{32, 1000, 128} : Config{};
}
struct Control {
    Request request;
    State state = State::Idle;
    bool start(const Request& value) {
        if (!valid(value) || state == State::Queued || state == State::Running || state == State::Stopping)
            return false;
        request = value;
        state = State::Queued;
        return true;
    }
    bool stop() {
        if (state == State::Queued || state == State::Running) state = State::Stopping;
        return true; // Repeated cleanup never restarts or changes the terminal report.
    }
};
struct Schedule {
    std::uint64_t nextUs = 0, endUs = 0, missed = 0, maxLateUs = 0;
    std::uint32_t periodUs = 0;
    void begin(std::uint64_t now, const Request& request) {
        *this = {};
        const auto parameters = config(request.mode);
        periodUs = parameters.rateHz ? 1000000 / parameters.rateHz : 0;
        nextUs = now; // Half-open run window [start, end), one initial release.
        endUs = now + std::uint64_t(request.durationMs) * 1000;
    }
    bool dispatch(std::uint64_t now) {
        if (!periodUs || now < nextUs || now >= endUs) return false;
        const auto late = now - nextUs;
        if (late > maxLateUs) maxLateUs = late;
        const auto skipped = late / periodUs;
        missed += skipped;
        nextUs += (skipped + 1) * periodUs; // Exactly one job; never catch up in bursts.
        return true;
    }
    void finish(std::uint64_t now) {
        const auto limit = now < endUs ? now : endUs - 1;
        if (!periodUs || nextUs > limit) return;
        const auto skipped = (limit - nextUs) / periodUs + 1;
        missed += skipped;
        nextUs += skipped * periodUs;
    }
};
struct Work { std::uint64_t sequence = 0, readyUs = 0; std::uint32_t checksum = 0; };
struct Mailbox {
    Work latest;
    std::uint64_t produced = 0, consumed = 0, overwritten = 0, sequenceGaps = 0, consumeMaxUs = 0;
    std::uint64_t lastConsumed = 0;
    bool pending = false;
    void publish(std::uint64_t readyUs, std::uint32_t checksum) {
        if (pending) ++overwritten;
        latest = Work{++produced, readyUs, checksum};
        pending = true;
    }
    bool take(std::uint64_t now, Work& output) {
        if (!pending) return false;
        output = latest;
        ++consumed;
        sequenceGaps += latest.sequence - lastConsumed - 1;
        lastConsumed = latest.sequence;
        const auto delay = now >= latest.readyUs ? now - latest.readyUs : 0;
        if (delay > consumeMaxUs) consumeMaxUs = delay;
        pending = false;
        return true;
    }
};
// Integer-only synthetic work. The checksum escapes into the observable report;
// firmware calls this outside the lock with runtime seed/configuration.
inline std::uint32_t kernel(std::uint32_t seed, Config parameters) {
    auto value = seed;
    for (std::uint32_t channel = 0; channel < parameters.channels; ++channel) {
        for (std::uint32_t iteration = 0; iteration < parameters.iterations; ++iteration) {
            value ^= 0x9e3779b9U + channel + iteration;
            value = (value << 5) | (value >> 27);
            value += 0x7f4a7c15U;
        }
    }
    return value;
}
}
