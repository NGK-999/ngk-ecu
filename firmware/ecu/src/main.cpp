#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <cstring>
#include <esp_system.h>
#include <sd_diskio.h>
extern "C" {
#include <ff.h>
#include <diskio.h>
}
#include "command_line.hpp"
#include "ui.hpp"
#ifdef NGK_SD_MAINTENANCE
#include <esp_task_wdt.h>
#include "card_probe.hpp"
#include "maintenance_policy.hpp"
#endif

namespace {
constexpr int SdClock = 18, SdMiso = 19, SdMosi = 23, SdSelect = 5;
constexpr uint32_t SectorBytes = 512;
constexpr const char* Version = "0.1.1-bench";
struct Snapshot {
    bool present = false, mounted = false, verified = false, formatReady = false;
    uint32_t sectors = 0, type = 0;
    uint64_t identity = 0; // RAM only. Never printed or persisted.
    int formatState = 0; // 0 idle, 1 active, 2 success, -1 failed
};
QueueHandle_t snapshots = nullptr;
Snapshot view;
uint32_t bootToken = 0;

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
    Snapshot state;
    uint32_t lastAttempt = millis() - 5000;
#ifdef NGK_SD_MAINTENANCE
    ecu::MaintenancePolicy policy;
    uint64_t identifiedCard = 0;
    bool revoked = false;
#endif
    for (;;) {
        if (uint32_t(millis() - lastAttempt) >= 5000) {
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

void printInfo() {
    const Snapshot data = view;
    Serial.printf("{\"project\":\"NGK ECU\",\"firmware\":\"%s\",\"build\":\"%s\","
        "\"mode\":\"bench\",\"sensor_inputs_enabled\":false,\"actuators_enabled\":false,"
        "\"chip\":\"%s\",\"flash_bytes\":%u,\"psram_bytes\":%u,\"cpu_mhz\":%u,"
        "\"sd_present\":%s,\"sd_mounted\":%s,\"sd_rw_ok\":%s,"
        "\"sd_sectors\":%u,\"sd_mib\":%llu,\"sd_type\":%u,\"format_state\":%d,\"format_ready\":%s",
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
        data.formatReady ? "true" : "false");
#ifdef NGK_SD_MAINTENANCE
    Serial.printf(",\"format_token\":%u", data.formatReady ? unsigned(bootToken) : 0U);
#endif
    Serial.println("}");
}

void command(const char* line) {
    if (!strcmp(line, "info")) { printInfo(); return; }
#ifdef NGK_SD_MAINTENANCE
    static bool submitted = false;
    if (!submitted && !strncmp(line, "FORMAT_SD ", 10) && view.formatReady) {
        FormatRequest request{};
        strlcpy(request.line, line, sizeof(request.line));
        request.expected = view;
        submitted = xQueueSend(formatQueue, &request, 0) == pdTRUE;
        Serial.println(submitted ? "FORMAT_QUEUED" : "FORMAT_FAILED queue");
        return;
    }
#endif
    Serial.println("ERROR command rejected");
}

void serialCommands() {
    static ecu::CommandLine parser;
    while (Serial.available()) {
        const auto result = parser.push(char(Serial.read()));
        if (result == ecu::CommandLine::Result::Ready) command(parser.value());
        else if (result == ecu::CommandLine::Result::Rejected) Serial.println("ERROR invalid command line");
    }
}
} // namespace

void setup() {
    Serial.begin(115200);
    bootToken = esp_random();
    if (!bootToken) bootToken = 1;
    snapshots = xQueueCreate(1, sizeof(Snapshot));
    if (!snapshots) { Serial.println("FATAL snapshot queue"); while (true) delay(1000); }
    if (!ecu::ui::begin()) { Serial.println("FATAL UI initialization"); while (true) delay(1000); }
#ifdef NGK_SD_MAINTENANCE
    formatQueue = xQueueCreate(1, sizeof(FormatRequest));
    if (!formatQueue) { Serial.println("FATAL storage queue"); while (true) delay(1000); }
#endif
    if (xTaskCreatePinnedToCore(storageWorker, "ngk-sd", 8192, nullptr, 1, nullptr, 0) != pdPASS) {
        Serial.println("FATAL storage worker"); while (true) delay(1000);
    }
    Serial.println("NGK ECU BENCH: sensor inputs and actuators disabled");
    printInfo();
}

void loop() {
    xQueuePeek(snapshots, &view, 0);
    serialCommands();
    ecu::ui::setStorage(view.mounted, uint32_t(uint64_t(view.sectors) * SectorBytes / (1024 * 1024)));
    ecu::ui::tick();
    static int lastFormatState = 0;
    if (view.formatState != lastFormatState) {
        lastFormatState = view.formatState;
        if (view.formatState == 2) Serial.println("FORMAT_DONE OK");
        if (view.formatState == -1) Serial.println("FORMAT_DONE FAILED");
    }
    delay(1);
}
