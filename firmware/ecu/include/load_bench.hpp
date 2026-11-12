#pragma once
#ifdef NGK_LOAD_BENCH
#include "load_bench_policy.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace ecu::loadbench {
struct Snapshot {
    Control control;
    Schedule schedule;
    Mailbox mailbox;
    std::uint64_t startedUs = 0, finishedUs = 0, elapsedUs = 0;
    // Wall time including preemption. These values are never CPU utilization.
    std::uint64_t kernelTotalUs = 0, kernelMaxUs = 0;
    std::uint32_t checksum = 0;
    std::uint32_t heapFreeBytes = 0, heapMinBytes = 0, heapLargestBytes = 0;
    // Heap minimum is since boot; stack minima are since each task was created.
    std::uint32_t stackMainMinBytes = 0, stackSdMinBytes = 0;
    std::uint32_t stackRadioMinBytes = 0, stackLoadMinBytes = 0;
};

// Only one command token and one latest-work slot; no peripherals or persistence.
bool begin(TaskHandle_t mainTask, TaskHandle_t sdTask);
bool start(const Request& request);
bool stop(); // Idempotent, including queued-before-running and completed jobs.
void poll(); // Main must continue draining after done/stopped.
Snapshot snapshot();
const char* stateText(State state);
}
#endif
