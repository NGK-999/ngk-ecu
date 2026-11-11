#include "sd_io_request.hpp"
#include <cassert>
#include <cstring>
#include <initializer_list>

int main() {
    ecu::sdbench::Request request{};
    assert(ecu::sdbench::parseRequest("sd-io-bench 61962240 123 4", request));
    assert(request.sectors == 61962240 && request.id == 123 && request.mode == 4);
    const auto original = request;
    for (const char* text : {"sd-io-bench 0 123 1", "sd-io-bench 1 0 1",
            "sd-io-bench 1 1 0", "sd-io-bench 1 1 5", "sd-io-bench -1 1 1",
            "sd-io-bench 4294967296 1 1", "sd-io-bench 1 4294967296 1",
            "sd-io-bench 1 1 1 extra", "sd-io-bench  1 1 1", "sd-io-bench 1 1 1 ",
            "sd-io-bench 1 1 1\n", "sd-io-bench +1 1 1", "sd-io-bench 1 1", ""}) {
        assert(!ecu::sdbench::parseRequest(text, request));
        assert(request.id == original.id && request.mode == original.mode && request.sectors == original.sectors);
    }
    assert(!ecu::sdbench::parseRequest(nullptr, request));
    assert(ecu::sdbench::parseRequest("sd-io-bench 4294967295 4294967295 1", request));
    char oversized[120]; std::memset(oversized, '1', sizeof(oversized)); oversized[119] = 0;
    assert(!ecu::sdbench::parseRequest(oversized, request));
}
