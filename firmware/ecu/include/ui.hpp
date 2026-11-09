#pragma once
#include <cstdint>

namespace ecu::ui {
struct BenchmarkReport {
    std::uint32_t durationMs = 0, presentations = 0, partialP95Us = 0, partialMaxUs = 0;
    std::uint32_t touchMaxGapUs = 0, fullP95Us = 0;
    std::uint32_t heapFree = 0, heapMin = 0, heapLargest = 0;
    std::uint32_t lvglUsedPercent = 0, lvglFragPercent = 0;
};
// These functions belong exclusively to the main UI task.
bool begin();
// Check readiness and schedule the cockpit before main begins a USB session.
bool startUsbSimulation();
void tick();
void setStorage(bool mounted, std::uint32_t capacityMiB);
// Explicit synthetic test: 20 full redraws, then 10 s of small updates at 17 ms.
// No sensor inputs, persistent configuration or per-frame output are involved.
bool startBenchmark();
bool takeBenchmarkReport(BenchmarkReport& report);
bool benchmarkRunning();
}
