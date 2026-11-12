#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <driver/uart.h>
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
#ifdef NGK_LOAD_BENCH
#include "load_bench.hpp"
#include <esp_timer.h>
#endif
#ifdef NGK_SD_IO_BENCH
#include "sd_io_service.hpp"
#include "card_probe.hpp"
#include <esp_timer.h>
#endif
#ifdef NGK_SD_MAINTENANCE
#include <esp_task_wdt.h>
#include "card_probe.hpp"
#include "maintenance_policy.hpp"
#endif

namespace {
constexpr int SdClock = 18, SdMiso = 19, SdMosi = 23, SdSelect = 5;
constexpr uint32_t SectorBytes = 512;
#ifdef NGK_LOAD_BENCH
constexpr const char* Version = "0.4.11-load-bench";
TaskHandle_t loadMainTask = nullptr, loadSdTask = nullptr;
#elif defined(NGK_SD_IO_BENCH)
constexpr const char* Version = "0.4.11-sd-io-bench";
#elif defined(NGK_COMBINED_NUMBERS)
constexpr const char* Version = "0.4.11-reference";
#else
constexpr const char* Version = "0.4.11-resource";
#endif
#if defined(NGK_SD_IO_BENCH) || defined(NGK_LOAD_BENCH)
uint64_t motionObservedStartUs = 0, motionObservedEndUs = 0;
#endif
struct Snapshot {
    bool present = false, mounted = false, verified = false, formatReady = false;
    uint32_t sectors = 0, type = 0;
    uint64_t identity = 0; // RAM only. Never printed or persisted.
    int formatState = 0; // 0 idle, 1 active, 2 success, -1 failed
#ifdef NGK_SD_IO_BENCH
    uint32_t clockHz = 0;
#endif
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
    if (!serialTx.pending()) return;
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
#if defined(NGK_SD_MAINTENANCE) || defined(NGK_SD_IO_BENCH)
    // Failure still permits diagnostics, but never formatting.
    ecu::readCardIdentity(spi, SdSelect, geometry.identity);
#endif
    return true;
}

bool mountCard(SPIClass& spi, const Geometry& geometry, uint32_t* observedClock = nullptr) {
    SD.end();
    uint32_t frequency = 8000000;
    bool okay = SD.begin(SdSelect, spi, 8000000, "/sdcard", 3, false);
    if (!okay) {
        SD.end();
        frequency = 1000000;
        okay = SD.begin(SdSelect, spi, 1000000, "/sdcard", 3, false);
    }
    if (okay && SD.cardSize() / SectorBytes != geometry.sectors) {
        SD.end(); okay = false;
    }
    if (observedClock) *observedClock = okay ? frequency : 0;
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
            state.mounted = state.present && mountCard(spi, geometry
#ifdef NGK_SD_IO_BENCH
                , &state.clockHz
#endif
            );
            if (!state.present || !state.mounted || state.sectors != previousSectors ||
                state.identity != previousIdentity) state.verified = false;
#ifdef NGK_SD_MAINTENANCE
            state.formatReady = state.present && state.identity && !revoked && !policy.consumed();
#endif
            xQueueOverwrite(snapshots, &state);
        }
        if (hasProfileRequest) ecu::profiles::workerProcess(profileRequest, state.mounted);
#ifdef NGK_SD_IO_BENCH
        ecu::sdbench::workerPoll(state.mounted, state.sectors, spi, state.identity);
#endif
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
        "\"status\":%u,\"worker_wakeups\":%u,\"heap_free\":%u,\"heap_min\":%u,\"heap_largest\":%u}\n",
        state.running ? "true" : "false", state.wifiEnabled ? "true" : "false",
        state.wifiConnected ? "true" : "false", state.wifiConnecting ? "true" : "false",
        state.wifiScanInProgress ? "true" : "false", state.bleEnabled ? "true" : "false",
        state.bleAdvertising ? "true" : "false", state.bleConnected ? "true" : "false",
        state.bleAuthenticated ? "true" : "false", state.blePairingPending ? "true" : "false",
        state.memoryFailure ? "true" : "false", unsigned(state.status), state.workerWakeups, ESP.getFreeHeap(),
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
    constexpr unsigned screenCaptureProtocol = 0;
#else
    constexpr unsigned simulationProtocol = ecu::simulation::protocolVersion;
    constexpr unsigned screenCaptureProtocol = 1;
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
        "\"simulation_protocol\":%u,\"simulation_enabled\":%s,\"simulation_live\":%s,"
        "\"refresh_mode\":%u,\"reduced_motion\":%s,\"screen_capture_protocol\":%u"
#ifdef NGK_LOAD_BENCH
        ",\"load_benchmark_protocol\":1"
#endif
#ifdef NGK_SD_IO_BENCH
        ",\"sd_io_benchmark_protocol\":1,\"sd_spi_hz\":%u"
#endif
#ifdef NGK_SD_MAINTENANCE
        ",\"format_token\":%u"
#endif
        "}\r\n",
        Version,
#ifdef NGK_SD_MAINTENANCE
        "maintenance",
#elif defined(NGK_LOAD_BENCH)
        "load-benchmark",
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
        simulationProtocol, simulation.enabled ? "true" : "false", simulation.live ? "true" : "false",
        unsigned(ecu::ui::presentationRefreshMode()), ecu::ui::reducedMotionEnabled() ? "true" : "false",
        screenCaptureProtocol
#ifdef NGK_SD_IO_BENCH
        , data.clockHz
#endif
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
#ifdef NGK_LOAD_BENCH
    if (!strcmp(line, "load-status")) {
        const auto state = ecu::loadbench::snapshot();
        const auto parameters = ecu::loadbench::config(state.control.request.mode);
        const auto& counts = state.mailbox;
        queuePrintf("{\"kind\":\"load_benchmark\",\"protocol\":1,\"id\":%u,\"mode\":%u,\"state\":\"%s\","
            "\"duration_ms\":%u,\"elapsed_us\":%llu,\"phase_started_us\":%llu,\"phase_finished_us\":%llu,"
            "\"channels\":%u,\"rate_hz\":%u,\"iterations\":%u,"
            "\"produced\":%llu,\"consumed\":%llu,\"overwritten\":%llu,\"pending\":%u,\"sequence_gaps\":%llu,"
            "\"missed_periods\":%llu,\"dispatch_max_us\":%llu,\"consume_max_us\":%llu,"
            "\"kernel_total_us\":%llu,\"kernel_max_us\":%llu,\"checksum\":%u,"
            "\"heap_free_bytes\":%u,\"heap_min_bytes\":%u,\"heap_largest_bytes\":%u,"
            "\"stack_main_min_bytes\":%u,\"stack_sd_min_bytes\":%u,"
            "\"stack_radio_min_bytes\":%u,\"stack_load_min_bytes\":%u}\n",
            state.control.request.id, unsigned(state.control.request.mode), ecu::loadbench::stateText(state.control.state),
            state.control.request.durationMs, static_cast<unsigned long long>(state.elapsedUs),
            static_cast<unsigned long long>(state.startedUs), static_cast<unsigned long long>(state.finishedUs),
            parameters.channels, parameters.rateHz, parameters.iterations,
            static_cast<unsigned long long>(counts.produced), static_cast<unsigned long long>(counts.consumed),
            static_cast<unsigned long long>(counts.overwritten), unsigned(counts.pending), static_cast<unsigned long long>(counts.sequenceGaps),
            static_cast<unsigned long long>(state.schedule.missed), static_cast<unsigned long long>(state.schedule.maxLateUs),
            static_cast<unsigned long long>(counts.consumeMaxUs), static_cast<unsigned long long>(state.kernelTotalUs),
            static_cast<unsigned long long>(state.kernelMaxUs), state.checksum, state.heapFreeBytes, state.heapMinBytes,
            state.heapLargestBytes, state.stackMainMinBytes, state.stackSdMinBytes, state.stackRadioMinBytes, state.stackLoadMinBytes);
        return;
    }
    if (!strcmp(line, "load-start") || !strncmp(line, "load-start ", 11)) {
        ecu::loadbench::Request request;
        queueLine(ecu::loadbench::parseRequest(line, request) && ecu::loadbench::start(request) ? "LOAD_QUEUED" : "LOAD_REFUSED");
        return;
    }
    if (!strcmp(line, "load-stop")) {
        queueLine(ecu::loadbench::stop() ? "LOAD_STOP_QUEUED" : "LOAD_REFUSED");
        return;
    }
#endif
#ifdef NGK_SD_IO_BENCH
    if (!strcmp(line, "sd-io-status")) {
        const auto state = ecu::sdbench::snapshot();
        const auto& r = state.result;
        queuePrintf("{\"kind\":\"sd_io_benchmark\",\"id\":%u,\"mode\":%u,\"state\":\"%s\","
            "\"phase_started_us\":%llu,\"phase_finished_us\":%llu,\"okay\":%s,"
            "\"created\":%s,\"removed\":%s,\"card_consistent\":%s,\"error\":%u,"
            "\"setup_ms\":%u,\"duration_ms\":%u,\"stack_min_bytes\":%u,"
            "\"read_ops\":%u,\"read_bytes\":%llu,\"read_total_us\":%llu,\"read_p95_upper_us\":%u,\"read_max_us\":%u,"
            "\"write_ops\":%u,\"write_bytes\":%llu,\"write_total_us\":%llu,\"write_p95_upper_us\":%u,\"write_max_us\":%u,"
            "\"sync_ops\":%u,\"sync_bytes\":%llu,\"sync_total_us\":%llu,\"sync_p95_upper_us\":%u,\"sync_max_us\":%u}\n",
            state.request.id, unsigned(state.request.mode), ecu::sdbench::stateText(state.state),
            static_cast<unsigned long long>(state.phaseStartedUs), static_cast<unsigned long long>(state.phaseFinishedUs),
            r.okay ? "true" : "false", r.created ? "true" : "false", r.removed ? "true" : "false",
            state.cardConsistent ? "true" : "false", r.error, r.setupMs, r.durationMs, state.stackMinBytes,
            r.read.operations, static_cast<unsigned long long>(r.read.bytes), static_cast<unsigned long long>(r.read.totalUs), r.read.p95UpperUs, r.read.maxUs,
            r.write.operations, static_cast<unsigned long long>(r.write.bytes), static_cast<unsigned long long>(r.write.totalUs), r.write.p95UpperUs, r.write.maxUs,
            r.sync.operations, static_cast<unsigned long long>(r.sync.bytes), static_cast<unsigned long long>(r.sync.totalUs), r.sync.p95UpperUs, r.sync.maxUs);
        return;
    }
    if (!strncmp(line, "sd-io-bench", 11)) {
        ecu::sdbench::Request request{};
        const auto sim = ecu::simulation::snapshot(); // This state belongs to main/UI.
        ecu::radio::Snapshot radios{};
        const bool radiosReady = ecu::radio::snapshot(radios) && radios.running;
        const bool accepted = ecu::sdbench::parseRequest(line, request) &&
            sim.enabled && sim.live && view.mounted && view.sectors == request.sectors &&
            radiosReady && !radios.wifiEnabled && !radios.bleEnabled && ecu::sdbench::request(request);
        queueLine(accepted ? "SD_IO_QUEUED" : "SD_IO_REFUSED");
        return;
    }
#endif
#ifndef NGK_SD_MAINTENANCE
    if (!strcmp(line, "screen-capture")) {
        // serialCommands accepts no next line while the bounded outbox is pending.
        // Drain bytes already owned by UART before sending the binary header.
        if (serialTx.pending() || uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(1000)) != ESP_OK) {
            queueLine("{\"kind\":\"screen_capture\",\"error\":\"screen_capture_tx_busy\"}");
        } else if (!ecu::ui::captureScreen()) {
            queueLine("{\"kind\":\"screen_capture\",\"error\":\"screen_capture_blocked\"}");
        }
        return;
    }
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
    if (!strcmp(line, "motion-bench")) {
#if defined(NGK_SD_IO_BENCH) || defined(NGK_LOAD_BENCH)
        const uint64_t before = esp_timer_get_time();
#endif
        const bool started = ecu::ui::startMotionBenchmark();
#if defined(NGK_SD_IO_BENCH) || defined(NGK_LOAD_BENCH)
        if (started) motionObservedStartUs = before;
#endif
        queueLine(started ? "MOTION_BENCH_STARTED" : "MOTION_BENCH_REFUSED");
        return;
    }
    if (!strcmp(line, "ui-smooth")) {
        queueLine(ecu::ui::requestSmoothPresentation() ? "UI_SMOOTH_QUEUED" : "UI_SMOOTH_REFUSED");
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
#ifdef NGK_LOAD_BENCH
    loadMainTask = xTaskGetCurrentTaskHandle();
#endif
    Serial.begin(115200);
    bootToken = esp_random();
    if (!bootToken) bootToken = 1;
    snapshots = xQueueCreate(1, sizeof(Snapshot));
    if (!snapshots) halt("FATAL snapshot queue");
    if (!ecu::profiles::begin()) halt("FATAL profiles queue");
#ifdef NGK_SD_IO_BENCH
    if (!ecu::sdbench::begin()) halt("FATAL SD benchmark queues");
#endif
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
    if (xTaskCreatePinnedToCore(storageWorker, "ngk-sd",
#ifdef NGK_SD_IO_BENCH
        12288,
#else
        8192,
#endif
        nullptr, 1,
#ifdef NGK_LOAD_BENCH
        &loadSdTask,
#else
        nullptr,
#endif
        0) != pdPASS) {
        halt("FATAL storage worker");
    }
#ifdef NGK_LOAD_BENCH
    if (!ecu::loadbench::begin(loadMainTask, loadSdTask)) halt("FATAL load benchmark worker");
#endif
    queueLine("NGK ECU BENCH: sensor inputs and actuators disabled");
    printInfo();
}

void loop() {
#ifdef NGK_LOAD_BENCH
    ecu::loadbench::poll();
#endif
    const bool storageChanged = xQueueReceive(snapshots, &view, 0) == pdTRUE;
    serialCommands();
    if (storageChanged)
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
    static ecu::ui::MotionBenchmarkReport motionReport{};
    static bool motionReportPending = false;
    if (!motionReportPending) {
        motionReportPending = ecu::ui::takeMotionBenchmarkReport(motionReport);
#if defined(NGK_SD_IO_BENCH) || defined(NGK_LOAD_BENCH)
        if (motionReportPending) motionObservedEndUs = esp_timer_get_time();
#endif
    }
    if (motionReportPending) {
        if (queuePrintf("{\"kind\":\"ui_motion_benchmark\",\"synthetic\":true,\"duration_ms\":%u,"
            "\"presentations\":%u,\"interval_p95_us\":%u,\"interval_max_us\":%u,"
            "\"max_pixels\":%u,\"transferred_pixels\":%u,\"touch_poll_max_gap_us\":%u,"
            "\"ui_tick_total_us\":%u,\"ui_tick_max_us\":%u,\"refresh_mode\":%u,"
            "\"reduced_motion\":%s,\"ramp_updates\":%u,\"ramp_gap_p95_upper_us\":%u,"
            "\"ramp_gap_max_us\":%u,\"instrument_passes\":%u,\"number_passes\":%u,"
            "\"rpm_number_passes\":%u,\"speed_number_passes\":%u,"
            "\"rpm_number_gap_max_us\":%u,\"speed_number_gap_max_us\":%u,"
            "\"instrument_gap_max_us\":%u,\"ramp_stable_passes\":%u,"
            "\"ramp_gap_without_stable_max_us\":%u,\"ramp_gap_with_stable_max_us\":%u,"
            "\"detail_passes\":%u,\"idle_skips\":%u,\"sample_presentations\":%u,"
            "\"sample_to_present_p95_ms\":%u,\"sample_to_present_max_ms\":%u,\"interrupted\":%s"
#if defined(NGK_SD_IO_BENCH) || defined(NGK_LOAD_BENCH)
            ",\"observed_start_us\":%llu,\"observed_end_us\":%llu"
#endif
            "}\n",
            motionReport.durationMs, motionReport.presentations, motionReport.intervalP95Us,
            motionReport.intervalMaxUs, motionReport.maxPixels, motionReport.transferredPixels, motionReport.touchPollMaxGapUs,
            motionReport.uiTickTotalUs, motionReport.uiTickMaxUs,
            unsigned(ecu::ui::presentationRefreshMode()), ecu::ui::reducedMotionEnabled() ? "true" : "false",
            motionReport.rampUpdates, motionReport.rampGapP95UpperUs, motionReport.rampGapMaxUs,
            motionReport.instrumentPasses, motionReport.numberPasses,
            motionReport.rpmNumberPasses, motionReport.speedNumberPasses,
            motionReport.rpmNumberGapMaxUs, motionReport.speedNumberGapMaxUs,
            motionReport.instrumentGapMaxUs, motionReport.rampStablePasses,
            motionReport.rampGapWithoutStableMaxUs, motionReport.rampGapWithStableMaxUs, motionReport.detailPasses,
            motionReport.idleSkips, motionReport.samplePresentations,
            motionReport.sampleToPresentP95Ms, motionReport.sampleToPresentMaxMs,
            motionReport.interrupted ? "true" : "false"
#if defined(NGK_SD_IO_BENCH) || defined(NGK_LOAD_BENCH)
            , static_cast<unsigned long long>(motionObservedStartUs), static_cast<unsigned long long>(motionObservedEndUs)
#endif
        )) motionReportPending = false;
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
