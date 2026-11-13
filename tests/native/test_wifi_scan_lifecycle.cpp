#include "wifi_scan_lifecycle.hpp"

#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>

using ecu::radio::ScanLifecycle;

// Deterministic model of Arduino 2.0.17 WiFiScan.cpp's public scan methods.
// It preserves the SDK order without touching protected internals: the driver
// may finish _scanDone + application hooks BEFORE scan_start returns ESP_OK.
struct ScanDriver {
    static constexpr int running = -1;
    static constexpr int failed = -2;
    ScanLifecycle& lifecycle;
    unsigned starts = 0;
    unsigned stops = 0;
    bool scanning = false;
    bool done = false;
    bool hasResults = false;
    bool failStart = false;
    bool completeEarly = true;
    uint32_t scanStarted = 0;

    void scanDone() {
        hasResults = true; // Arduino allocates/finishes writing first.
        scanStarted = 0;
        done = true;
        scanning = false;
        lifecycle.completed(); // onEvent runs only after _scanDone.
    }

    bool idfStart() {
        ++starts;
        if (failStart) return false; // No completion event is generated.
        if (completeEarly) scanDone();
        return true;
    }

    int legacyArduinoStart() {
        if (scanning) return running; // No actual driver call on this path.
        scanDelete();
        if (!idfStart()) return failed;
        scanStarted = 1;
        done = false; // These post-start writes erase a completed scan.
        scanning = true;
        return running;
    }

    int scanComplete() const {
        if (done) return hasResults ? 1 : 0;
        return scanning ? running : failed;
    }

    void scanDelete() {
        done = false;
        hasResults = false;
        // Like Arduino, scanDelete does NOT clear scanning or scanStarted.
    }
};

static unsigned earlyCompletionRestart(bool legacy) {
    ScanLifecycle scan;
    ScanDriver driver{scan};
    for (unsigned generation = 0; generation < 2; ++generation) {
        assert(scan.begin()); // Arm before the nonblocking IDF start.
        const bool started = legacy ? driver.legacyArduinoStart() == ScanDriver::running
                                    : driver.idfStart();
        assert(started);
        if (!scan.ready()) {
            // The legacy second call reports RUNNING without any driver scan.
            assert(legacy && generation == 1);
            assert(driver.starts == 1 && driver.scanning);
            assert(scan.pending());
            return driver.starts;
        }
        const auto found = driver.scanComplete();
        if (legacy) assert(found == ScanDriver::running);
        else assert(found == 1); // Direct IDF start leaves callback state intact.
        driver.scanDelete();
        scan.released();
        assert(!scan.pending());
    }
    return driver.starts;
}

static void idfStartFailureAndCancellation() {
    ScanLifecycle scan;
    ScanDriver driver{scan};
    driver.failStart = true;
    assert(scan.begin());
    assert(!driver.idfStart());
    scan.startFailed();
    assert(!scan.pending() && !scan.ready());

    driver.failStart = false;
    driver.completeEarly = false;
    assert(scan.begin()); // Immediate start failure permits another real start.
    assert(driver.idfStart());
    assert(driver.starts == 2);
    if (scan.cancel()) ++driver.stops;
    if (scan.cancel()) ++driver.stops;
    assert(driver.stops == 1 && !scan.ready());
    assert(!scan.begin()); // Timeout/off retain ownership until SCAN_DONE.
    driver.scanDone();
    assert(scan.ready() && scan.cancelled());
    driver.scanDelete();
    scan.released();
    driver.completeEarly = true;
    assert(scan.begin());
    assert(driver.idfStart());
    assert(driver.starts == 3 && scan.ready() && !scan.cancelled());
    assert(!scan.cancel()); // Completion before cancel needs no driver stop.
    driver.scanDelete();
    scan.released();
    assert(!scan.pending());
}

// Same ownership as Arduino 2.0.17: _scanDone allocates the array, passes it
// into esp_wifi_scan_get_ap_records, then invokes application SCAN_DONE hooks.
// The driver pause fixes the interleaving instead of relying on stress timing.
static void cancelWhileArduinoWrites() {
    ScanLifecycle scan;
    assert(scan.begin());
    std::mutex mutex;
    std::condition_variable changed;
    bool filling = false;
    bool resume = false;
    uint8_t* result = nullptr;
    std::thread event([&] {
        auto* records = new uint8_t[76];
        {
            std::unique_lock<std::mutex> lock(mutex);
            result = records;
            filling = true;
            changed.notify_one();
            changed.wait(lock, [&] { return resume; });
        }
        std::memset(records, 0x55, 76);
        scan.completed(); // WiFiGeneric invokes this AFTER _scanDone returns.
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock, [&] { return filling; });
    }
    assert(scan.cancel()); // issue esp_wifi_scan_stop once, without waiting
    assert(scan.pending());
    assert(!scan.ready()); // scanComplete FAILED/timeout is NOT this barrier
    assert(!scan.begin()); // another IDF start must not overlap this callback
    assert(!scan.cancel()); // repeated off/disconnect does not stop twice
    {
        std::lock_guard<std::mutex> lock(mutex);
        resume = true;
    }
    changed.notify_one();
    event.join();
    assert(scan.ready());
    assert(scan.cancelled());
    assert(result[75] == 0x55);
    delete[] result; // only the worker frees, after the completion barrier
    scan.released();
    assert(!scan.pending());
    assert(scan.begin()); // off/on/rescan can now admit another generation
    scan.startFailed(); // failed esp_wifi_scan_start produces no SCAN_DONE
    assert(!scan.pending());
}

static void completionOrders() {
    ScanLifecycle scan;
    scan.completed(); // unrelated/late idle hook cannot authorize a new scan
    assert(scan.begin());
    assert(!scan.ready());
    scan.completed(); // allowed before esp_wifi_scan_start returns ESP_OK
    scan.completed(); // duplicate hooks before cleanup are idempotent
    assert(scan.ready());
    assert(!scan.cancel()); // done-before-cancel: never stop a completed scan
    assert(scan.cancelled());
    assert(!scan.begin());
    scan.released();
    assert(!scan.pending());

    assert(scan.begin());
    scan.released(); // no completion: accidental cleanup cannot reopen scans
    assert(scan.pending());
    assert(!scan.ready());
    assert(scan.cancel()); // timeout/cancel/off all request the same stop
    assert(!scan.begin()); // toggling radios does not release this ownership
    scan.completed(); // notification still works when command queue is full
    assert(scan.ready());
    scan.released();
    assert(!scan.pending());

    assert(scan.begin());
    scan.startFailed();
    assert(scan.begin()); // immediate start failure needs no event to recover
    scan.completed();
    assert(scan.ready());
    assert(!scan.cancelled());
    scan.released();
}

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--legacy-start") == 0) {
        // Explicit RED reproducer: old wrapper admits no second driver start.
        assert(earlyCompletionRestart(true) == 2);
        return 0;
    }
    assert(earlyCompletionRestart(true) == 1); // SDK race reproduced without timing.
    assert(earlyCompletionRestart(false) == 2); // Fixed route starts both scans.
    idfStartFailureAndCancellation();
    cancelWhileArduinoWrites();
    completionOrders();
    std::puts("WiFi scan lifecycle: early IDF completion/restart, immediate failure, cancellation and threaded barrier PASS");
}
