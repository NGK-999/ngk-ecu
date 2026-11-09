#include "profile_service.hpp"
#include "profile_store.hpp"
#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <array>
#include <cstring>

namespace ecu::profiles {
namespace {
QueueHandle_t requests = nullptr, snapshots = nullptr;
Snapshot state;

bool enqueue(Operation operation, const UiSettings& settings = UiSettings{}) {
    if (!requests) return false;
    const Request request{operation, settings};
    return xQueueSend(requests, &request, 0) == pdTRUE;
}
void publish(Status status) {
    state.status = status;
    state.ready = true;
    ++state.revision;
    xQueueOverwrite(snapshots, &state);
}
bool readNvs(UiSettings& settings) {
    Preferences preferences;
    if (!preferences.begin("ngk-ui", true)) return false;
    const auto size = preferences.getBytesLength("settings-v1");
    std::array<char, kMaxSettingsEncodedSize + 1> bytes{};
    const bool read = size && size <= kMaxSettingsEncodedSize &&
        preferences.getBytes("settings-v1", bytes.data(), size) == size;
    preferences.end();
    return read && decodeSettings(bytes.data(), size, settings);
}
bool writeNvs(const UiSettings& settings) {
    std::array<char, kMaxSettingsEncodedSize + 1> bytes{};
    const auto size = encodeSettings(settings, bytes.data(), bytes.size());
    if (!size) return false;
    Preferences preferences;
    if (!preferences.begin("ngk-ui", false)) return false;
    const bool written = preferences.putBytes("settings-v1", bytes.data(), size) == size;
    preferences.end();
    UiSettings check;
    return written && readNvs(check) && check == settings;
}
struct SdFiles {
    bool directory() {
        if (!SD.exists("/ngk-ecu")) return SD.mkdir("/ngk-ecu");
        File file = SD.open("/ngk-ecu", FILE_READ);
        const bool okay = file && file.isDirectory();
        if (file) file.close();
        return okay;
    }
    bool exists(const char* path) { return SD.exists(path); }
    bool remove(const char* path) { return SD.remove(path); }
    bool rename(const char* from, const char* to) { return SD.rename(from, to); }
    bool write(const char* path, const char* bytes, std::size_t size) {
        File file = SD.open(path, FILE_WRITE);
        if (!file || file.isDirectory()) { if (file) file.close(); return false; }
        const bool okay = file.write(reinterpret_cast<const uint8_t*>(bytes), size) == size;
        file.flush(); file.close();
        return okay;
    }
    bool read(const char* path, char* bytes, std::size_t capacity, std::size_t& size) {
        File file = SD.open(path, FILE_READ);
        if (!file || file.isDirectory() || file.size() > capacity) {
            if (file) file.close();
            return false;
        }
        size = file.size();
        const bool okay = file.readBytes(bytes, size) == size;
        file.close();
        return okay;
    }
};
}

bool begin() {
    if (requests && snapshots) return true;
    requests = xQueueCreate(4, sizeof(Request));
    snapshots = xQueueCreate(1, sizeof(Snapshot));
    if (!requests || !snapshots) {
        if (requests) vQueueDelete(requests);
        if (snapshots) vQueueDelete(snapshots);
        requests = snapshots = nullptr;
        return false;
    }
    xQueueOverwrite(snapshots, &state);
    return true;
}
bool snapshot(Snapshot& output) { return snapshots && xQueuePeek(snapshots, &output, 0) == pdTRUE; }
bool apply(const UiSettings& settings) { return validSettings(settings) && enqueue(Operation::Apply, settings); }
bool exportSd() { return enqueue(Operation::ExportSd); }
bool importSd() { return enqueue(Operation::ImportSd); }
bool refreshSd() { return enqueue(Operation::RefreshSd); }
bool workerTake(Request& output) { return requests && xQueueReceive(requests, &output, 0) == pdTRUE; }

void workerBegin() {
    UiSettings loaded;
    if (readNvs(loaded)) {
        state.settings = loaded; state.persisted = true; publish(Status::Loaded);
    } else publish(Status::Defaults);
}

void workerProcess(const Request& request, bool cardMounted) {
    if (request.operation == Operation::Apply) {
        if (!validSettings(request.settings)) { publish(Status::InvalidSettings); return; }
        state.settings = request.settings;
        state.persisted = writeNvs(state.settings);
        publish(state.persisted ? Status::Saved : Status::StorageError);
        return;
    }
    if (request.operation == Operation::RefreshSd) {
        publish(cardMounted ? Status::CardRefreshed : Status::NoCard);
        return;
    }
    if (!cardMounted) { publish(Status::NoCard); return; }
    SdFiles files;
    if (request.operation == Operation::ExportSd) {
        publish(saveProfile(files, state.settings) ? Status::Exported : Status::WriteError);
        return;
    }
    UiSettings loaded;
    bool recovered = false;
    if (!loadProfile(files, loaded, recovered)) {
        publish(files.exists(primaryPath) || files.exists(backupPath) ? Status::ReadError : Status::MissingProfile);
        return;
    }
    if (!writeNvs(loaded)) { publish(Status::StorageError); return; }
    state.settings = loaded;
    state.persisted = true;
    publish(recovered ? Status::RecoveredBackup : Status::Imported);
}

const char* statusText(Status status) {
    switch (status) {
        case Status::Defaults: return "Padroes desta sessao";
        case Status::Loaded: return "Ajustes restaurados";
        case Status::Saved: return "Ajustes salvos";
        case Status::Exported: return "Perfil salvo no SD";
        case Status::Imported: return "Perfil importado";
        case Status::RecoveredBackup: return "Perfil recuperado do backup";
        case Status::InvalidSettings: return "Valores invalidos";
        case Status::NoCard: return "SD nao montado";
        case Status::MissingProfile: return "Nenhum perfil no SD";
        case Status::ReadError: return "Perfil invalido ou ilegivel";
        case Status::WriteError: return "Falha ao exportar perfil";
        case Status::StorageError: return "Nao foi possivel salvar na placa";
        case Status::CardRefreshed: return "SD atualizado";
    }
    return "Estado indisponivel";
}
}
