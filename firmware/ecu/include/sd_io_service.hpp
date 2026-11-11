#pragma once
#ifdef NGK_SD_IO_BENCH
#include "sd_io_benchmark.hpp"
#include "sd_io_request.hpp"
class SPIClass;
namespace ecu::sdbench {
enum class State : std::uint8_t { Idle, Queued, Preparing, Measuring, Verifying, Finished };
struct Snapshot {
    Request request{};
    State state = State::Idle;
    Result result{};
    std::uint64_t phaseStartedUs = 0, phaseFinishedUs = 0;
    std::uint32_t stackMinBytes = 0;
    bool cardConsistent = false;
};
bool begin();
bool request(const Request& value);
Snapshot snapshot();
const char* stateText(State state);
// Only the existing storage worker calls this; identity never leaves RAM.
void workerPoll(bool mounted, std::uint32_t sectors, SPIClass& spi, std::uint64_t identity);
}
#endif
