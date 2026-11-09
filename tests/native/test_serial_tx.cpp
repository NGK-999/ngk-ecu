#include "serial_tx.hpp"
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
    ecu::SerialTxBuffer<8> small;
    check(small.pending() == 0 && small.chunkSize(128) == 0, "empty queue sends no bytes");
    check(small.append("%s", "abc"), "formatted output is queued");
    check(!small.append("%s", "12345") && std::strcmp(small.data(), "abc") == 0,
          "failed append is atomic and leaves queued output intact");
    check(small.append("%04u", 7U) && std::strcmp(small.data(), "abc0007") == 0,
          "exact usable capacity preserves formatted bytes and a terminator");
    small.consume(3);
    check(std::strcmp(small.data(), "0007") == 0, "only acknowledged UART bytes are consumed");
    check(small.append("XY") && std::strcmp(small.data(), "0007XY") == 0,
          "append reuses consumed capacity without losing pending bytes");
    small.consume(1000);
    check(small.pending() == 0 && small.append("%u", 42U), "a drained queue is reusable");

    ecu::SerialTxBuffer<256> serial;
    std::string expected;
    for (unsigned block = 0; block < 20; ++block) expected += "0123456789";
    check(serial.append("%s", expected.c_str()), "a long reply fits without allocation in the queue");
    check(serial.chunkSize(0) == 0 && serial.chunkSize(3) == 3 && serial.chunkSize(128) == 64,
          "each write respects UART availability and the 64-byte loop budget");
    std::string received;
    while (serial.pending()) {
        const auto count = serial.chunkSize(97);
        received.append(serial.data(), count);
        serial.consume(count);
    }
    check(received == expected, "chunked transmission preserves the complete reply exactly");
    check(serial.append("{\"n\":%u}\r\n", 5U), "JSON and CRLF formatting are retained");
    check(std::strcmp(serial.data(), "{\"n\":5}\r\n") == 0, "queued JSON matches original serial bytes");
    serial.consume(2);
    check(serial.pending() == 7 && std::strcmp(serial.data(), "n\":5}\r\n") == 0,
          "short UART writes retain the unsent suffix");
    std::puts("Bounded nonblocking serial TX: PASS");
}
