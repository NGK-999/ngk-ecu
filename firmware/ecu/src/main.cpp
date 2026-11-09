#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <sd_diskio.h>
extern "C" {
#include <ff.h>
#include <diskio.h>
}
#include "command_line.hpp"
#include "profile_service.hpp"
#include "radio.hpp"
#include "storage_schedule.hpp"
#include "serial_tx.hpp"
#include "ui.hpp"
#include "simulation_service.hpp"
#ifdef NGK_SD_MAINTENANCE
#include <esp_task_wdt.h>
#include "card_probe.hpp"
#include "maintenance_policy.hpp"
#endif

namespace {
constexpr int SdClock = 18, SdMiso = 19, SdMosi = 23, SdSelect = 5;
constexpr uint32_t SectorBytes = 512;
constexpr const char* Version = "0.4.1-simulator";
struct Snapshot {
    bool present = false, mounted = false, verified = false, formatReady = false;
    uint32_t sectors = 0, type = 0;
    uint64_t identity = 0; // RAM only. Never printed or persisted.
    int formatState = 0; // 0 idle, 1 active, 2 success, -1 failed
};
QueueHandle_t snapshots = nullptr;
Snapshot view;
uint32_t bootToken = 0;
ecu::SerialTxBuffer<2048> serialTx;

bool queuePrintf(const char* format, ...) {
    std::va_list arguments;
    va_start(arguments, format);
    const bool queued = serialTx.appendV(format, arguments);
    va_end(arguments);
    return queued;
}

bool queueLine(const char* line) { return serialTx.append("%s\r\n", line); }

void pumpSerial() {
    const auto available = Serial.availableForWrite();
    if (available <= 0) return;
    const auto count = serialTx.chunkSize(static_cast<std::size_t>(available));
    if (count) {
        const auto sent = Serial.write(reinterpret_cast<const uint8_t*>(serialTx.data()), count);
        serialTx.consume(sent);
    }
}

[[noreturn]] void halt(const char* reason) {
    queueLine(reason);
    for (;;) { pumpSerial(); delay(1); }
}

struct Geometry { uint32_t sectors = 0, type = 0; uint64_t identity = 0; };

bool probe(SPIClass& spi, uint8_t& drive, Geometry& geometry, int frequency = 1000000) {
    drive = sdcard_init(SdSelect, &spi, frequency);
    if (drive == 0xff) return false;
    DWORD sectors = 0;
    WORD sectorSize = 0;
    if ((disk_initialize(drive) & STA_NOINIT) ||
        disk_ioctl(drive, GET_SECTOR_COUNT, &sectors) != RES_OK ||
        disk_ioctl(drive, GET_SECTOR_SIZE, &sectorSize) != RES_OK ||
        sectorSize != SectorBytes || !sectors) {
        sdcard_uninit(drive); drive = 0xff; return false;
    }
    geometry.sectors = sectors;
    geometry.type = unsigned(sdcard_type(drive));
#ifdef NGK_SD_MAINTENANCE
    // Failure still permits diagnostics, but never formatting.
    ecu::readCardIdentity(spi, SdSelect, geometry.identity);
#endif
    return true;
}

bool mountCard(SPIClass& spi, const Geometry& geometry) {
    SD.end();
    bool okay = SD.begin(SdSelect, spi, 8000000, "/sdcard", 3, false);
    if (!okay) {
        SD.end();
        okay = SD.begin(SdSelect, spi, 1000000, "/sdcard", 3, false);
    }
    if (okay && SD.cardSize() / SectorBytes != geometry.sectors) {
        SD.end(); okay = false;
    }
    return okay;
}

#ifdef NGK_SD_MAINTENANCE
struct FormatRequest { char line[96]; Snapshot expected; };
QueueHandle_t formatQueue = nullptr;

bool verifyOwnFile() {
    char path[64];
    snprintf(path, sizeof(path), "/ngk-check-%08lx.tmp", static_cast<unsigned long>(bootToken));
    if (SD.exists(path)) return false;
    const char expected[] = "NGK ECU SD CHECK\n";
    File file = SD.open(path, FILE_WRITE);
    if (!file) return false;
    bool okay = file.write(reinterpret_cast<const uint8_t*>(expected), sizeof(expected)) == sizeof(expected);
    file.flush(); file.close();
    file = SD.open(path, FILE_READ);
    char actual[sizeof(expected)]{};
    okay = file && file.readBytes(actual, sizeof(actual)) == sizeof(actual) &&
        !memcmp(expected, actual, sizeof(actual)) && okay;
    if (file) file.close();
    return SD.remove(path) && okay;
}

bool formatCard(SPIClass& spi, uint8_t drive, const Geometry& geometry) {
    // The worker holds this initialized card from CID validation to f_mkfs.
    void* work = malloc(4096);
    if (!work) { sdcard_uninit(drive); return false; }
    const bool fat32 = ecu::fat32ForCapacity(uint64_t(geometry.sectors) * SectorBytes);
    const char path[] = {char('0' + drive), ':', 0};
    esp_task_wdt_init(120, true);
    const FRESULT result = f_mkfs(path, fat32 ? FM_FAT32 : FM_FAT, fat32 ? 32768 : 0, work, 4096);
    esp_task_wdt_init(CONFIG_ESP_TASK_WDT_TIMEOUT_S, true);
    free(work);
    sdcard_uninit(drive);
    return result == FR_OK && mountCard(spi, geometry) && verifyOwnFile();
}
#endif

void storageWorker(void*) {
    SPIClass spi(VSPI);
    spi.begin(SdClock, SdMiso, SdMosi, SdSelect);
    ecu::profiles::workerBegin();
    Snapshot state;
    uint32_t lastAttempt = millis() - 5000;
#ifdef NGK_SD_MAINTENANCE
    ecu::MaintenancePolicy policy;
    uint64_t identifiedCard = 0;
    bool revoked = false;
#endif
    for (;;) {
        ecu::profiles::Request profileRequest;
        const bool hasProfileRequest = ecu::profiles::workerTake(profileRequest);
        const bool explicitRefresh = hasProfileRequest &&
            profileRequest.operation == ecu::profiles::Operation::RefreshSd;
#ifdef NGK_SD_MAINTENANCE
        // Keep periodic identity validation and the existing revocation policy
        // in maintenance; an explicit refresh performs the same guarded probe.
        const bool refreshCard = explicitRefresh || uint32_t(millis() - lastAttempt) >= 5000;
#else
        // A healthy mount stays open. Only explicit RefreshSd or a bounded
        // retry of an absent/unmounted card can remount it in normal operation.
        const bool refreshCard = ecu::shouldProbeStorage(state.mounted, explicitRefresh,
                                                        millis(), lastAttempt);
#endif
        if (refreshCard) {
            lastAttempt = millis();
            const uint32_t previousSectors = state.sectors;
            const uint64_t previousIdentity = state.identity;
            uint8_t drive = 0xff;
            Geometry geometry;
            SD.end();
            state.present = probe(spi, drive, geometry);
            if (drive != 0xff) sdcard_uninit(drive);
#ifdef NGK_SD_MAINTENANCE
            if (identifiedCard && (!state.present || geometry.identity != identifiedCard)) {
                policy.invalidateCard(); revoked = true;
            }
            if (geometry.identity && !identifiedCard) identifiedCard = geometry.identity;
#endif
            state.sectors = state.present ? geometry.sectors : 0;
            state.type = state.present ? geometry.type : 0;
            state.identity = state.present ? geometry.identity : 0;
            state.mounted = state.present && mountCard(spi, geometry);
            if (!state.present || !state.mounted || state.sectors != previousSectors ||
                state.identity != previousIdentity) state.verified = false;
#ifdef NGK_SD_MAINTENANCE
            state.formatReady = state.present && state.identity && !revoked && !policy.consumed();
#endif
            xQueueOverwrite(snapshots, &state);
        }
        if (hasProfileRequest) ecu::profiles::workerProcess(profileRequest, state.mounted);
#ifdef NGK_SD_MAINTENANCE
        FormatRequest request{};
        if (xQueueReceive(formatQueue, &request, pdMS_TO_TICKS(100)) == pdTRUE) {
            // All destructive checks and policy consumption belong to this worker.
            // A queue item holds exactly the snapshot shown to the requester.
            state.mounted = false;
            state.verified = false;
            state.formatState = 1;
            state.formatReady = false;
            xQueueOverwrite(snapshots, &state);
            SD.end();
            uint8_t drive = 0xff;
            Geometry current;
            const bool present = probe(spi, drive, current, 8000000);
            const bool same = present && request.expected.formatReady &&
                current.identity && current.identity == request.expected.identity &&
                current.sectors == request.expected.sectors && !revoked;
            const bool accepted = same && policy.request(request.line,
                request.expected.sectors, bootToken, present);
            bool okay = false;
            if (accepted) {
                okay = formatCard(spi, drive, current);
                drive = 0xff;
            } else if (!same) {
                policy.invalidateCard(); revoked = true;
            }
            if (drive != 0xff) sdcard_uninit(drive);
            state.present = present;
            state.sectors = present ? current.sectors : 0;
            state.type = present ? current.type : 0;
            state.identity = present ? current.identity : 0;
            state.mounted = accepted ? okay : (present && mountCard(spi, current));
            state.verified = okay;
            state.formatState = okay ? 2 : -1;
            state.formatReady = false;
            xQueueOverwrite(snapshots, &state);
            lastAttempt = millis();
        }
#else
        vTaskDelay(pdMS_TO_TICKS(100));
#endif
    }
}

ecu::radio::Snapshot radioView() {
    ecu::radio::Snapshot state;
#ifndef NGK_SD_MAINTENANCE
    ecu::radio::snapshot(state);
#endif
    return state;
}

void printRadios() {
    const auto state = radioView();
    // Deliberately omit SSIDs, network addresses, credentials and pairing digits.
    queuePrintf("{\"kind\":\"radios\",\"running\":%s,\"wifi_enabled\":%s,"
        "\"wifi_connected\":%s,\"wifi_connecting\":%s,\"wifi_scanning\":%s,"
        "\"ble_enabled\":%s,\"ble_advertising\":%s,\"ble_connected\":%s,"
        "\"ble_authenticated\":%s,\"ble_pairing_pending\":%s,\"memory_failure\":%s,"
        "\"status\":%u,\"heap_free\":%u,\"heap_min\":%u,\"heap_largest\":%u}\n",
        state.running ? "true" : "false", state.wifiEnabled ? "true" : "false",
        state.wifiConnected ? "true" : "false", state.wifiConnecting ? "true" : "false",
        state.wifiScanInProgress ? "true" : "false", state.bleEnabled ? "true" : "false",
        state.bleAdvertising ? "true" : "false", state.bleConnected ? "true" : "false",
        state.bleAuthenticated ? "true" : "false", state.blePairingPending ? "true" : "false",
        state.memoryFailure ? "true" : "false", unsigned(state.status), ESP.getFreeHeap(),
        ESP.getMinFreeHeap(), unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
}

void printInfo() {
    const Snapshot data = view;
    const auto radios = radioView();
    ecu::profiles::Snapshot profile;
    ecu::profiles::snapshot(profile);
    const auto simulation = ecu::simulation::snapshot();
#ifdef NGK_SD_MAINTENANCE
    constexpr unsigned simulationProtocol = 0;
#else
    constexpr unsigned simulationProtocol = ecu::simulation::protocolVersion;
#endif
    queuePrintf("{\"project\":\"NGK ECU\",\"firmware\":\"%s\",\"build\":\"%s\","
        "\"mode\":\"bench\",\"sensor_inputs_enabled\":false,\"actuators_enabled\":false,"
        "\"chip\":\"%s\",\"flash_bytes\":%u,\"psram_bytes\":%u,\"cpu_mhz\":%u,"
        "\"sd_present\":%s,\"sd_mounted\":%s,\"sd_rw_ok\":%s,"
        "\"sd_sectors\":%u,\"sd_mib\":%llu,\"sd_type\":%u,\"format_state\":%d,\"format_ready\":%s,"
        "\"heap_free\":%u,\"heap_min\":%u,\"heap_largest\":%u,"
        "\"wifi_enabled\":%s,\"wifi_connected\":%s,\"ble_enabled\":%s,"
        "\"ble_advertising\":%s,\"ble_connected\":%s,\"radio_memory_failure\":%s,"
        "\"profile_status\":%u,\"profile_revision\":%u,\"profile_persisted\":%s,"
        "\"simulation_protocol\":%u,\"simulation_enabled\":%s,\"simulation_live\":%s"
#ifdef NGK_SD_MAINTENANCE
        ",\"format_token\":%u"
#endif
        "}\r\n",
        Version,
#ifdef NGK_SD_MAINTENANCE
        "maintenance",
#else
        "normal",
#endif
        ESP.getChipModel(), ESP.getFlashChipSize(), ESP.getPsramSize(), ESP.getCpuFreqMHz(),
        data.present ? "true" : "false", data.mounted ? "true" : "false",
        data.verified ? "true" : "false", unsigned(data.sectors),
        uint64_t(data.sectors) * SectorBytes / (1024 * 1024), unsigned(data.type), data.formatState,
        data.formatReady ? "true" : "false", ESP.getFreeHeap(), ESP.getMinFreeHeap(),
        unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        radios.wifiEnabled ? "true" : "false", radios.wifiConnected ? "true" : "false",
        radios.bleEnabled ? "true" : "false", radios.bleAdvertising ? "true" : "false",
        radios.bleConnected ? "true" : "false", radios.memoryFailure ? "true" : "false",
        unsigned(profile.status), unsigned(profile.revision), profile.persisted ? "true" : "false",
        simulationProtocol, simulation.enabled ? "true" : "false", simulation.live ? "true" : "false"
#ifdef NGK_SD_MAINTENANCE
        , data.formatReady ? unsigned(bootToken) : 0U
#endif
    );
}

#ifndef NGK_SD_MAINTENANCE
void printSimulationStatus() {
    const auto value = ecu::simulation::snapshot();
    queuePrintf("{\"kind\":\"sim\",\"state\":\"%s\",\"protocol\":1,\"live\":%s,\"seq\":%u,\"faults\":%u}\n",
        !value.enabled ? "off" : value.live ? "live" : "stale", value.live ? "true" : "false",
        unsigned(value.frame.sequence), value.live ? unsigned(value.frame.faults) : 0U);
}
#endif

void command(const char* line) {
#ifndef NGK_SD_MAINTENANCE
    if (!strcmp(line, "sim-start") || !strncmp(line, "sim-start ", 10)) {
        uint32_t token = 0;
        if (line[9] && !ecu::simulation::parseSessionToken(line + 10, token)) {
            queueLine("{\"kind\":\"sim\",\"state\":\"rejected\",\"protocol\":1}");
            return;
        }
        const auto current = ecu::simulation::snapshot();
        const auto previous = current.frame.session;
        if (token && current.enabled && token == previous) {
            queueLine("{\"kind\":\"sim\",\"state\":\"rejected\",\"protocol\":1}");
            return;
        }
        if (!ecu::ui::startUsbSimulation()) {
            queueLine("{\"kind\":\"sim\",\"state\":\"busy\",\"protocol\":1}");
            return;
        }
        if (!token) do { token = esp_random(); } while (!token || token == previous);
        if (ecu::simulation::beginSession(token)) {
            queuePrintf("{\"kind\":\"sim\",\"state\":\"ready\",\"protocol\":1,\"session\":%u}\n", unsigned(token));
        } else queueLine("{\"kind\":\"sim\",\"state\":\"rejected\",\"protocol\":1}");
        return;
    }
    if (!strcmp(line, "sim-status")) { printSimulationStatus(); return; }
    if (!strncmp(line, "sim-stop ", 9)) {
        uint32_t token = 0;
        if (ecu::simulation::parseSessionToken(line + 9, token) && ecu::simulation::endSession(token)) printSimulationStatus();
        else queueLine("{\"kind\":\"sim\",\"state\":\"rejected\",\"protocol\":1}");
        return;
    }
    if (!strncmp(line, "SIM1 ", 5)) {
        ecu::simulation::Frame frame;
        if (!ecu::simulation::parseFrame(line, frame) || !ecu::simulation::submit(frame))
            queueLine("{\"kind\":\"sim\",\"state\":\"rejected\",\"protocol\":1}");
        return;
    }
#endif
    if (!strcmp(line, "info")) { printInfo(); return; }
    if (!strcmp(line, "radios") || !strcmp(line, "status")) { printRadios(); return; }
    if (!strcmp(line, "bench")) {
        queueLine(ecu::ui::startBenchmark() ? "BENCH_STARTED DEMO" : "BENCH_REFUSED");
        return;
    }
    if (!strcmp(line, "profile-export")) {
        queueLine(ecu::profiles::exportSd() ? "PROFILE_EXPORT_QUEUED" : "PROFILE_REFUSED queue");
        return;
    }
    if (!strcmp(line, "profile-import")) {
        queueLine(ecu::profiles::importSd() ? "PROFILE_IMPORT_QUEUED" : "PROFILE_REFUSED queue");
        return;
    }
    if (!strcmp(line, "sd-refresh")) {
        queueLine(ecu::profiles::refreshSd() ? "SD_REFRESH_QUEUED" : "SD_REFRESH_REFUSED queue");
        return;
    }
#ifndef NGK_SD_MAINTENANCE
    if (!strcmp(line, "wifi-on") || !strcmp(line, "wifi-off")) {
        queueLine(ecu::radio::enableWifi(!strcmp(line, "wifi-on")) ? "RADIO_QUEUED" : "RADIO_REFUSED");
        return;
    }
    if (!strcmp(line, "wifi-scan")) {
        queueLine(ecu::radio::scanWifi() ? "RADIO_QUEUED" : "RADIO_REFUSED");
        return;
    }
    if (!strcmp(line, "ble-on") || !strcmp(line, "ble-off")) {
        queueLine(ecu::radio::enableBle(!strcmp(line, "ble-on")) ? "RADIO_QUEUED" : "RADIO_REFUSED");
        return;
    }
    if (!strcmp(line, "ble-discover")) {
        queueLine(ecu::radio::discoverBle() ? "RADIO_QUEUED" : "RADIO_REFUSED");
        return;
    }
#endif
#ifdef NGK_SD_MAINTENANCE
    static bool submitted = false;
    if (!submitted && !strncmp(line, "FORMAT_SD ", 10) && view.formatReady) {
        FormatRequest request{};
        strlcpy(request.line, line, sizeof(request.line));
        request.expected = view;
        submitted = xQueueSend(formatQueue, &request, 0) == pdTRUE;
        queueLine(submitted ? "FORMAT_QUEUED" : "FORMAT_FAILED queue");
        return;
    }
#endif
    queueLine("ERROR command rejected");
}

void serialCommands() {
    static ecu::CommandLine parser;
    if (serialTx.pending()) return;
    for (unsigned consumed = 0; consumed < 128 && Serial.available(); ++consumed) {
        const auto character = char(Serial.read());
        const auto result = parser.push(character);
        if (result == ecu::CommandLine::Result::Ready) command(parser.value());
        else if (result == ecu::CommandLine::Result::Rejected) queueLine("ERROR invalid command line");
        // One terminal line per UI iteration, including an empty line.
        if (character == '\n') return;
    }
}
} // namespace

void setup() {
    Serial.begin(115200);
    bootToken = esp_random();
    if (!bootToken) bootToken = 1;
    snapshots = xQueueCreate(1, sizeof(Snapshot));
    if (!snapshots) halt("FATAL snapshot queue");
    if (!ecu::profiles::begin()) halt("FATAL profiles queue");
    if (!ecu::ui::begin()) halt("FATAL UI initialization");
#ifndef NGK_SD_MAINTENANCE
    // Radio work starts only after LVGL has its owner and initial allocation.
    // A radio allocation failure must leave the dashboard usable.
    if (!ecu::radio::begin()) queueLine("RADIO_UNAVAILABLE memory");
#endif
#ifdef NGK_SD_MAINTENANCE
    formatQueue = xQueueCreate(1, sizeof(FormatRequest));
    if (!formatQueue) halt("FATAL storage queue");
#endif
    if (xTaskCreatePinnedToCore(storageWorker, "ngk-sd", 8192, nullptr, 1, nullptr, 0) != pdPASS) {
        halt("FATAL storage worker");
    }
    queueLine("NGK ECU BENCH: sensor inputs and actuators disabled");
    printInfo();
}

void loop() {
    xQueuePeek(snapshots, &view, 0);
    serialCommands();
    ecu::ui::setStorage(view.mounted, uint32_t(uint64_t(view.sectors) * SectorBytes / (1024 * 1024)));
    ecu::ui::tick();
    static ecu::ui::BenchmarkReport report{};
    static bool reportPending = false;
    if (!reportPending) reportPending = ecu::ui::takeBenchmarkReport(report);
    if (reportPending) {
        const bool queued = queuePrintf("{\"kind\":\"ui_benchmark\",\"synthetic\":true,\"duration_ms\":%u,"
            "\"presentations\":%u,\"partial_p95_us\":%u,\"partial_max_us\":%u,"
            "\"touch_max_gap_us\":%u,\"full_p95_us\":%u,\"heap_free\":%u,"
            "\"heap_min\":%u,\"heap_largest\":%u,\"lvgl_used_pct\":%u,\"lvgl_frag_pct\":%u}\n",
            report.durationMs, report.presentations, report.partialP95Us, report.partialMaxUs,
            report.touchMaxGapUs, report.fullP95Us, report.heapFree, report.heapMin,
            report.heapLargest, report.lvglUsedPercent, report.lvglFragPercent);
        if (queued) reportPending = false;
    }
    static int lastFormatState = 0;
    if (view.formatState != lastFormatState) {
        bool queued = true;
        if (view.formatState == 2) queued = queueLine("FORMAT_DONE OK");
        if (view.formatState == -1) queued = queueLine("FORMAT_DONE FAILED");
        if (queued) lastFormatState = view.formatState;
    }
    pumpSerial();
    delay(1);
}
