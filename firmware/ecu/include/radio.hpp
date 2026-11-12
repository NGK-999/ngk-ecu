#pragma once

#include <cstddef>
#include <cstdint>

namespace ecu::radio {

constexpr std::size_t maxNetworks = 6;

enum class Status : uint8_t {
    Off, Ready, WifiEnabled, WifiScanning, WifiScanDone, WifiConnecting,
    WifiConnected, WifiDisconnected, WifiTimeout, WifiScanFailed,
    BleEnabled, BleDiscoverable, BleDiscoveryEnded, BleConnected,
    BlePairing, BleAuthenticated, BlePairingRejected, BleBondsCleared,
    InvalidInput, QueueFull, Busy, MemoryLow, StorageError, DriverError,
    NotEnabled, NoSavedProfile
};

struct WifiNetwork {
    char ssid[33]{};
    int16_t rssi = 0;
    uint8_t auth = 0;  // ESP32 wifi_auth_mode_t; zero is an open network.
    bool open = false;
};

// Copy only to the local UI. SSIDs and ephemeral pairing digits must never be
// copied into logs, SD profiles, telemetry or the BLE metadata channel.
struct Snapshot {
    // Completed queue waits (commands or timeouts); diagnostics compare deltas.
    uint32_t workerWakeups = 0;
    WifiNetwork networks[maxNetworks]{};
    char wifiSsid[33]{};
    char savedSsid[33]{};
    Status status = Status::Off;
    uint8_t networkCount = 0;
    bool running = false;
    bool wifiEnabled = false;
    bool wifiConnected = false;
    bool wifiConnecting = false;
    bool wifiScanInProgress = false;
    bool wifiProfileAvailable = false;
    bool savedNetworkOpen = false;
    int16_t wifiRssi = 0;
    bool bleEnabled = false;
    bool bleAdvertising = false;
    bool bleConnected = false;
    bool bleAuthenticated = false;
    bool blePairingPending = false;
    uint32_t blePasskey = 0;
    uint32_t blePairingId = 0;
    bool memoryFailure = false;
};

// Starts the idle, radios-off worker on core 0. No UI or hardware callback is
// invoked by this module. The queue holds at most eight small command tokens.
bool begin();
bool snapshot(Snapshot& output);
#ifdef NGK_LOAD_BENCH
// Public ESP-IDF watermark, bytes since this worker was created.
uint32_t workerStackMinBytes();
#endif
const char* statusText(Status status);

// These methods copy bounded input and enqueue without waiting. A true return
// means accepted, not completed; inspect a later snapshot for the result.
bool enableWifi(bool enabled);
bool scanWifi();
// password == nullptr explicitly reuses the saved private profile for this
// exact SSID. An empty password is accepted only for a scanned/saved open AP.
bool connectWifi(const char* ssid, const char* password);
bool disconnectWifi(bool forget = false);
bool enableBle(bool enabled);
bool discoverBle(uint16_t seconds = 120);
bool confirmPairing(bool accept);
bool clearBonds();

}  // namespace ecu::radio
