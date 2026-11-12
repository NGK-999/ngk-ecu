#ifdef NGK_LOAD_BENCH
#include "load_bench.hpp"
#include "radio.hpp"
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/queue.h>

namespace ecu::loadbench {
namespace {
StaticQueue_t commandControl;
alignas(Request) std::uint8_t commandStorage[sizeof(Request)];
QueueHandle_t commands = nullptr;
TaskHandle_t loadTask = nullptr, mainTask = nullptr, sdTask = nullptr;
portMUX_TYPE sharedMux = portMUX_INITIALIZER_UNLOCKED;
Snapshot shared;
bool ready = false;

std::uint64_t clockUs() { return static_cast<std::uint64_t>(esp_timer_get_time()); }

// Keep a materialized runtime kernel for disassembly and prevent dead-code work.
__attribute__((noinline)) std::uint32_t runKernel(std::uint32_t seed, Config parameters) {
    return kernel(seed, parameters);
}

void worker(void*) {
    for (;;) {
        Request request;
        if (xQueueReceive(commands, &request, portMAX_DELAY) != pdTRUE) continue;
        ulTaskNotifyTake(pdTRUE, 0); // A stale stop wakeup cannot affect a new job.
        Schedule schedule;
        const auto startedUs = clockUs();
        schedule.begin(startedUs, request);
        const auto parameters = config(request.mode);
        auto checksum = request.id;
        portENTER_CRITICAL(&sharedMux);
        shared.startedUs = startedUs;
        shared.schedule = schedule;
        // stop() may have been accepted before this queued request was dequeued.
        if (shared.control.state != State::Stopping) shared.control.state = State::Running;
        portEXIT_CRITICAL(&sharedMux);
        for (;;) {
            const auto now = clockUs();
            portENTER_CRITICAL(&sharedMux);
            const bool stopping = shared.control.state == State::Stopping;
            portEXIT_CRITICAL(&sharedMux);
            if (stopping || now >= schedule.endUs) break;
            if (schedule.dispatch(now)) {
                const auto before = clockUs();
                checksum = runKernel(checksum, parameters);
                const auto after = clockUs();
                const auto wallUs = after - before;
                portENTER_CRITICAL(&sharedMux);
                shared.kernelTotalUs += wallUs;
                if (wallUs > shared.kernelMaxUs) shared.kernelMaxUs = wallUs;
                shared.checksum = checksum;
                shared.mailbox.publish(after, checksum);
                shared.schedule = schedule;
                portEXIT_CRITICAL(&sharedMux);
            }
            const auto after = clockUs();
            auto wakeUs = schedule.periodUs ? schedule.nextUs : after + 100000;
            if (wakeUs > schedule.endUs) wakeUs = schedule.endUs;
            constexpr std::uint64_t tickUs = std::uint64_t(portTICK_PERIOD_MS) * 1000;
            const auto ticks = wakeUs > after ? (wakeUs - after + tickUs - 1) / tickUs : 1;
            // Always block/yield; missed releases are skipped on the next wake.
            ulTaskNotifyTake(pdTRUE, static_cast<TickType_t>(ticks));
        }
        const auto finishedUs = clockUs();
        schedule.finish(finishedUs);
        portENTER_CRITICAL(&sharedMux);
        shared.finishedUs = finishedUs;
        shared.schedule = schedule;
        shared.control.state = shared.control.state == State::Stopping ? State::Stopped : State::Done;
        // Preserve the final mailbox item; main's next poll accounts for it.
        portEXIT_CRITICAL(&sharedMux);
    }
}
}

bool begin(TaskHandle_t mainHandle, TaskHandle_t sdHandle) {
    if (ready) return true;
    mainTask = mainHandle;
    sdTask = sdHandle;
    commands = xQueueCreateStatic(1, sizeof(Request), commandStorage, &commandControl);
    if (!commands) return false;
    if (xTaskCreatePinnedToCore(worker, "ngk-load", 4096, nullptr, 1, &loadTask, 1) != pdPASS) {
        vQueueDelete(commands);
        commands = nullptr;
        return false;
    }
    ready = true;
    return true;
}

bool start(const Request& request) {
    if (!ready) return false;
    portENTER_CRITICAL(&sharedMux);
    auto control = shared.control;
    const bool accepted = !shared.mailbox.pending && control.start(request);
    if (accepted) {
        shared = {};
        shared.control = control;
    }
    portEXIT_CRITICAL(&sharedMux);
    if (!accepted) return false;
    if (xQueueSend(commands, &request, 0) == pdTRUE) return true;
    portENTER_CRITICAL(&sharedMux);
    shared.control.state = State::Error;
    portEXIT_CRITICAL(&sharedMux);
    return false;
}

bool stop() {
    if (!ready) return false;
    portENTER_CRITICAL(&sharedMux);
    shared.control.stop();
    portEXIT_CRITICAL(&sharedMux);
    xTaskNotifyGive(loadTask);
    return true;
}

void poll() {
    Work item;
    portENTER_CRITICAL(&sharedMux);
    // Read after acquiring the slot: a producer may preempt main before the lock.
    shared.mailbox.take(clockUs(), item);
    portEXIT_CRITICAL(&sharedMux);
}

Snapshot snapshot() {
    Snapshot result;
    portENTER_CRITICAL(&sharedMux);
    result = shared; // Counters and mailbox occupancy share this one lock.
    portEXIT_CRITICAL(&sharedMux);
    result.elapsedUs = result.startedUs ? (result.finishedUs ? result.finishedUs : clockUs()) - result.startedUs : 0;
    constexpr auto caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    result.heapFreeBytes = heap_caps_get_free_size(caps);
    result.heapMinBytes = heap_caps_get_minimum_free_size(caps);
    result.heapLargestBytes = heap_caps_get_largest_free_block(caps);
    // ESP-IDF's public API returns bytes, unlike vanilla FreeRTOS words.
    result.stackMainMinBytes = mainTask ? uxTaskGetStackHighWaterMark(mainTask) : 0;
    result.stackSdMinBytes = sdTask ? uxTaskGetStackHighWaterMark(sdTask) : 0;
    result.stackRadioMinBytes = radio::workerStackMinBytes();
    result.stackLoadMinBytes = loadTask ? uxTaskGetStackHighWaterMark(loadTask) : 0;
    return result;
}

const char* stateText(State state) {
    switch (state) {
        case State::Idle: return "idle";
        case State::Queued: return "queued";
        case State::Running: return "running";
        case State::Stopping: return "stopping";
        case State::Done: return "done";
        case State::Stopped: return "stopped";
        case State::Error: return "error";
    }
    return "error";
}
}
#endif
