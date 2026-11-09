#pragma once
#include "settings.hpp"
#include <cstdint>

namespace ecu::profiles {
enum class Status : std::uint8_t {
    Defaults, Loaded, Saved, Exported, Imported, RecoveredBackup,
    InvalidSettings, NoCard, MissingProfile, ReadError, WriteError, StorageError, CardRefreshed
};
struct Snapshot {
    UiSettings settings{};
    Status status = Status::Defaults;
    std::uint32_t revision = 0;
    bool ready = false, persisted = false;
};
enum class Operation : std::uint8_t { Apply, ExportSd, ImportSd, RefreshSd };
struct Request { Operation operation{}; UiSettings settings{}; };
// Public requests enqueue without waiting; the storage worker owns I/O.
// false is an immediate rejection (invalid input/unavailable/full queue): no
// revision follows. Every accepted request publishes one completion revision.
bool begin();
bool snapshot(Snapshot& output);
bool apply(const UiSettings& settings);
bool exportSd();
bool importSd();
bool refreshSd();
const char* statusText(Status status);
// Call only from the sole storage worker, never from an LVGL callback.
void workerBegin();
bool workerTake(Request& output);
void workerProcess(const Request& request, bool cardMounted);
}
