#ifdef NGK_SD_IO_BENCH
#include "sd_io_service.hpp"
#include "card_probe.hpp"
#include <SD.h>
#include <atomic>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <esp_system.h>
#include <esp_timer.h>

namespace ecu::sdbench {
namespace {
QueueHandle_t requests = nullptr, states = nullptr;
std::atomic<bool> busy{false};

void publish(const Snapshot& value) { xQueueOverwrite(states, &value); }

class PosixFile {
public:
    explicit PosixFile(Snapshot& state) : state_(state) {
        std::snprintf(path_, sizeof(path_), "/sdcard/ngk-io-%08lx-%08lx.tmp",
                      static_cast<unsigned long>(state.request.id), static_cast<unsigned long>(esp_random()));
    }
    bool createExclusive() {
        descriptor_ = ::open(path_, O_RDWR | O_CREAT | O_EXCL, 0600);
        owned_ = descriptor_ >= 0;
        return owned_;
    }
    std::size_t write(const std::uint8_t* data, std::size_t length) {
        const auto result = ::write(descriptor_, data, length);
        return result < 0 ? 0 : static_cast<std::size_t>(result);
    }
    std::size_t read(std::uint8_t* data, std::size_t length) {
        const auto result = ::read(descriptor_, data, length);
        return result < 0 ? 0 : static_cast<std::size_t>(result);
    }
    bool seek(std::uint32_t offset) { return ::lseek(descriptor_, offset, SEEK_SET) == static_cast<off_t>(offset); }
    bool sync() { return ::fsync(descriptor_) == 0; }
    bool close() {
        if (descriptor_ < 0) return false;
        const int descriptor = descriptor_;
        descriptor_ = -1; // Never retry close on an fd potentially released by VFS.
        return ::close(descriptor) == 0;
    }
    bool removeOwn() { return owned_ && descriptor_ < 0 && ::unlink(path_) == 0; }
    std::uint64_t nowUs() { return static_cast<std::uint64_t>(esp_timer_get_time()); }
    void yield() { vTaskDelay(1); }
    void measurementStarted() {
        state_.phaseStartedUs = nowUs();
        state_.state = State::Measuring;
        publish(state_);
    }
    void measurementFinished() {
        state_.phaseFinishedUs = nowUs();
        state_.state = State::Verifying;
        publish(state_);
    }
private:
    Snapshot& state_;
    int descriptor_ = -1;
    bool owned_ = false;
    char path_[64]{};
};
}

bool begin() {
    requests = xQueueCreate(1, sizeof(Request));
    states = xQueueCreate(1, sizeof(Snapshot));
    if (!requests || !states) return false;
    publish(Snapshot{});
    return true;
}

Snapshot snapshot() {
    Snapshot value{};
    if (states) xQueuePeek(states, &value, 0);
    return value;
}

bool request(const Request& value) {
    if (!requests || !value.id || !value.sectors || value.mode < 1 || value.mode > 4 ||
        value.id == snapshot().request.id || busy.exchange(true)) return false;
    Snapshot state{};
    state.request = value;
    state.state = State::Queued;
    publish(state); // Publish before delivery: worker can never regress to Queued.
    if (xQueueSend(requests, &value, 0) == pdTRUE) return true;
    state.state = State::Finished;
    state.result.error = 103; // Queue unavailable; no file operation was attempted.
    publish(state);
    busy.store(false);
    return false;
}

const char* stateText(State state) {
    switch (state) {
        case State::Idle: return "idle";
        case State::Queued: return "queued";
        case State::Preparing: return "preparing";
        case State::Measuring: return "measuring";
        case State::Verifying: return "verifying";
        case State::Finished: return "finished";
    }
    return "invalid";
}

void workerPoll(bool mounted, std::uint32_t sectors, SPIClass& spi, std::uint64_t identity) {
    Request value{};
    if (xQueueReceive(requests, &value, 0) != pdTRUE) return;
    Snapshot state{};
    state.request = value;
    state.state = State::Preparing;
    publish(state);
    std::uint64_t observed = 0;
    if (!mounted || sectors != value.sectors || SD.cardSize() / 512 != sectors) {
        state.result.error = 100; // Mount or expected geometry changed; no file created.
    } else if (!identity || !readCardIdentity(spi, 5, observed) || observed != identity) {
        state.result.error = 101; // CID continuity is checked only in RAM.
    } else if (SD.totalBytes() < SD.usedBytes() + 2ULL * 1024 * 1024) {
        state.result.error = 102; // Keep two MiB free before creating the one-MiB fixture.
    } else {
        PosixFile file(state);
        state.result = run(file, static_cast<Mode>(value.mode - 1));
        state.cardConsistent = readCardIdentity(spi, 5, observed) && observed == identity;
        if (!state.cardConsistent) {
            state.result.okay = false;
            if (!state.result.error) state.result.error = 101;
        }
    }
    state.stackMinBytes = uxTaskGetStackHighWaterMark(nullptr); // ESP-IDF reports bytes.
    state.state = State::Finished;
    publish(state);
    busy.store(false);
}
}
#endif
