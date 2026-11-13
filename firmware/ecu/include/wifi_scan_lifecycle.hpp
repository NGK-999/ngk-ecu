#pragma once

#include <atomic>

namespace ecu::radio {

// The worker owns the lifecycle; only completed() runs on the Arduino event
// task, AFTER WiFiScanClass::_scanDone has finished writing its result array.
// A scanComplete() timeout or esp_wifi_scan_stop() return is not that barrier.
class ScanLifecycle {
public:
    bool begin() {
        if (pending_) return false;
        completed_.store(false, std::memory_order_relaxed);
        cancelled_ = false;
        pending_ = true;
        return true;
    }

    // Only after IDF scan_start failed: no SCAN_DONE is due in that case.
    void startFailed() { pending_ = false; }

    void completed() { completed_.store(true, std::memory_order_release); }

    // Returns whether the worker needs to request esp_wifi_scan_stop. Repeated
    // cancels, or cancellation after completion, must not request another stop.
    bool cancel() {
        if (!pending_ || cancelled_) return false;
        cancelled_ = true;
        return !ready();
    }

    bool pending() const { return pending_; }
    bool cancelled() const { return cancelled_; }
    bool ready() const {
        return pending_ && completed_.load(std::memory_order_acquire);
    }

    // Call only after the worker has copied/discarded and deleted the results.
    void released() {
        if (ready()) pending_ = false;
    }

private:
    std::atomic<bool> completed_{false};
    bool pending_ = false;
    bool cancelled_ = false;
};

} // namespace ecu::radio
