#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ecu::sdbench {

enum class Mode { Write512, Write4096, Batch4096, Read4096 };

// Result::error is the numeric value of Error. The first failure is retained.
enum class Error : uint32_t {
    None = 0,
    InvalidMode,
    Create,
    Write,
    Read,
    Seek,
    Sync,
    Close,
    Remove,
    Corrupt,
    Timeout,
    Clock,
};

struct Metrics {
    uint32_t operations = 0;
    uint32_t maxUs = 0;
    uint32_t p95UpperUs = 0;
    uint64_t bytes = 0;
    uint64_t totalUs = 0;
};

struct Result {
    bool okay = false;
    bool created = false;
    bool removed = false;
    uint32_t error = static_cast<uint32_t>(Error::None);
    uint32_t setupMs = 0;     // Creation, 1 MiB seed, sync and seek.
    uint32_t durationMs = 0;  // Measured phase through final sync, excluding readback/cleanup.
    Metrics read;
    Metrics write;
    Metrics sync;
};

namespace detail {
constexpr uint32_t kFileSize = 1024U * 1024U;
constexpr uint64_t kGlobalLimitUs = 60'000'000ULL;

inline uint8_t pattern(uint32_t offset, uint32_t generation) {
    // Setup is generation 0; each measured full-file pass increments it.
    const uint32_t mixed = (offset * 0x9E3779B1U) ^ (offset >> 7U) ^ 0xA5C36F29U;
    return static_cast<uint8_t>((mixed ^ (mixed >> 8U) ^ (mixed >> 16U) ^
                                 (mixed >> 24U) ^ generation) & 0xFFU);
}

inline uint32_t boundedUs(uint64_t value) {
    return value > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(value);
}

struct Histogram {
    Metrics metrics;
    std::array<uint32_t, 64> bins{};

    void add(uint64_t elapsedUs, uint32_t bytes) {
        const uint32_t bounded = boundedUs(elapsedUs);
        ++metrics.operations;
        metrics.bytes += bytes;
        metrics.totalUs += elapsedUs;
        if (bounded > metrics.maxUs) metrics.maxUs = bounded;
        const size_t bin = elapsedUs >= 63'000ULL
                               ? 63U
                               : static_cast<size_t>(elapsedUs / 1000ULL);
        ++bins[bin];
    }

    Metrics finish() {
        if (metrics.operations == 0) return metrics;
        const uint64_t rank = (static_cast<uint64_t>(metrics.operations) * 95U + 99U) / 100U;
        uint64_t seen = 0;
        for (size_t i = 0; i < bins.size(); ++i) {
            seen += bins[i];
            if (seen >= rank) {
                metrics.p95UpperUs = i == 63U ? metrics.maxUs
                                               : static_cast<uint32_t>((i + 1U) * 1000U);
                break;
            }
        }
        return metrics;
    }
};
}  // namespace detail

template <class Backend>
Result run(Backend& backend, Mode mode, uint32_t durationMs = 15000) {
    // Backend owns exclusive-file naming and I/O. measurementStarted() and
    // measurementFinished() are no-argument lifecycle callbacks; finish is
    // called once after start even when the measured phase fails.
    Result result;
    if (mode != Mode::Write512 && mode != Mode::Write4096 &&
        mode != Mode::Batch4096 && mode != Mode::Read4096) {
        result.error = static_cast<uint32_t>(Error::InvalidMode);
        return result;
    }

    std::array<uint8_t, 4096> buffer{};
    detail::Histogram readHist;
    detail::Histogram writeHist;
    detail::Histogram syncHist;
    const uint64_t workStart = backend.nowUs();
    uint64_t lastNow = workStart;
    uint64_t phaseStart = 0;
    bool phaseStarted = false;

    const auto fail = [&](Error error) {
        if (result.error == static_cast<uint32_t>(Error::None))
            result.error = static_cast<uint32_t>(error);
    };
    const auto sample = [&](uint64_t& now) {
        now = backend.nowUs();
        if (now < lastNow) {
            fail(Error::Clock);
            return false;
        }
        lastNow = now;
        if (now - workStart >= detail::kGlobalLimitUs) {
            fail(Error::Timeout);
            return false;
        }
        return true;
    };
    const auto pause = [&]() {
        backend.yield();
        uint64_t now = 0;
        return sample(now);
    };
    const auto writeBlock = [&](uint32_t count, uint32_t offset, uint32_t generation,
                                bool measured) {
        for (uint32_t i = 0; i < count; ++i)
            buffer[i] = detail::pattern(offset + i, generation);
        uint64_t before = 0;
        if (!sample(before)) return false;
        const size_t written = backend.write(buffer.data(), count);
        uint64_t after = 0;
        const bool withinTime = sample(after);
        if (measured && after >= before) writeHist.add(after - before, static_cast<uint32_t>(written));
        if (written != count) fail(Error::Write);
        return withinTime && written == count;
    };
    const auto syncFile = [&](bool measured) {
        uint64_t before = 0;
        if (!sample(before)) return false;
        const bool synced = backend.sync();
        uint64_t after = 0;
        const bool withinTime = sample(after);
        if (measured && after >= before) syncHist.add(after - before, 0);
        if (!synced) fail(Error::Sync);
        return withinTime && synced;
    };
    const auto seekTo = [&](uint32_t offset) {
        const bool sought = backend.seek(offset);
        uint64_t now = 0;
        const bool withinTime = sample(now);
        if (!sought) fail(Error::Seek);
        return withinTime && sought;
    };

    if (!backend.createExclusive()) {
        fail(Error::Create);
        return result;
    }
    result.created = true;
    uint64_t now = 0;
    bool proceed = sample(now);

    // Preparation is intentionally outside the measurement histograms.
    for (uint32_t offset = 0; proceed && offset < detail::kFileSize; offset += 4096U) {
        proceed = writeBlock(4096U, offset, 0, false);
        if (proceed) proceed = pause();
    }
    if (proceed) proceed = syncFile(false);
    if (proceed) proceed = seekTo(0);
    if (proceed) {
        backend.measurementStarted();
        proceed = sample(phaseStart);
        phaseStarted = true;
        result.setupMs = static_cast<uint32_t>(((proceed ? phaseStart : lastNow) - workStart) /
                                               1000ULL);
    } else {
        result.setupMs = static_cast<uint32_t>((lastNow - workStart) / 1000ULL);
    }

    uint32_t offset = 0;
    uint32_t generation = mode == Mode::Read4096 ? 0U : 1U;
    uint32_t pendingBytes = 0;
    const uint32_t blockSize = mode == Mode::Write512 ? 512U : 4096U;
    const uint64_t requestedUs = static_cast<uint64_t>(durationMs) * 1000ULL;
    while (proceed) {
        if (!sample(now)) break;
        if (now - phaseStart >= requestedUs) break;
        if (offset == detail::kFileSize) {
            proceed = seekTo(0);
            offset = 0;
            if (mode != Mode::Read4096) ++generation;
            if (!proceed) break;
        }
        if (mode == Mode::Read4096) {
            const uint64_t before = lastNow;
            const size_t count = backend.read(buffer.data(), blockSize);
            uint64_t after = 0;
            const bool withinTime = sample(after);
            if (after >= before) readHist.add(after - before, static_cast<uint32_t>(count));
            if (count != blockSize) fail(Error::Read);
            if (!withinTime || count != blockSize) break;
            for (uint32_t i = 0; i < blockSize; ++i) {
                if (buffer[i] != detail::pattern(offset + i, 0)) {
                    fail(Error::Corrupt);
                    proceed = false;
                    break;
                }
            }
        } else {
            proceed = writeBlock(blockSize, offset, generation, true);
            if (!proceed) break;
            pendingBytes += blockSize;
            if (mode != Mode::Batch4096 || pendingBytes >= 64U * 1024U) {
                proceed = syncFile(true);
                pendingBytes = 0;
            }
        }
        if (!proceed || result.error != static_cast<uint32_t>(Error::None)) break;
        offset += blockSize;
        proceed = pause();
    }

    // The final flush belongs to the measured phase, never to close().
    if (phaseStarted && result.error == static_cast<uint32_t>(Error::None) &&
        mode != Mode::Read4096) {
        syncFile(true);
    }
    if (phaseStarted) {
        uint64_t phaseEnd = 0;
        sample(phaseEnd);
        if (phaseEnd >= phaseStart)
            result.durationMs = static_cast<uint32_t>((phaseEnd - phaseStart) / 1000ULL);
        backend.measurementFinished();
    }

    // Read the complete file after the measured phase. A partial latest pass
    // leaves the suffix from the previous generation (or from setup).
    if (phaseStarted && result.error == static_cast<uint32_t>(Error::None)) {
        bool verified = seekTo(0);
        for (uint32_t verifyOffset = 0; verified && verifyOffset < detail::kFileSize;
             verifyOffset += 4096U) {
            uint64_t before = 0;
            if (!sample(before)) break;
            const size_t count = backend.read(buffer.data(), buffer.size());
            uint64_t after = 0;
            verified = sample(after);
            if (count != buffer.size()) {
                fail(Error::Read);
                break;
            }
            if (!verified) break;
            for (uint32_t i = 0; i < 4096U; ++i) {
                const uint32_t byteOffset = verifyOffset + i;
                const uint32_t expectedGeneration =
                    mode == Mode::Read4096 ? 0U
                                           : (byteOffset < offset ? generation : generation - 1U);
                if (buffer[i] != detail::pattern(byteOffset, expectedGeneration)) {
                    fail(Error::Corrupt);
                    verified = false;
                    break;
                }
            }
            if (verified) verified = pause();
        }
    }

    if (!backend.close()) fail(Error::Close);
    sample(now);
    result.removed = backend.removeOwn();
    sample(now);
    if (!result.removed) fail(Error::Remove);
    result.read = readHist.finish();
    result.write = writeHist.finish();
    result.sync = syncHist.finish();
    result.okay = result.error == static_cast<uint32_t>(Error::None) && result.removed;
    return result;
}
}  // namespace ecu::sdbench
