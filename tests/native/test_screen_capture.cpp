#include "screen_capture.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
void check(bool okay, const char* message) {
    if (!okay) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
}

int main() {
    ecu::ScreenCaptureFrame crc;
    check(crc.beginTile(0, 0, 319, 0), "a first full-width row can begin");
    const auto* fixture = reinterpret_cast<const std::uint8_t*>("123456789");
    check(crc.consume(fixture, 4) && crc.consume(fixture + 4, 5), "CRC accepts UART fragments");
    check(crc.crc32() == 0xcbf43926U && crc.bytes() == 9 && !crc.complete(),
          "CRC matches the independent zlib/IEEE known answer, not tile boundaries");
    check(!crc.beginTile(0, 1, 319, 1), "a new tile cannot replace a partially transmitted tile");
    check(!crc.consume(fixture, 641), "a write cannot consume beyond the current tile");

    ecu::ScreenCaptureFrame frame;
    check(!frame.beginTile(1, 0, 319, 15) && !frame.beginTile(0, 0, 318, 15),
          "partial-width flushes are rejected before binary output");
    check(!frame.beginTile(0, 1, 319, 15) && !frame.beginTile(0, -1, 319, 15) &&
          !frame.beginTile(0, 0, 319, 16) && !frame.beginTile(0, 0, 319, -1),
          "gaps, negative bounds, oversized tiles and reversed rows are rejected");
    std::array<std::uint8_t, 10240> tile{};
    for (int y = 0; y < 240; y += 16) {
        check(frame.beginTile(0, y, 319, y + 15), "full-screen tiles retain row-major ordering");
        check(frame.consume(tile.data(), tile.size()), "a complete DMA tile is accounted");
        check(!frame.beginTile(0, y, 319, y + 15), "a duplicate or overlapping tile is rejected");
    }
    check(frame.complete() && frame.bytes() == 153600 && frame.crc32() == 0x066e64a1U,
          "exact full-screen coverage and CRC are required for a valid trailer");
    check(!frame.beginTile(0, 240, 319, 240) && !frame.consume(tile.data(), 1),
          "rows and bytes beyond the frame cannot contaminate a complete capture");

    std::string received;
    for (std::size_t index = 0; index < tile.size(); ++index) tile[index] = std::uint8_t(index % 251);
    const std::string expected(reinterpret_cast<const char*>(tile.data()), tile.size());
    std::uint32_t clock = 0;
    const auto sent = ecu::writeCaptureBytes(tile.data(), tile.size(), 0, 25000,
        [] { return 900; },
        [&](const std::uint8_t* bytes, std::size_t count) {
            check(count <= 512, "each UART write is bounded to at most 512 bytes");
            const auto actual = count / 2 + 1;
            received.append(reinterpret_cast<const char*>(bytes), actual);
            return actual;
        }, [&] { return clock; }, [&] { ++clock; });
    check(sent == tile.size() && received == expected,
          "short writes preserve every unsent suffix until the whole tile is accepted");
    clock = 0;
    unsigned writes = 0;
    const auto stalled = ecu::writeCaptureBytes(tile.data(), tile.size(), 0, 25,
        [] { return 0; },
        [&](const std::uint8_t*, std::size_t) { ++writes; return std::size_t(0); },
        [&] { return clock; }, [&] { ++clock; });
    check(!stalled && !writes && clock == 25, "an unavailable UART stops at the absolute deadline");
    clock = 0xfffffff0U;
    const auto wrapped = ecu::writeCaptureBytes(tile.data(), tile.size(), clock, 25,
        [] { return 10; }, [](const std::uint8_t*, std::size_t) { return std::size_t(0); },
        [&] { return clock; }, [&] { ++clock; });
    check(!wrapped && clock == 9, "zero writes time out safely across the millis wrap");
    clock = 20;
    const auto late = ecu::writeCaptureBytes(tile.data(), tile.size(), 0, 20,
        [] { return 100; },
        [&](const std::uint8_t*, std::size_t) { ++writes; return std::size_t(100); },
        [&] { return clock; }, [&] { ++clock; });
    check(!late && !writes, "later tiles share the original deadline rather than restarting it");
    std::puts("Streaming screen capture geometry/CRC/bounded UART: PASS");
}
