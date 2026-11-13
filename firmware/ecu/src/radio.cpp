#include "radio.hpp"
#include "radio_policy.hpp"
#include "wifi_scan_lifecycle.hpp"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#ifdef USING_NIMBLE_ARDUINO_HEADERS
#include <nimble/nimble/host/include/host/ble_hs.h>
#else
#include <host/ble_hs.h>
#endif
#include <nvs.h>
#include <nvs_flash.h>

#include <cstring>

namespace ecu::radio {
namespace {

constexpr uint8_t queueDepth = 8;
constexpr uint32_t scanChannelMs = 200;
// Match Arduino 2.0.17's max_ms_per_chan * 20 timeout without reading its
// mutable scan state concurrently with _scanDone on the other core.
constexpr uint32_t scanTimeoutMs = scanChannelMs * 20;
constexpr uint16_t noPeer = BLE_HS_CONN_HANDLE_NONE;
constexpr char privateNamespace[] = "ngk-radio";
constexpr char serviceUuid[] = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char rxUuid[] = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char txUuid[] = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

enum class Kind : uint8_t {
    WifiEnable, WifiScan, WifiConnect, WifiDisconnect, BleEnable,
    BleDiscover, BleConfirm, BleClearBonds, BleConnectEvent,
    BleDisconnectEvent, BleConfirmEvent, BleAuthEvent, BleRxEvent,
    BleRejectEvent
};

// Only small non-secret tokens and bounded metadata enter this queue.
struct Command {
    Kind kind{};
    uint16_t handle = noPeer;
    uint16_t seconds = 0;
    uint32_t token = 0;
    uint32_t passkey = 0;
    uint8_t slot = queueDepth;
    uint8_t length = 0;
    bool value = false;
    bool bonded = false;
    char bytes[20]{};
};

struct CredentialSlot {
    policy::Credentials credentials;
    bool occupied = false;
    bool reuseSaved = false;
};

StaticQueue_t queueControl;
alignas(Command) uint8_t queueStorage[queueDepth * sizeof(Command)];
QueueHandle_t queue = nullptr;
TaskHandle_t workerTask = nullptr;
CredentialSlot credentialPool[queueDepth];
portMUX_TYPE sharedMux = portMUX_INITIALIZER_UNLOCKED;
Snapshot sharedSnapshot;
bool started = false;
ScanLifecycle wifiScanLifecycle;

void wifiScanCompleted(WiFiEvent_t) {
    // Arduino 2.0.17 invokes user SCAN_DONE hooks after its _scanDone(). This
    // release/acquire mailbox cannot lose completion when the command queue is
    // full and performs no driver, UI or allocation work on the event task.
    wifiScanLifecycle.completed();
}

struct Reply {
    char text[64]{};
    uint8_t length = 0;
    uint8_t offset = 0;
};

struct Worker {
    Snapshot state;
    policy::Credentials saved;
    policy::Credentials connecting;
    policy::Deadline scanDeadline;
    policy::Deadline connectionDeadline;
    policy::Deadline discoveryDeadline;
    policy::Deadline pairingDeadline;
    policy::Deadline frameDeadline;
    policy::BleFramer framer;
    NimBLEServer* server = nullptr;
    NimBLECharacteristic* tx = nullptr;
    uint16_t peer = noPeer;
    uint32_t pairingSequence = 0;
    bool associationPending = false;
    bool discoveryOpen = false;
    bool acceptedPairing = false;
    bool peerWasBonded = false;
    bool wifiDisablePending = false;
    Reply replies[4];
    uint8_t replyRead = 0;
    uint8_t replyCount = 0;

    void status(Status value) {
        state.status = value;
        state.memoryFailure = value == Status::MemoryLow;
    }

    void publish() {
        portENTER_CRITICAL(&sharedMux);
        sharedSnapshot = state;
        portEXIT_CRITICAL(&sharedMux);
    }

    void profileVisible() {
        const auto visible = saved.redacted();
        state.wifiProfileAvailable = visible.configured;
        state.savedNetworkOpen = visible.open;
        std::memcpy(state.savedSsid, visible.ssid, sizeof(state.savedSsid));
    }

    bool memoryAvailable(policy::Radio radio) {
        if (policy::memoryEnough(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                                 heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                                 radio)) return true;
        status(Status::MemoryLow);
        return false;
    }

    void clearPairing() {
        state.blePairingPending = false;
        state.blePasskey = 0;
        state.blePairingId = 0;
        pairingDeadline.cancel();
    }

    void clearPeer() {
        clearPairing();
        peer = noPeer;
        acceptedPairing = false;
        peerWasBonded = false;
        state.bleConnected = false;
        state.bleAuthenticated = false;
        framer.reset();
        frameDeadline.cancel();
        policy::erase(replies, sizeof(replies));
        replyRead = 0;
        replyCount = 0;
    }

    bool authenticated() const {
        if (!server || peer == noPeer || !state.bleAuthenticated) return false;
        const auto info = server->getPeerInfoByHandle(peer);
        return info.getConnHandle() == peer &&
            policy::bleAuthorized(info.isEncrypted(), info.isAuthenticated(), info.getSecKeySize());
    }

    void appendReply(const char* text) {
        if (replyCount == 4) return;  // A slow subscriber never grows RAM usage.
        auto& reply = replies[(replyRead + replyCount) % 4];
        const auto length = policy::boundedLength(text, sizeof(reply.text) - 1);
        if (length >= sizeof(reply.text)) return;
        std::memcpy(reply.text, text, length);
        reply.length = static_cast<uint8_t>(length);
        reply.offset = 0;
        ++replyCount;
    }

    void transmitReply() {
        if (!replyCount || !tx || !authenticated()) return;
        auto& reply = replies[replyRead];
        const auto length = policy::fragmentLength(reply.length - reply.offset, server->getPeerMTU(peer));
        // Never spin on an unsubscribed/slow peer: a failed fragment is dropped.
        if (!length || !tx->notify(reinterpret_cast<const uint8_t*>(reply.text + reply.offset), length, peer)) {
            reply.offset = reply.length;
        } else {
            reply.offset += static_cast<uint8_t>(length);
        }
        if (reply.offset == reply.length) {
            policy::erase(&reply, sizeof(reply));
            replyRead = (replyRead + 1) % 4;
            --replyCount;
        }
    }

    void loadProfile() {
        nvs_handle_t handle;
        if (nvs_open(privateNamespace, NVS_READONLY, &handle) != ESP_OK) return;
        policy::Credentials stored;
        std::size_t size = sizeof(stored);
        const auto result = nvs_get_blob(handle, "profile-v1", &stored, &size);
        nvs_close(handle);
        if (result == ESP_OK && size == sizeof(stored) && stored.ssid[32] == '\0' &&
            stored.password[64] == '\0') {
            saved.assign(stored.ssid, stored.password, stored.open);
        }
        stored.clear();
        profileVisible();
    }

    bool persistProfile() {
        nvs_handle_t handle;
        if (nvs_open(privateNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
        const auto result = nvs_set_blob(handle, "profile-v1", &connecting, sizeof(connecting));
        const auto committed = result == ESP_OK ? nvs_commit(handle) : result;
        nvs_close(handle);
        if (committed != ESP_OK) return false;
        saved = connecting;
        profileVisible();
        return true;
    }

    bool forgetProfile() {
        nvs_handle_t handle;
        auto result = nvs_open(privateNamespace, NVS_READWRITE, &handle);
        if (result == ESP_OK) {
            result = nvs_erase_key(handle, "profile-v1");
            if (result == ESP_OK || result == ESP_ERR_NVS_NOT_FOUND) result = nvs_commit(handle);
            nvs_close(handle);
        }
        if (result != ESP_OK && result != ESP_ERR_NVS_NOT_FOUND) return false;
        saved.clear();
        profileVisible();
        return true;
    }

    void stopScan() {
        if (wifiScanLifecycle.cancel()) esp_wifi_scan_stop();
        // Keep the Arduino-owned array and lifecycle reserved until the event
        // task finishes _scanDone(), even if the driver already reports FAILED.
        state.wifiScanInProgress = false;
        scanDeadline.cancel();
    }

    void finishWifiDisable() {
        if (!wifiDisablePending || wifiScanLifecycle.pending()) return;
        wifiDisablePending = false;
        WiFi.disconnect(false, true);
        const bool stopped = WiFi.mode(WIFI_OFF);
        state.wifiEnabled = WiFi.getMode() != WIFI_OFF;
        wifi_ap_record_t observed{};
        state.wifiConnected = state.wifiEnabled &&
            esp_wifi_sta_get_ap_info(&observed) == ESP_OK && WiFi.status() == WL_CONNECTED;
        state.wifiRssi = state.wifiConnected ? static_cast<int16_t>(WiFi.RSSI()) : 0;
        std::memset(state.wifiSsid, 0, sizeof(state.wifiSsid));
        if (state.wifiConnected)
            std::memcpy(state.wifiSsid, observed.ssid, sizeof(state.wifiSsid) - 1);
        status(stopped && !state.wifiEnabled ? Status::Off : Status::DriverError);
    }

    void wifiEnable(bool enabled) {
        if (enabled && wifiDisablePending) {
            // The driver is still STA while its scan callback drains. Cancel
            // only the requested shutdown; pending scan ownership stays held.
            wifiDisablePending = false;
            status(Status::WifiEnabled);
            return;
        }
        if (enabled == state.wifiEnabled) return;
        if (enabled) {
            if (!memoryAvailable(policy::Radio::Wifi)) return;
            WiFi.persistent(false);
            WiFi.setAutoReconnect(false);
            if (!WiFi.mode(WIFI_STA)) {
                status(Status::DriverError);
                return;
            }
            WiFi.setHostname("ngk-ecu");
            state.wifiEnabled = true;
            status(Status::WifiEnabled);
        } else {
            stopScan();
            connecting.clear();
            associationPending = false;
            connectionDeadline.cancel();
            state.wifiConnecting = false;
            std::memset(state.networks, 0, sizeof(state.networks));
            state.networkCount = 0;
            wifiDisablePending = true;
            // Do not deinitialize the driver while _scanDone may still use it.
            // Keep wifiEnabled faithful to the actual mode until it is off.
            status(Status::Busy);
            finishWifiDisable();
        }
    }

    void wifiScan() {
        if (!state.wifiEnabled) { status(Status::NotEnabled); return; }
        if (state.wifiConnecting || wifiScanLifecycle.pending() || wifiDisablePending) {
            status(Status::Busy); return;
        }
        if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 32000 ||
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 8192) {
            status(Status::MemoryLow);
            return;
        }
        // STA is already enabled by wifiEnable. Start through IDF: Arduino
        // 2.0.17's scanNetworks writes SCANNING after scan_start returns and
        // can overwrite a completion that arrived before that return.
        wifi_scan_config_t config{};
        config.channel = 0;
        config.show_hidden = false;
        config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
        config.scan_time.active.min = 100;
        config.scan_time.active.max = scanChannelMs;
        // Arm BEFORE IDF start: _scanDone + onEvent may finish before it
        // returns. Arduino still owns result allocation, reading and deletion.
        if (!wifiScanLifecycle.begin()) { status(Status::Busy); return; }
        const auto result = esp_wifi_scan_start(&config, false);
        if (result != ESP_OK) {
            wifiScanLifecycle.startFailed();
            status(Status::WifiScanFailed);
            return;
        }
        state.wifiScanInProgress = true;
        state.networkCount = 0;
        std::memset(state.networks, 0, sizeof(state.networks));
        scanDeadline.start(millis(), scanTimeoutMs);
        status(Status::WifiScanning);
    }

    void wifiConnect(policy::Credentials& credentials, bool reuseSaved) {
        if (!state.wifiEnabled) { status(Status::NotEnabled); return; }
        if (wifiScanLifecycle.pending() || wifiDisablePending || state.wifiConnecting) {
            status(Status::Busy); return;
        }
        if (reuseSaved) {
            if (!state.wifiProfileAvailable || std::strcmp(credentials.ssid, saved.ssid) != 0) {
                status(Status::NoSavedProfile);
                return;
            }
            credentials = saved;
        }
        WiFi.disconnect(false, true);
        connecting.clear();
        connecting = credentials;
        state.wifiConnected = false;
        state.wifiConnecting = true;
        state.wifiRssi = 0;
        associationPending = true;
        std::memcpy(state.wifiSsid, credentials.ssid, sizeof(state.wifiSsid));
        connectionDeadline.start(millis(), 20000);
        status(Status::WifiConnecting);
        // pollWifi waits for the old association to end before starting the new
        // one, so stale WL_CONNECTED cannot save unverified replacement data.
    }

    void wifiDisconnect(bool forget) {
        stopScan();
        state.wifiConnecting = false;
        connectionDeadline.cancel();
        connecting.clear();
        associationPending = false;
        const bool disconnected = WiFi.getMode() == WIFI_OFF || WiFi.disconnect(false, true);
        state.wifiEnabled = WiFi.getMode() != WIFI_OFF;
        wifi_ap_record_t observed{};
        state.wifiConnected = state.wifiEnabled &&
            esp_wifi_sta_get_ap_info(&observed) == ESP_OK && WiFi.status() == WL_CONNECTED;
        state.wifiRssi = state.wifiConnected ? static_cast<int16_t>(WiFi.RSSI()) : 0;
        std::memset(state.wifiSsid, 0, sizeof(state.wifiSsid));
        if (state.wifiConnected)
            std::memcpy(state.wifiSsid, observed.ssid, sizeof(state.wifiSsid) - 1);
        const bool forgotten = !forget || forgetProfile();
        status(!disconnected ? Status::DriverError :
               !forgotten ? Status::StorageError : Status::WifiDisconnected);
    }

    void pollWifi(uint32_t now) {
        // Cleanup also runs with radios off, and never relies on queue delivery.
        if (wifiScanLifecycle.ready()) {
            const auto found = WiFi.scanComplete();
            const bool accept = state.wifiScanInProgress && !wifiScanLifecycle.cancelled();
            if (accept && found >= 0) {
                // The Arduino driver owns its transient scan results. Copy only
                // six visible SSIDs and immediately release that driver buffer.
                state.networkCount = 0;
                for (int index = 0; index < found && state.networkCount < maxNetworks; ++index) {
                    const auto ssid = WiFi.SSID(index);
                    if (!policy::validSsid(ssid.c_str())) continue;
                    auto& network = state.networks[state.networkCount++];
                    std::memcpy(network.ssid, ssid.c_str(), ssid.length());
                    network.ssid[ssid.length()] = '\0';
                    network.rssi = static_cast<int16_t>(WiFi.RSSI(index));
                    network.auth = static_cast<uint8_t>(WiFi.encryptionType(index));
                    network.open = network.auth == WIFI_AUTH_OPEN;
                }
            }
            WiFi.scanDelete();
            wifiScanLifecycle.released();
            state.wifiScanInProgress = false;
            scanDeadline.cancel();
            if (accept) status(found >= 0 ? Status::WifiScanDone : Status::WifiScanFailed);
        } else if (state.wifiScanInProgress && scanDeadline.expired(now)) {
            stopScan();
            status(Status::WifiScanFailed);
        }
        finishWifiDisable();
        if (!state.wifiEnabled || wifiDisablePending) return;
        wifi_ap_record_t accessPoint{};
        const auto associated = esp_wifi_sta_get_ap_info(&accessPoint) == ESP_OK;
        if (associationPending && !associated) {
            associationPending = false;
            WiFi.setMinSecurity(connecting.open ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK);
            // Arduino begins association asynchronously; never wait for DHCP.
            const auto result = WiFi.begin(connecting.ssid, connecting.open ? nullptr : connecting.password);
            if (result == WL_CONNECT_FAILED) {
                state.wifiConnecting = false;
                connecting.clear();
                connectionDeadline.cancel();
                status(Status::DriverError);
            }
        }
        const auto connected = associated && WiFi.status() == WL_CONNECTED;
        const auto requestedNetwork = connected &&
            std::strncmp(reinterpret_cast<const char*>(accessPoint.ssid), connecting.ssid, 32) == 0;
        if (state.wifiConnecting && !associationPending && requestedNetwork) {
            state.wifiConnecting = false;
            state.wifiConnected = true;
            connectionDeadline.cancel();
            // Persist only after this boot's explicit local Connect succeeded.
            status(persistProfile() ? Status::WifiConnected : Status::StorageError);
            connecting.clear();
        } else if (state.wifiConnecting && connectionDeadline.expired(now)) {
            WiFi.disconnect(false, true);
            state.wifiConnecting = false;
            state.wifiConnected = false;
            associationPending = false;
            connecting.clear();
            connectionDeadline.cancel();
            status(Status::WifiTimeout);
        } else if (state.wifiConnected && !connected) {
            state.wifiConnected = false;
            status(Status::WifiDisconnected);
        }
        state.wifiRssi = connected ? static_cast<int16_t>(WiFi.RSSI()) : 0;
    }

    bool bleEnable(bool enabled);
    void bleDiscover(uint16_t seconds);
    void bleEvent(const Command& command);
    void process(const Command& command);
    void pollBle(uint32_t now);
};

bool reject(Status status) {
    portENTER_CRITICAL(&sharedMux);
    sharedSnapshot.status = status;
    sharedSnapshot.memoryFailure = status == Status::MemoryLow;
    portEXIT_CRITICAL(&sharedMux);
    return false;
}

bool enqueue(const Command& command) {
    if (!queue) return reject(Status::NotEnabled);
    if (xQueueSend(queue, &command, 0) != pdTRUE) return reject(Status::QueueFull);
    return true;
}

bool callbackEnqueue(const Command& command) {
    return queue && xQueueSend(queue, &command, 0) == pdTRUE;
}

class ServerCallbacks final : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
        Command event;
        event.kind = Kind::BleConnectEvent;
        event.handle = info.getConnHandle();
        // sec_state.bonded can still be false before encryption is restored.
        // Resolve the existing private bond without exposing any address.
        event.bonded = info.isBonded() || NimBLEDevice::isBonded(info.getIdAddress());
        if (!callbackEnqueue(event)) server->disconnect(info);
    }
    void onDisconnect(NimBLEServer*, NimBLEConnInfo& info, int) override {
        Command event;
        event.kind = Kind::BleDisconnectEvent;
        event.handle = info.getConnHandle();
        callbackEnqueue(event);  // Worker also reconciles dropped disconnects.
    }
    void onConfirmPassKey(NimBLEConnInfo& info, uint32_t passkey) override {
        Command event;
        event.kind = Kind::BleConfirmEvent;
        event.handle = info.getConnHandle();
        event.passkey = passkey;
        if (!callbackEnqueue(event)) NimBLEDevice::injectConfirmPasskey(info, false);
    }
    void onAuthenticationComplete(NimBLEConnInfo& info) override {
        Command event;
        event.kind = Kind::BleAuthEvent;
        event.handle = info.getConnHandle();
        event.value = policy::bleAuthorized(info.isEncrypted(), info.isAuthenticated(), info.getSecKeySize());
        if (!callbackEnqueue(event)) {
            auto* server = NimBLEDevice::getServer();
            if (server) server->disconnect(info);
        }
    }
    uint32_t onPassKeyDisplay() override {
        // This local UI supports numeric comparison only. Never fall back to
        // an invisible/static passkey entry ceremony.
        Command event;
        event.kind = Kind::BleRejectEvent;
        callbackEnqueue(event);
        return esp_random() % 1000000;
    }
    void onPassKeyEntry(NimBLEConnInfo& info) override {
        auto* server = NimBLEDevice::getServer();
        if (server) server->disconnect(info);
    }
};

class RxCallbacks final : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* characteristic, NimBLEConnInfo& info) override {
        if (!policy::bleAuthorized(info.isEncrypted(), info.isAuthenticated(), info.getSecKeySize())) return;
        const auto value = characteristic->getValue();
        Command event;
        event.kind = Kind::BleRxEvent;
        event.handle = info.getConnHandle();
        if (value.size() > sizeof(event.bytes)) return;
        event.length = static_cast<uint8_t>(value.size());
        std::memcpy(event.bytes, value.data(), value.size());
        if (!callbackEnqueue(event)) {
            // Dropping a fragment could turn its suffix into another command.
            // Close the stream rather than accepting ambiguous framing.
            auto* server = NimBLEDevice::getServer();
            if (server) server->disconnect(info);
        }
    }
};

ServerCallbacks serverCallbacks;
RxCallbacks rxCallbacks;

bool Worker::bleEnable(bool enabled) {
    if (enabled == state.bleEnabled) return true;
    if (!enabled) {
        if (server) {
            NimBLEDevice::getAdvertising()->stop();
            if (peer != noPeer) server->disconnect(peer);
        }
        // clearAll frees GATT objects; it does not erase bonds or release BLE
        // controller memory permanently. A later explicit enable can init again.
        if (!NimBLEDevice::deinit(false)) { status(Status::DriverError); return false; }
        NimBLEDevice::deinit(true);
        server = nullptr;
        tx = nullptr;
        state.bleEnabled = false;
        state.bleAdvertising = false;
        discoveryDeadline.cancel();
        discoveryOpen = false;
        clearPeer();
        status(Status::Off);
        return true;
    }
    if (!memoryAvailable(policy::Radio::Ble)) return false;
    // NimBLE 2.5.1 otherwise auto-erases the entire NVS on two init errors.
    // Reject those errors before entering the library; never erase this board.
    if (nvs_flash_init() != ESP_OK) { status(Status::StorageError); return false; }
    if (!NimBLEDevice::init("NGK ECU")) { status(Status::DriverError); return false; }
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO);
    ble_hs_cfg.sm_sc_only = 1;
    NimBLEDevice::setMTU(64);
    server = NimBLEDevice::createServer();
    if (!server) { NimBLEDevice::deinit(true); status(Status::MemoryLow); return false; }
    server->setCallbacks(&serverCallbacks, false);
    server->advertiseOnDisconnect(false);
    auto* service = server->createService(serviceUuid);
    if (!service) { NimBLEDevice::deinit(true); server = nullptr; status(Status::MemoryLow); return false; }
    auto* rx = service->createCharacteristic(rxUuid,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN, 20);
    tx = service->createCharacteristic(txUuid, NIMBLE_PROPERTY::NOTIFY, 64);
    if (!rx || !tx) {
        NimBLEDevice::deinit(true);
        server = nullptr;
        tx = nullptr;
        status(Status::MemoryLow);
        return false;
    }
    rx->setCallbacks(&rxCallbacks);
    if (!server->start()) {
        NimBLEDevice::deinit(true);
        server = nullptr;
        tx = nullptr;
        status(Status::DriverError);
        return false;
    }
    auto* advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(serviceUuid);
    advertising->setName("NGK ECU");
    advertising->enableScanResponse(true);
    state.bleEnabled = true;
    status(Status::BleEnabled);
    return true;
}

void Worker::bleDiscover(uint16_t seconds) {
    if (!state.bleEnabled) { status(Status::NotEnabled); return; }
    if (state.bleConnected) { status(Status::Busy); return; }
    auto* advertising = NimBLEDevice::getAdvertising();
    advertising->stop();
    if (!advertising->start(uint32_t(seconds) * 1000)) { status(Status::DriverError); return; }
    state.bleAdvertising = true;
    discoveryOpen = true;
    discoveryDeadline.start(millis(), uint32_t(seconds) * 1000);
    status(Status::BleDiscoverable);
}

void Worker::bleEvent(const Command& command) {
    if (!state.bleEnabled || !server) return;
    if (command.kind == Kind::BleConnectEvent) {
        if (peer != noPeer || !discoveryOpen || discoveryDeadline.expired(millis())) {
            server->disconnect(command.handle);
            return;
        }
        clearPeer();
        peer = command.handle;
        peerWasBonded = command.bonded;
        state.bleConnected = true;
        state.bleAdvertising = false;
        discoveryDeadline.cancel();
        discoveryOpen = false;
        status(Status::BleConnected);
        if (!NimBLEDevice::startSecurity(peer)) { server->disconnect(peer); status(Status::BlePairingRejected); }
        return;
    }
    if (command.kind == Kind::BleRejectEvent) {
        if (peer != noPeer) server->disconnect(peer);
        clearPairing();
        status(Status::BlePairingRejected);
        return;
    }
    if (command.handle != peer) return;
    if (command.kind == Kind::BleDisconnectEvent) {
        clearPeer();
        status(Status::BleEnabled);
    } else if (command.kind == Kind::BleConfirmEvent) {
        acceptedPairing = false;
        state.blePairingPending = true;
        state.blePasskey = command.passkey;
        if (++pairingSequence == 0) ++pairingSequence;
        state.blePairingId = pairingSequence;
        pairingDeadline.start(millis(), 30000);
        status(Status::BlePairing);
    } else if (command.kind == Kind::BleAuthEvent) {
        state.bleAuthenticated = command.value && (acceptedPairing || peerWasBonded);
        clearPairing();
        if (!state.bleAuthenticated) {
            server->disconnect(peer);
            status(Status::BlePairingRejected);
        } else {
            status(Status::BleAuthenticated);
        }
    } else if (command.kind == Kind::BleRxEvent && authenticated()) {
        for (uint8_t index = 0; index < command.length; ++index) {
            const auto parsed = framer.push(command.bytes[index]);
            if (command.bytes[index] == '\n') frameDeadline.cancel();
            else frameDeadline.start(millis(), 3000);
            if (parsed == policy::BleCommand::Ping) appendReply("PONG\n");
            else if (parsed == policy::BleCommand::Info)
                appendReply("NGK ECU;role=bench;commands=PING,INFO;control=none\n");
            else if (parsed == policy::BleCommand::Rejected) appendReply("ERR command\n");
        }
    }
}

void releaseCredential(uint8_t slot) {
    if (slot >= queueDepth) return;
    portENTER_CRITICAL(&sharedMux);
    credentialPool[slot].credentials.clear();
    credentialPool[slot].reuseSaved = false;
    credentialPool[slot].occupied = false;
    portEXIT_CRITICAL(&sharedMux);
}

void Worker::process(const Command& command) {
    switch (command.kind) {
    case Kind::WifiEnable: wifiEnable(command.value); break;
    case Kind::WifiScan: wifiScan(); break;
    case Kind::WifiDisconnect: wifiDisconnect(command.value); break;
    case Kind::WifiConnect: {
        policy::Credentials credentials;
        bool reuseSaved = false;
        bool occupied = false;
        if (command.slot < queueDepth) {
            portENTER_CRITICAL(&sharedMux);
            occupied = credentialPool[command.slot].occupied;
            credentials = credentialPool[command.slot].credentials;
            reuseSaved = credentialPool[command.slot].reuseSaved;
            portEXIT_CRITICAL(&sharedMux);
            releaseCredential(command.slot);
        }
        if (occupied) wifiConnect(credentials, reuseSaved);
        credentials.clear();
        break;
    }
    case Kind::BleEnable: bleEnable(command.value); break;
    case Kind::BleDiscover: bleDiscover(command.seconds); break;
    case Kind::BleConfirm:
        if (server && state.blePairingPending && command.token == state.blePairingId &&
            !pairingDeadline.expired(millis())) {
            const auto info = server->getPeerInfoByHandle(peer);
            acceptedPairing = command.value;
            if (!NimBLEDevice::injectConfirmPasskey(info, command.value)) acceptedPairing = false;
            clearPairing();
            if (!acceptedPairing) { server->disconnect(peer); status(Status::BlePairingRejected); }
        }
        break;
    case Kind::BleClearBonds:
        if (!state.bleEnabled) { status(Status::NotEnabled); break; }
        if (state.bleConnected) { status(Status::Busy); break; }
        status(NimBLEDevice::deleteAllBonds() ? Status::BleBondsCleared : Status::StorageError);
        break;
    default: bleEvent(command); break;
    }
}

void Worker::pollBle(uint32_t now) {
    if (!state.bleEnabled || !server) return;
    if (discoveryOpen && (discoveryDeadline.expired(now) ||
        (!NimBLEDevice::getAdvertising()->isAdvertising() && server->getConnectedCount() == 0))) {
        NimBLEDevice::getAdvertising()->stop();
        state.bleAdvertising = false;
        discoveryDeadline.cancel();
        discoveryOpen = false;
        status(Status::BleDiscoveryEnded);
    }
    if (state.blePairingPending && pairingDeadline.expired(now)) {
        const auto info = server->getPeerInfoByHandle(peer);
        NimBLEDevice::injectConfirmPasskey(info, false);
        server->disconnect(peer);
        clearPairing();
        status(Status::BlePairingRejected);
    }
    if (peer != noPeer && server->getConnectedCount() == 0) {
        clearPeer();
        status(Status::BleEnabled);
    }
    if (frameDeadline.expired(now)) {
        // Reject a stalled stream rather than making a later suffix executable.
        if (peer != noPeer) server->disconnect(peer);
        framer.reset();
        frameDeadline.cancel();
    }
    transmitReply();
}

void runWorker(void*) {
    // This task alone owns radio driver calls and private credentials. The
    // callbacks only publish bounded notifications; no LVGL/SD/Serial is used here.
    Worker worker;
    WiFi.onEvent(wifiScanCompleted, ARDUINO_EVENT_WIFI_SCAN_DONE);
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_OFF);
    worker.loadProfile();
    worker.state.running = true;
    worker.status(Status::Ready);
    worker.publish();
    for (;;) {
        Command command;
        const auto wait = worker.state.wifiEnabled || worker.state.bleEnabled || wifiScanLifecycle.pending() ?
            pdMS_TO_TICKS(20) : portMAX_DELAY;
        const bool received = xQueueReceive(queue, &command, wait) == pdTRUE;
        ++worker.state.workerWakeups;
        if (received) {
            worker.process(command);
            policy::erase(&command, sizeof(command));
        }
        const auto now = millis();
        worker.pollWifi(now);
        worker.pollBle(now);
        worker.publish();
        // Even under a full command queue, yield to Wi-Fi/NimBLE and other
        // core-0 tasks. UI/acquisition stay on their own owner/core.
        vTaskDelay(1);
    }
}

}  // namespace

bool begin() {
    if (started) return true;
    queue = xQueueCreateStatic(queueDepth, sizeof(Command), queueStorage, &queueControl);
    if (!queue) return reject(Status::MemoryLow);
    if (xTaskCreatePinnedToCore(runWorker, "ngk-radio", 7168, nullptr, 1, &workerTask, 0) != pdPASS) {
        vQueueDelete(queue);
        queue = nullptr;
        return reject(Status::MemoryLow);
    }
    started = true;
    return true;
}

bool snapshot(Snapshot& output) {
    portENTER_CRITICAL(&sharedMux);
    output = sharedSnapshot;
    portEXIT_CRITICAL(&sharedMux);
    return output.running;
}

#ifdef NGK_LOAD_BENCH
uint32_t workerStackMinBytes() {
    return workerTask ? uxTaskGetStackHighWaterMark(workerTask) : 0;
}
#endif

bool enableWifi(bool enabled) {
    Command command;
    command.kind = Kind::WifiEnable;
    command.value = enabled;
    return enqueue(command);
}

bool scanWifi() {
    Command command;
    command.kind = Kind::WifiScan;
    return enqueue(command);
}

bool connectWifi(const char* ssid, const char* password) {
    if (!policy::validSsid(ssid)) return reject(Status::InvalidInput);
    Snapshot current;
    snapshot(current);
    bool knownOpen = current.wifiProfileAvailable && current.savedNetworkOpen &&
        std::strcmp(current.savedSsid, ssid) == 0;
    for (uint8_t index = 0; index < current.networkCount; ++index) {
        if (current.networks[index].open && std::strcmp(current.networks[index].ssid, ssid) == 0) knownOpen = true;
    }
    policy::Credentials credentials;
    if (password) {
        if (!credentials.assign(ssid, password, knownOpen)) return reject(Status::InvalidInput);
    } else {
        std::memcpy(credentials.ssid, ssid, policy::boundedLength(ssid, 32));
    }
    uint8_t slot = queueDepth;
    portENTER_CRITICAL(&sharedMux);
    for (uint8_t index = 0; index < queueDepth; ++index) {
        if (credentialPool[index].occupied) continue;
        slot = index;
        credentialPool[index].occupied = true;
        credentialPool[index].reuseSaved = password == nullptr;
        credentialPool[index].credentials = credentials;
        break;
    }
    portEXIT_CRITICAL(&sharedMux);
    credentials.clear();
    if (slot == queueDepth) return reject(Status::QueueFull);
    Command command;
    command.kind = Kind::WifiConnect;
    command.slot = slot;
    if (enqueue(command)) return true;
    releaseCredential(slot);
    return false;
}

bool disconnectWifi(bool forget) {
    Command command;
    command.kind = Kind::WifiDisconnect;
    command.value = forget;
    return enqueue(command);
}

bool enableBle(bool enabled) {
    Command command;
    command.kind = Kind::BleEnable;
    command.value = enabled;
    return enqueue(command);
}

bool discoverBle(uint16_t seconds) {
    if (seconds == 0 || seconds > 120) return reject(Status::InvalidInput);
    Command command;
    command.kind = Kind::BleDiscover;
    command.seconds = seconds;
    return enqueue(command);
}

bool confirmPairing(bool accept) {
    Snapshot current;
    snapshot(current);
    if (!current.blePairingPending) return reject(Status::InvalidInput);
    Command command;
    command.kind = Kind::BleConfirm;
    command.value = accept;
    command.token = current.blePairingId;
    // The token identifies the worker's current peer without exposing a handle.
    return enqueue(command);
}

bool clearBonds() {
    Command command;
    command.kind = Kind::BleClearBonds;
    return enqueue(command);
}

const char* statusText(Status status) {
    switch (status) {
    case Status::Off: return "Desligado";
    case Status::Ready: return "Conexoes prontas";
    case Status::WifiEnabled: return "Wi-Fi ligado";
    case Status::WifiScanning: return "Buscando redes 2,4 GHz";
    case Status::WifiScanDone: return "Busca concluida";
    case Status::WifiConnecting: return "Conectando Wi-Fi";
    case Status::WifiConnected: return "Wi-Fi conectado";
    case Status::WifiDisconnected: return "Wi-Fi desconectado";
    case Status::WifiTimeout: return "Conexao expirou";
    case Status::WifiScanFailed: return "Busca indisponivel";
    case Status::BleEnabled: return "BLE ligado; descoberta fechada";
    case Status::BleDiscoverable: return "BLE visivel temporariamente";
    case Status::BleDiscoveryEnded: return "Descoberta BLE encerrada";
    case Status::BleConnected: return "BLE aguardando autenticacao";
    case Status::BlePairing: return "Compare o codigo nas duas telas";
    case Status::BleAuthenticated: return "BLE autenticado; metadados";
    case Status::BlePairingRejected: return "Pareamento BLE recusado";
    case Status::BleBondsCleared: return "Vinculos BLE apagados";
    case Status::InvalidInput: return "Entrada invalida";
    case Status::QueueFull: return "Fila de conexoes cheia";
    case Status::Busy: return "Conexao ocupada";
    case Status::MemoryLow: return "Memoria insuficiente para radio";
    case Status::StorageError: return "NVS privado indisponivel";
    case Status::DriverError: return "Radio indisponivel";
    case Status::NotEnabled: return "Ligue o radio primeiro";
    case Status::NoSavedProfile: return "Perfil Wi-Fi nao salvo";
    }
    return "Estado de conexao desconhecido";
}

}  // namespace ecu::radio
