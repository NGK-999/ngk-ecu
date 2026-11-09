#pragma once
#include <cstdint>

namespace ecu::ui {
struct BenchmarkReport {
    std::uint32_t durationMs = 0, presentations = 0, partialP95Us = 0, partialMaxUs = 0;
    std::uint32_t touchMaxGapUs = 0, fullP95Us = 0;
    std::uint32_t heapFree = 0, heapMin = 0, heapLargest = 0;
    std::uint32_t lvglUsedPercent = 0, lvglFragPercent = 0;
};
struct MotionBenchmarkReport {
    std::uint32_t durationMs = 0, presentations = 0, intervalP95Us = 0, intervalMaxUs = 0;
    std::uint32_t maxPixels = 0, touchPollMaxGapUs = 0;
    std::uint32_t uiTickTotalUs = 0, uiTickMaxUs = 0;
    std::uint32_t rampUpdates = 0, rampGapP95UpperUs = 0, rampGapMaxUs = 0;
    bool interrupted = false;
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
// Explicit, bounded metadata-only observation of the real SIM presentation path.
// Keeps the 17 ms synthetic benchmark separate and does not disable touch.
bool startMotionBenchmark();
bool takeMotionBenchmarkReport(MotionBenchmarkReport& report);
std::uint8_t presentationRefreshMode();
bool reducedMotionEnabled();
// Explicit bench-only frozen redraw, mirrored from existing DMA tiles to UART.
// Returns false before output when the page/state is unsafe. Once output starts,
// only a complete byte count + CRC trailer denotes success (true can abort).
bool captureScreen();
// Explicit operator request: persist Smooth and enable motion, preserving other settings.
bool requestSmoothPresentation();
}
