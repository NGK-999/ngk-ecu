#include "ui.hpp"
#include "board.hpp"
#include "ui_layout.hpp"
#include "perf_stats.hpp"
#include "profile_service.hpp"
#include "radio.hpp"
#include "cockpit_fonts.h"
#include "simulation_service.hpp"
#include "simulation_view.hpp"

#include <Preferences.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <lvgl.h>

namespace ecu::ui {
static_assert(sizeof(lv_color_t) == 2 && LV_COLOR_16_SWAP == 1,
              "Display DMA requires byte-swapped RGB565");
namespace {
constexpr std::uint32_t background = 0x06090D;
constexpr std::uint32_t card = 0x101820;
constexpr std::uint32_t buttonColor = 0x131E28;
constexpr std::uint32_t text = 0xFFFFFF;
constexpr std::uint32_t secondary = 0x8F9DAA;
constexpr std::uint32_t accent = 0x35D9D0;
constexpr std::uint32_t danger = 0xFF4D5A;
constexpr std::uint32_t border = 0x283642;
const lv_font_t* const rpmDigits = &ngk_cockpit_64;
const lv_font_t* const speedDigits = &ngk_cockpit_72;
constexpr std::uint32_t warning = 0xFFC857;
constexpr const char* unknown = "--";
constexpr const char* noSignal = "SEM SINAL";
enum class Page : std::uint8_t { Menu, Panel, Sensors, Settings, System, Networks, Keyboard, Confirm, SettingsHome };

Display display;
ResistiveTouch touch;
Calibration calibration;
TouchDebouncer debounce;
DashboardDoubleTap dashboardGesture;
Point gesturePosition{};
lv_disp_draw_buf_t drawBuffer;
lv_disp_drv_t displayDriver;
lv_indev_drv_t inputDriver;
lv_disp_t* registeredDisplay = nullptr;
lv_disp_drv_t* pendingFlush = nullptr;
bool pendingFlushLast = false;
lv_color_t* bufferA = nullptr;
lv_color_t* bufferB = nullptr;
lv_obj_t* pageContent = nullptr;
lv_obj_t* storageLabel = nullptr;
lv_obj_t* menuStatusLabel = nullptr;
lv_obj_t* speedValue = nullptr;
lv_obj_t* rpmValue = nullptr;
lv_obj_t* speedSignal = nullptr;
lv_obj_t* rpmSignal = nullptr;
lv_obj_t* modeLabel = nullptr;
lv_obj_t* unitLabel = nullptr;
lv_obj_t* settingsStateLabel = nullptr;
lv_obj_t* detailLabel = nullptr;
lv_obj_t* keyboardValue = nullptr;
lv_obj_t* temperatureValue = nullptr;
lv_obj_t* oilValue = nullptr;
lv_obj_t* rpmScaleLabel = nullptr;
lv_obj_t* gearLabel = nullptr;
lv_obj_t* gearValue = nullptr;
lv_obj_t* dashboardHintLabel = nullptr;
lv_obj_t* faultFooterLabel = nullptr;
lv_obj_t* sensorSourceLabel = nullptr;
std::array<lv_obj_t*, 4> sensorValues{};
std::array<lv_obj_t*, 3> settingValues{}, networkLabels{}, networkButtons{};
std::array<lv_obj_t*, 18> keyLabels{}, keyButtons{};
std::array<lv_obj_t*, 20> rpmSegments{};
std::array<std::uint32_t, 20> segmentColors{};
lv_obj_t* pairOverlay = nullptr;
lv_obj_t* pairCodeLabel = nullptr;
lv_obj_t* pairStatusLabel = nullptr;
lv_obj_t* pairAccept = nullptr;
lv_obj_t* pairReject = nullptr;
lv_obj_t* pairPendingBanner = nullptr;
std::uint32_t pairingShownId = 0;
bool pairingAnswered = false;

SettingsDraft settingsEditor;
profiles::Snapshot profileView;
radio::Snapshot radioView;
bool settingsLoaded = false, applyPending = false, importPending = false;
UiSettings submittedSettings;
std::array<char, 96> localNotice{};
std::uint32_t localNoticeAt = 0;
bool localNoticeActive = false, localNoticeFailure = false;
bool demoEnabled = false, keyboardForSsid = false, credentialActive = false;
bool usbWasEnabled = false;
RpmWarning usbRpmWarning = RpmWarning::Normal;
std::uint32_t usbRpmSession = 0;
std::uint32_t demoStartedAt = 0, demoUpdatedAt = 0, lastServicesAt = 0;
unsigned settingsGroup = 0, networkPage = 0, keyboardBank = 0;
LocalTextEntry textEntry;
std::array<char, 33> selectedNetwork{};
unsigned queuedAction = 0;
enum Action : unsigned {
    PreviousSettings = 1, NextSettings, ApplySettings, Calibrate, ToggleDemo,
    ExportProfile, ImportProfile, RefreshCard, ToggleWifi, ScanWifi, ShowNetworks,
    ManualSsid, UseSavedWifi, ToggleBle, DiscoverBle, EraseBonds,
    PreviousNetworks, NextNetworks, BackWifi, CancelKeyboard, Backspace,
    PreviousKeys, NextKeys, SubmitKeyboard, ConfirmYes, ConfirmNo,
    AcceptPairing, RejectPairing, OpenSettingsHome, UnitsPage, RpmPage, StopUsbSimulation, EditSetting = 100, KeyInput = 200, NetworkInput = 300, CategoryInput = 400
};
enum class Confirmation : std::uint8_t { None, Import, Bonds };
Confirmation confirmation = Confirmation::None;
Page page = Page::Menu, requestedPage = Page::Menu;
bool pageChangeRequested = false, calibrationRequested = false;
bool ready = false;
bool calibrationStored = false;
bool storageMounted = false;
std::uint32_t storageCapacity = 0;

lv_obj_t* calibrationOverlay = nullptr;
lv_obj_t* calibrationTarget = nullptr;
lv_obj_t* calibrationInstruction = nullptr;
std::array<Point, 5> calibrationRaw{};
std::size_t calibrationStep = 0;
bool calibrating = false, calibrationFinished = false;
bool calibrationContact = false, calibrationReleasing = false;
std::uint32_t calibrationPressAt = 0, calibrationReleaseAt = 0;
Point calibrationSum{}, calibrationAnchor{};
unsigned calibrationSamples = 0;

enum class BenchmarkPhase : std::uint8_t { Idle, Preparing, Full, Partial, Restoring };
BenchmarkPhase benchmarkPhase = BenchmarkPhase::Idle;
PerfWindow<256> partialTimes;
PerfWindow<20> fullTimes;
BenchmarkReport benchmarkReport;
bool benchmarkReportReady = false, benchmarkAwaiting = false, benchmarkFrameDone = false;
bool benchmarkTouchSeen = false;
std::uint32_t benchmarkRequestedAt = 0, benchmarkPartialAt = 0, benchmarkUpdatedAt = 0;
std::uint32_t benchmarkTouchAt = 0, benchmarkTouchMaxGap = 0, benchmarkDurationMs = 0;

std::uint32_t clockUs() { return static_cast<std::uint32_t>(esp_timer_get_time()); }

void presentationCompleted() {
    if (!benchmarkAwaiting) return;
    const auto duration = elapsedUs(clockUs(), benchmarkRequestedAt);
    if (benchmarkPhase == BenchmarkPhase::Full) fullTimes.add(duration);
    else if (benchmarkPhase == BenchmarkPhase::Partial) partialTimes.add(duration);
    benchmarkAwaiting = false;
    benchmarkFrameDone = true;
}

void completeFlush() {
    if (pendingFlush && !display.dmaBusy()) {
        display.endWrite();
        auto* completed = pendingFlush;
        const bool last = pendingFlushLast;
        pendingFlush = nullptr;
        pendingFlushLast = false;
        // LVGL may reuse a tile only after DMA no longer owns its memory.
        lv_disp_flush_ready(completed);
        if (last) presentationCompleted();
    }
}

void flushDisplay(lv_disp_drv_t* driver, const lv_area_t* area, lv_color_t* pixels) {
    display.waitDMA();
    completeFlush();
    // lv_disp_flush_ready() clears this flag: capture it while LVGL owns it.
    pendingFlushLast = lv_disp_flush_is_last(driver);
    display.startWrite();
    display.pushImageDMA(area->x1, area->y1, area->x2 - area->x1 + 1,
                         area->y2 - area->y1 + 1,
                         reinterpret_cast<lgfx::swap565_t*>(pixels));
    pendingFlush = driver;
}

void waitFlush(lv_disp_drv_t*) {
    completeFlush();
    taskYIELD();
}

void setText(lv_obj_t* object, const char* value) {
    if (object && std::strcmp(lv_label_get_text(object), value) != 0) {
        lv_label_set_text(object, value);
    }
}

void setHidden(lv_obj_t* object, bool hidden) {
    if (!object || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN) == hidden) return;
    if (hidden) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
}

void setTextColor(lv_obj_t* object, std::uint32_t color) {
    if (!object) return;
    const auto wanted = lv_color_hex(color);
    if (lv_obj_get_style_text_color(object, LV_PART_MAIN).full != wanted.full)
        lv_obj_set_style_text_color(object, wanted, 0);
}

void setNumber(lv_obj_t* object, bool valid, std::int32_t number) {
    if (!valid) { setText(object, unknown); return; }
    char value[16];
    std::snprintf(value, sizeof(value), "%ld", static_cast<long>(number));
    setText(object, value);
}

void noScroll(lv_obj_t* object) {
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(object, LV_SCROLLBAR_MODE_OFF);
}

lv_obj_t* label(lv_obj_t* parent, const char* value, int x, int y, int width,
                const lv_font_t* font = &lv_font_montserrat_14,
                std::uint32_t color = text) {
    auto* object = lv_label_create(parent);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_width(object, width);
    lv_label_set_long_mode(object, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    lv_label_set_text(object, value);
    return object;
}

lv_obj_t* surface(lv_obj_t* parent, Rect rect, std::uint32_t color = card, int radius = 4) {
    auto* object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, rect.x, rect.y);
    lv_obj_set_size(object, rect.w, rect.h);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(border), 0);
    noScroll(object);
    return object;
}

lv_obj_t* button(lv_obj_t* parent, const char* value, Rect rect,
                 lv_event_cb_t callback, void* userData = nullptr,
                 const lv_font_t* font = &lv_font_montserrat_16) {
    auto* object = lv_btn_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, rect.x, rect.y);
    lv_obj_set_size(object, rect.w, rect.h);
    lv_obj_set_style_bg_color(object, lv_color_hex(buttonColor), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(object, 6, 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(border), 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(0x123C42), LV_STATE_PRESSED);
    lv_obj_set_style_anim_time(object, 0, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(accent), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(object, lv_color_hex(accent), LV_STATE_CHECKED);
    noScroll(object);
    auto* caption = label(object, value, 0, 0, rect.w - 4, font);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(caption);
    lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, userData);
    return object;
}

void updateMenuStatus() {
    if (!menuStatusLabel) return;
    const auto* connection = settingsEditor.dirty() ? "Ajustes nao salvos" : radioView.wifiEnabled ? (radioView.wifiConnected ? "Wi-Fi conectado" : "Wi-Fi ligado") :
        (radioView.bleEnabled ? "Bluetooth ligado" : "Conexoes desligadas");
    char message[80];
    std::snprintf(message, sizeof(message), "%s   /   %s", storageMounted ? "SD pronto" : "SD ausente", connection);
    setText(menuStatusLabel, message);
}

lv_obj_t* region(lv_obj_t* parent, Rect rect) {
    auto* object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, rect.x, rect.y);
    lv_obj_set_size(object, rect.w, rect.h);
    noScroll(object);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
    return object;
}

void updateStorage() {
    if (!storageLabel) return;
    char value[96];
    formatStorageStatus(value, sizeof(value), storageMounted, storageCapacity);
    setText(storageLabel, value);
}

void renderPage(Page next);
void startCalibration();

void selectPage(lv_event_t* event) {
    const auto index = reinterpret_cast<std::uintptr_t>(lv_event_get_user_data(event));
    if (index < navigation.size()) {
        requestedPage = index == 2 ? Page::SettingsHome : static_cast<Page>(index + 1);
        pageChangeRequested = true;
    }
}

void returnMenu(lv_event_t*) {
    requestedPage = Page::Menu;
    pageChangeRequested = true;
}

lv_obj_t* centeredLabel(lv_obj_t* parent, const char* value, int y, int width,
                   const lv_font_t* font, std::uint32_t color = text) {
    auto* object = label(parent, value, 4, y, width - 8, font, color);
    lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, 0);
    return object;
}

void queueAction(lv_event_t* event) {
    queuedAction = static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(lv_event_get_user_data(event)));
}

lv_obj_t* actionButton(lv_obj_t* parent, const char* caption, Rect rect, unsigned action) {
    return button(parent, caption, rect, queueAction,
                  reinterpret_cast<void*>(static_cast<std::uintptr_t>(action)));
}

void requestPage(Page next) { requestedPage = next; pageChangeRequested = true; }

void applyPreview() {
    display.setBrightness(static_cast<std::uint8_t>(settingsEditor.values.brightness * 255U / 100U));
    if (registeredDisplay && benchmarkPhase == BenchmarkPhase::Idle) {
        // Poll dirty regions more often than values change: avoid an extra
        // producer-period wait without increasing the instrument update rate.
        lv_timer_set_period(registeredDisplay->refr_timer,
                            std::min(16U, framePeriodMs(settingsEditor.values.refreshMode) / 3));
    }
}

std::uint32_t rpmWarningColor(RpmWarning state, std::uint32_t normal = text) {
    if (state == RpmWarning::Critical) return danger;
    if (state == RpmWarning::High) return warning;
    return normal;
}

void updateRpmBar(unsigned rpm, bool valid, bool simulationMode = false) {
    const auto& settings = settingsEditor.values;
    const auto filled = std::min(20U, rpm * 20U / settings.rpmScale);
    for (unsigned index = 0; index < rpmSegments.size(); ++index) {
        if (!rpmSegments[index]) continue;
        std::uint32_t color = 0x202020;
        if (valid && index < filled) {
            if (simulationMode) {
                const auto segmentRpm = static_cast<std::int32_t>((index + 1) * settings.rpmScale / 20U);
                color = rpmWarningColor(ecu::rpmWarning(segmentRpm, true), accent);
            } else {
                const auto percent = (index + 1) * 5;
                color = percent >= settings.rpmRedPercent ? danger :
                    (percent >= settings.rpmYellowPercent ? warning : accent);
            }
        }
        if (segmentColors[index] != color) {
            segmentColors[index] = color;
            lv_obj_set_style_bg_color(rpmSegments[index], lv_color_hex(color), 0);
        }
    }
}

void showNotice(const char* message, bool failure = true) {
    std::snprintf(localNotice.data(), localNotice.size(), "%s", message);
    localNoticeAt = millis();
    localNoticeActive = true;
    localNoticeFailure = failure;
    setText(detailLabel, localNotice.data());
}

void refreshNotice() {
    if (!localNoticeActive) return;
    if (std::uint32_t(millis() - localNoticeAt) >= 2500) localNoticeActive = false;
    else setText(detailLabel, localNotice.data());
}

void refreshSettingsView() {
    if (page != Page::Settings) return;
    const auto& settings = settingsEditor.values;
    if (settingsGroup <= 2) {
        setText(settingsStateLabel, applyPending ? "SALVANDO" :
                (settingsEditor.dirty() || !profileView.persisted ? "NAO SALVO" : "SALVO"));
    } else if (settingsGroup == 4) setText(modeLabel, storageMounted ? "SD PRONTO" : "SD AUSENTE");
    else if (settingsGroup == 5) setText(modeLabel, radioView.wifiEnabled ? "WI-FI LIGADO" : "WI-FI OFF");
    else if (settingsGroup == 6) setText(modeLabel, radioView.bleEnabled ? "BLE LIGADO" : "BLE OFF");
    else setText(modeLabel, simulation::snapshot().enabled ? "SIM USB" : (demoEnabled ? "DEMO ATIVO" : "LOCAL"));
    char value[64];
    if (settingsGroup == 0) {
        std::snprintf(value, sizeof(value), "%u %%", unsigned(settings.brightness));
        setText(settingValues[0], value);
        std::snprintf(value, sizeof(value), "%lu ms", static_cast<unsigned long>(framePeriodMs(settings.refreshMode)));
        setText(settingValues[1], value);
        setText(settingValues[2], settings.reducedMotion ? "SIM" : "NAO");
        setText(detailLabel, "Previa imediata. Salvar para manter.");
    } else if (settingsGroup == 1) {
        setText(settingValues[0], settings.speedUnit == SpeedUnit::Kmh ? "km/h" : "mph");
        setText(settingValues[1], settings.tempUnit == TempUnit::Celsius ? "Celsius" : "Fahrenheit");
        setText(detailLabel, "Sem sensores: leituras continuam --.");
    } else if (settingsGroup == 2) {
        std::snprintf(value, sizeof(value), "%u RPM", unsigned(settings.rpmScale));
        setText(settingValues[0], value);
        std::snprintf(value, sizeof(value), "%u %%", unsigned(settings.rpmYellowPercent));
        setText(settingValues[1], value);
        std::snprintf(value, sizeof(value), "%u %%", unsigned(settings.rpmRedPercent));
        setText(settingValues[2], value);
        setText(detailLabel, "Faixas visuais. Sem corte de giro.");
    } else if (settingsGroup == 3) {
        setText(settingValues[0], calibrationStored ? "Calibrar toque (salvo)" : "Calibrar toque");
        setText(settingValues[1], demoEnabled ? "Desligar DEMO" : "Ativar DEMO sintetico");
        setText(detailLabel, simulation::snapshot().enabled ? "USB sintetico; encerrar libera DEMO." : "DEMO nao ativa entradas do motor.");
    } else if (settingsGroup == 4) {
        setText(detailLabel, profiles::statusText(profileView.status));
    } else if (settingsGroup == 5) {
        setText(settingValues[0], radioView.wifiEnabled ? "Desligar Wi-Fi" : "Ativar Wi-Fi");
        if (radioView.wifiEnabled) lv_obj_add_state(lv_obj_get_parent(settingValues[0]), LV_STATE_CHECKED);
        else lv_obj_clear_state(lv_obj_get_parent(settingValues[0]), LV_STATE_CHECKED);
        setText(detailLabel, radio::statusText(radioView.status));
    } else if (settingsGroup == 6) {
        setText(settingValues[0], radioView.bleEnabled ? "Desligar BLE" : "Ativar BLE");
        if (radioView.bleEnabled) lv_obj_add_state(lv_obj_get_parent(settingValues[0]), LV_STATE_CHECKED);
        else lv_obj_clear_state(lv_obj_get_parent(settingValues[0]), LV_STATE_CHECKED);
        setText(detailLabel, radio::statusText(radioView.status));
    }
    refreshNotice();
}

void settingRow(unsigned row, const char* title, SettingField field) {
    const auto bounds = settingsRows[row];
    surface(pageContent, bounds);
    label(pageContent, title, 16, bounds.y + 3, 192, &lv_font_montserrat_12, secondary);
    settingValues[row] = label(pageContent, "", 16, bounds.y + 20, 192, &lv_font_montserrat_16);
    const unsigned action = EditSetting + static_cast<unsigned>(field) * 2;
    actionButton(pageContent, "-", {216, bounds.y, 44, 44}, action);
    actionButton(pageContent, "+", {268, bounds.y, 44, 44}, action + 1);
}

void refreshNetworks() {
    if (page != Page::Networks) return;
    char caption[80];
    for (unsigned row = 0; row < networkLabels.size(); ++row) {
        const auto index = networkPage * 3 + row;
        if (index < radioView.networkCount && index < radio::maxNetworks) {
            const auto& network = radioView.networks[index];
            std::snprintf(caption, sizeof(caption), "%s %s", network.open ? "ABERTA" : "SENHA", network.ssid);
            setText(networkLabels[row], caption);
            lv_obj_clear_state(networkButtons[row], LV_STATE_DISABLED);
        } else {
            setText(networkLabels[row], row == 0 ? "Nenhuma rede nesta pagina" : "--");
            lv_obj_add_state(networkButtons[row], LV_STATE_DISABLED);
        }
    }
    std::snprintf(caption, sizeof(caption), "%s | %u/2", radio::statusText(radioView.status), networkPage + 1);
    setText(detailLabel, caption);
    refreshNotice();
}

void refreshKeyboard() {
    if (page != Page::Keyboard) return;
    char visible[40];
    textEntry.preview(visible, sizeof(visible));
    if (keyboardForSsid) setText(keyboardValue, visible);
    else {
        char masked[64];
        std::snprintf(masked, sizeof(masked), "[%u/64] %s", unsigned(textEntry.size()), visible);
        setText(keyboardValue, masked);
    }
    for (unsigned index = 0; index < keyLabels.size(); ++index) {
        const auto key = LocalTextEntry::key(keyboardBank, index);
        char caption[2]{key, 0};
        setText(keyLabels[index], key == ' ' ? "SP" : (key ? caption : ""));
        if (key) lv_obj_clear_state(keyButtons[index], LV_STATE_DISABLED);
        else lv_obj_add_state(keyButtons[index], LV_STATE_DISABLED);
    }
}

void renderPage(Page next) {
    if (page == Page::Keyboard && next != Page::Keyboard) {
        textEntry.clear();
        credentialActive = false;
        selectedNetwork.fill(0);
    }
    page = next;
    storageLabel = menuStatusLabel = speedValue = rpmValue = speedSignal = rpmSignal = modeLabel = nullptr;
    unitLabel = settingsStateLabel = detailLabel = keyboardValue = nullptr;
    temperatureValue = oilValue = rpmScaleLabel = gearLabel = gearValue = nullptr;
    dashboardHintLabel = faultFooterLabel = sensorSourceLabel = nullptr;
    sensorValues.fill(nullptr);
    settingValues.fill(nullptr); networkLabels.fill(nullptr); networkButtons.fill(nullptr);
    keyLabels.fill(nullptr); keyButtons.fill(nullptr); rpmSegments.fill(nullptr);
    segmentColors.fill(0x202020);
    dashboardGesture.reset(); debounce.reset(); gesturePosition = {};
    lv_obj_clean(pageContent);
    if (page != Page::Keyboard) {
        const char* title = page == Page::Menu || page == Page::Panel ? "NGK // ECU" :
            (page == Page::Sensors ? "SENSORES" : (page == Page::System ? "SISTEMA" :
             (page == Page::Networks ? "REDES WI-FI" : (page == Page::Confirm ? "CONFIRMAR" : "AJUSTES"))));
        label(pageContent, title, 8, 7, 184, &lv_font_montserrat_16);
        modeLabel = label(pageContent, demoEnabled ? "DEMO ATIVO" : "BANCADA", 184, 9, 128,
                          &lv_font_montserrat_12, accent);
        lv_obj_set_style_text_align(modeLabel, LV_TEXT_ALIGN_RIGHT, 0);
    }
    if (page == Page::Menu) {
        auto* title = lv_obj_get_child(pageContent, 0);
        setText(title, "NGK ECU");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
        lv_obj_set_y(title, 0);
        auto* primary = button(pageContent, "", navigation[0], selectPage, nullptr);
        label(primary, "PAINEL", 16, 8, 220, &lv_font_montserrat_28);
        label(primary, "RPM / velocidade / motor", 17, 46, 248, &lv_font_montserrat_12, secondary);
        label(primary, LV_SYMBOL_RIGHT, 264, 21, 28, &lv_font_montserrat_28, accent);
        auto* edge = surface(primary, {0, 0, 3, 72}, accent, 0);
        lv_obj_set_style_border_width(edge, 0, 0);
        lv_obj_clear_flag(edge, LV_OBJ_FLAG_CLICKABLE);
        constexpr std::array<const char*, 3> names{{"SENSORES", "AJUSTES", "SISTEMA"}};
        constexpr std::array<const char*, 3> icons{{LV_SYMBOL_LIST, LV_SYMBOL_SETTINGS, LV_SYMBOL_SD_CARD}};
        for (std::size_t i = 0; i < names.size(); ++i) {
            auto* tile = button(pageContent, "", navigation[i + 1], selectPage,
                                reinterpret_cast<void*>(std::uintptr_t(i + 1)));
            auto* icon = label(tile, icons[i], 0, 12, 94, &lv_font_montserrat_28, accent);
            lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
            auto* caption = label(tile, names[i], 0, 56, 94, &lv_font_montserrat_12);
            lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
        }
        menuStatusLabel = label(pageContent, "", 8, 224, 304, &lv_font_montserrat_12, secondary);
        lv_obj_set_style_text_align(menuStatusLabel, LV_TEXT_ALIGN_CENTER, 0);
        updateMenuStatus();
    } else if (page == Page::Panel) {
        setText(lv_obj_get_child(pageContent, 0), "NGK ECU");
        auto* rpm = region(pageContent, rpmCard);
        rpmValue = label(rpm, unknown, 0, 0, 200, rpmDigits);
        lv_obj_set_width(rpmValue, LV_SIZE_CONTENT);
        label(pageContent, "RPM", 174, 58, 42, &lv_font_montserrat_14, accent);
        rpmSignal = label(pageContent, noSignal, 228, 36, 84, &lv_font_montserrat_12, secondary);
        char scale[32];
        std::snprintf(scale, sizeof(scale), "MAX %u", unsigned(settingsEditor.values.rpmScale));
        rpmScaleLabel = label(pageContent, scale, 228, 57, 84, &lv_font_montserrat_12, secondary);
        gearLabel = label(pageContent, "MARCHA", 228, 36, 84, &lv_font_montserrat_12, warning);
        gearValue = label(pageContent, unknown, 228, 45, 84, &lv_font_montserrat_28);
        setHidden(gearLabel, true); setHidden(gearValue, true);
        for (unsigned index = 0; index < rpmSegments.size(); ++index) {
            const auto height = 12 + int(index) / 2;
            rpmSegments[index] = surface(pageContent, {8 + int(index) * 15, rpmBar.y + rpmBar.h - height, 12, height}, 0x202020, 0);
            lv_obj_set_style_border_width(rpmSegments[index], 0, 0);
        }
        for (unsigned index = 0; index < 5; ++index) {
            char tick[12];
            std::snprintf(tick, sizeof(tick), "%u", unsigned(settingsEditor.values.rpmScale) * index / 4);
            const int x = std::clamp(8 + int(index) * 76 - 22, 8, 268);
            auto* caption = label(pageContent, tick, x, 112, 44, &lv_font_montserrat_12, secondary);
            lv_obj_set_style_text_align(caption, index == 0 ? LV_TEXT_ALIGN_LEFT :
                                       (index == 4 ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_CENTER), 0);
        }
        auto* divider = surface(pageContent, {160, 133, 1, 90}, border, 0);
        lv_obj_set_style_border_width(divider, 0, 0);
        auto* speed = region(pageContent, speedCard);
        centeredLabel(speed, "VELOCIDADE", 0, speedCard.w, &lv_font_montserrat_12, secondary);
        speedValue = centeredLabel(speed, unknown, 23, speedCard.w, speedDigits);
        lv_obj_set_width(speedValue, LV_SIZE_CONTENT);
        lv_obj_align(speedValue, LV_ALIGN_TOP_MID, 0, 23);
        unitLabel = centeredLabel(speed, settingsEditor.values.speedUnit == SpeedUnit::Kmh ? "km/h" : "mph",
                                  76, speedCard.w, &lv_font_montserrat_12, secondary);
        label(pageContent, "TEMP. BLOCO", 8, 136, 116, &lv_font_montserrat_12, secondary);
        label(pageContent, settingsEditor.values.tempUnit == TempUnit::Celsius ? "C" : "F",
              136, 136, 20, &lv_font_montserrat_12, accent);
        temperatureValue = label(pageContent, unknown, 8, 151, 148, &lv_font_montserrat_28);
        divider = surface(pageContent, {8, 189, 148, 1}, border, 0);
        lv_obj_set_style_border_width(divider, 0, 0);
        label(pageContent, "OLEO", 8, 201, 52, &lv_font_montserrat_12, secondary);
        oilValue = label(pageContent, noSignal, 72, 201, 84, &lv_font_montserrat_12);
        speedSignal = label(pageContent, noSignal, 8, dashboardHint.y, 96, &lv_font_montserrat_12, secondary);
        dashboardHintLabel = label(pageContent, "2 toques: menu", 112, dashboardHint.y, 200,
                           &lv_font_montserrat_12, secondary);
        lv_obj_set_style_text_align(dashboardHintLabel, LV_TEXT_ALIGN_RIGHT, 0);
        faultFooterLabel = label(pageContent, "", 8, dashboardHint.y, 304, &lv_font_montserrat_12, danger);
        setHidden(faultFooterLabel, true);
    } else if (page == Page::SettingsHome) {
        constexpr std::array<const char*, 6> names{{"Tela", "Instrumentos", "Toque / DEMO", "Perfis SD", "Wi-Fi", "Bluetooth"}};
        for (unsigned index = 0; index < names.size(); ++index) {
            actionButton(pageContent, names[index], settingsCategories[index], CategoryInput + index);
        }
        button(pageContent, "Menu", menuReturn, returnMenu);
        setText(modeLabel, settingsEditor.dirty() || !profileView.persisted ? "NAO SALVO" : "SALVO");
    } else if (page == Page::Sensors) {
        auto* panel = surface(pageContent, content);
        constexpr std::array<const char*, 4> names{{"RPM", "Bloco", "Velocidade", "Oleo (chave)"}};
        for (std::size_t i = 0; i < names.size(); ++i) {
            label(panel, names[i], 10, 10 + int(i) * 26, 118, &lv_font_montserrat_14);
            sensorValues[i] = label(panel, "-- / SEM SINAL", 132, 12 + int(i) * 26, 162, &lv_font_montserrat_12, secondary);
        }
        sensorSourceLabel = label(panel, "Entradas fisicas desabilitadas", 10, 122, 284, &lv_font_montserrat_12, accent);
        button(pageContent, "Menu", menuReturn, returnMenu);
    } else if (page == Page::Settings) {
        constexpr std::array<const char*, 7> names{{"TELA", "UNIDADES", "ESCALA RPM", "TOQUE / DEMO", "PERFIS SD", "WI-FI", "BLUETOOTH"}};
        char heading[48];
        std::snprintf(heading, sizeof(heading), "%s", names[settingsGroup]);
        setText(lv_obj_get_child(pageContent, 0), heading);
        settingsStateLabel = modeLabel;
        detailLabel = label(pageContent, "", 8, 25, 304, &lv_font_montserrat_12, secondary);
        if (settingsGroup == 0) {
            settingRow(0, "Brilho", SettingField::Brightness);
            settingRow(1, "Atualizacao", SettingField::Refresh);
            settingRow(2, "Reduzir movimento", SettingField::Motion);
        } else if (settingsGroup == 1) {
            settingRow(0, "Velocidade", SettingField::Speed);
            settingRow(1, "Temperatura", SettingField::Temperature);
            label(pageContent, "Unidades nao criam leituras reais.", 12, 146, 296, &lv_font_montserrat_12, secondary);
        } else if (settingsGroup == 2) {
            settingRow(0, "Escala maxima", SettingField::RpmScale);
            settingRow(1, "Inicio amarelo", SettingField::Yellow);
            settingRow(2, "Inicio vermelho", SettingField::Red);
        } else if (settingsGroup == 3) {
            auto* control = actionButton(pageContent, "Calibrar toque", settingsRows[0], Calibrate);
            settingValues[0] = lv_obj_get_child(control, 0);
            control = actionButton(pageContent, "", settingsRows[1], ToggleDemo);
            settingValues[1] = lv_obj_get_child(control, 0);
            actionButton(pageContent, "Encerrar SIM USB", settingsRows[2], StopUsbSimulation);
        } else if (settingsGroup == 4) {
            actionButton(pageContent, "Exportar ajustes salvos", settingsRows[0], ExportProfile);
            actionButton(pageContent, "Importar perfil do SD", settingsRows[1], ImportProfile);
            actionButton(pageContent, "Tentar montar SD", settingsRows[2], RefreshCard);
        } else if (settingsGroup == 5) {
            auto* control = actionButton(pageContent, "", settingsRows[0], ToggleWifi);
            settingValues[0] = lv_obj_get_child(control, 0);
            actionButton(pageContent, "Buscar", {8, 88, 148, 44}, ScanWifi);
            actionButton(pageContent, "Redes", {164, 88, 148, 44}, ShowNetworks);
            actionButton(pageContent, "SSID manual", {8, 136, 148, 44}, ManualSsid);
            actionButton(pageContent, "Usar salva", {164, 136, 148, 44}, UseSavedWifi);
        } else {
            auto* control = actionButton(pageContent, "", settingsRows[0], ToggleBle);
            settingValues[0] = lv_obj_get_child(control, 0);
            actionButton(pageContent, "Descobrir por 120 s", settingsRows[1], DiscoverBle);
            actionButton(pageContent, "Apagar vinculos", settingsRows[2], EraseBonds);
        }
        if (settingsGroup == 1 || settingsGroup == 2) {
            actionButton(pageContent, LV_SYMBOL_LEFT, {8, 188, 44, 44}, OpenSettingsHome);
            auto* unitsTab = actionButton(pageContent, "Unidades", {60, 188, 96, 44}, UnitsPage);
            auto* rpmTab = actionButton(pageContent, "RPM", {164, 188, 60, 44}, RpmPage);
            lv_obj_add_state(settingsGroup == 1 ? unitsTab : rpmTab, LV_STATE_CHECKED);
            actionButton(pageContent, "Salvar", {232, 188, 80, 44}, ApplySettings);
        } else if (settingsGroup == 0) {
            actionButton(pageContent, "Voltar", {8, 188, 148, 44}, OpenSettingsHome);
            actionButton(pageContent, "Salvar ajustes", {164, 188, 148, 44}, ApplySettings);
        } else {
            actionButton(pageContent, "Voltar", {8, 188, 148, 44}, OpenSettingsHome);
            button(pageContent, "Menu", {164, 188, 148, 44}, returnMenu);
        }
        refreshSettingsView();
    } else if (page == Page::Networks) {
        detailLabel = label(pageContent, "", 8, 25, 304, &lv_font_montserrat_12, secondary);
        for (unsigned row = 0; row < networkButtons.size(); ++row) {
            networkButtons[row] = actionButton(pageContent, "", settingsRows[row], NetworkInput + row);
            networkLabels[row] = lv_obj_get_child(networkButtons[row], 0);
        }
        actionButton(pageContent, LV_SYMBOL_LEFT, {8, 188, 60, 44}, PreviousNetworks);
        actionButton(pageContent, "Wi-Fi", {76, 188, 168, 44}, BackWifi);
        actionButton(pageContent, LV_SYMBOL_RIGHT, {252, 188, 60, 44}, NextNetworks);
        refreshNetworks();
    } else if (page == Page::Keyboard) {
        char heading[64];
        if (keyboardForSsid) std::snprintf(heading, sizeof(heading), "SSID MANUAL");
        else std::snprintf(heading, sizeof(heading), "SENHA: %.32s", selectedNetwork.data());
        detailLabel = label(pageContent, heading, 8, 3, 304, &lv_font_montserrat_12, secondary);
        keyboardValue = label(pageContent, "", 8, 20, 304, &lv_font_montserrat_14);
        for (unsigned index = 0; index < keyButtons.size(); ++index) {
            keyButtons[index] = actionButton(pageContent, "", keyboardKey(index), KeyInput + index);
            keyLabels[index] = lv_obj_get_child(keyButtons[index], 0);
        }
        actionButton(pageContent, "Sair", {8, 190, 60, 44}, CancelKeyboard);
        actionButton(pageContent, LV_SYMBOL_BACKSPACE, {74, 190, 44, 44}, Backspace);
        actionButton(pageContent, LV_SYMBOL_LEFT, {124, 190, 44, 44}, PreviousKeys);
        actionButton(pageContent, LV_SYMBOL_RIGHT, {174, 190, 44, 44}, NextKeys);
        actionButton(pageContent, "OK", {224, 190, 88, 44}, SubmitKeyboard);
        refreshKeyboard();
    } else if (page == Page::Confirm) {
        label(pageContent, confirmation == Confirmation::Import ?
              "Importar substitui os ajustes\natuais e o rascunho local.\nContinuar?" :
              "Apagar todos os vinculos BLE?\nSera preciso parear novamente.",
              12, 50, 296, &lv_font_montserrat_16);
        actionButton(pageContent, "Confirmar", {8, 172, 148, 44}, ConfirmYes);
        actionButton(pageContent, "Cancelar", {164, 172, 148, 44}, ConfirmNo);
    } else {
        auto* panel = surface(pageContent, content);
        storageLabel = label(panel, "", 10, 8, 282, &lv_font_montserrat_14);
        updateStorage();
        detailLabel = label(panel, "", 10, 62, 282, &lv_font_montserrat_12, secondary);
        label(panel, "Sem sensores / sem atuacao\nESP32 | flash interna 4 MiB",
              10, 100, 282, &lv_font_montserrat_12, accent);
        button(pageContent, "Menu", menuReturn, returnMenu);
    }
}

void localResult(bool accepted) {
    showNotice(accepted ? "Solicitacao enviada" : "Nao enviado. Verifique estado / tente de novo.", !accepted);
}

void openKeyboard(bool ssid) {
    keyboardForSsid = ssid;
    credentialActive = true;
    keyboardBank = 0;
    textEntry.begin(!ssid, ssid ? 32 : 64);
    requestPage(Page::Keyboard);
}

void handleAction() {
    const auto action = queuedAction;
    queuedAction = 0;
    if (!action) return;
    if (page == Page::Keyboard && radioView.blePairingPending && pageChangeRequested) return;
    if (action >= CategoryInput && action < CategoryInput + 6) {
        constexpr unsigned groups[]{0, 1, 3, 4, 5, 6};
        settingsGroup = groups[action - CategoryInput];
        requestPage(Page::Settings);
        return;
    }
    if (action >= EditSetting && action < EditSetting + 16) {
        settingsEditor.adjust(SettingField((action - EditSetting) / 2), (action - EditSetting) % 2 ? 1 : -1);
        applyPreview(); refreshSettingsView();
        return;
    }
    if (action >= KeyInput && action < KeyInput + 18) {
        textEntry.append(LocalTextEntry::key(keyboardBank, action - KeyInput));
        refreshKeyboard(); return;
    }
    if (action >= NetworkInput && action < NetworkInput + 3) {
        const auto index = networkPage * 3 + action - NetworkInput;
        if (index >= radioView.networkCount || index >= radio::maxNetworks) return;
        const auto& network = radioView.networks[index];
        std::memcpy(selectedNetwork.data(), network.ssid, selectedNetwork.size());
        selectedNetwork.back() = 0;
        if (network.open) localResult(radio::connectWifi(selectedNetwork.data(), ""));
        else openKeyboard(false);
        return;
    }
    switch (action) {
        case OpenSettingsHome: requestPage(Page::SettingsHome); break;
        case UnitsPage: settingsGroup = 1; requestPage(Page::Settings); break;
        case RpmPage: settingsGroup = 2; requestPage(Page::Settings); break;
        case PreviousSettings: settingsGroup = (settingsGroup + 6) % 7; requestPage(Page::Settings); break;
        case NextSettings: settingsGroup = (settingsGroup + 1) % 7; requestPage(Page::Settings); break;
        case ApplySettings:
            if (applyPending || importPending) { showNotice("Aguarde a operacao anterior"); break; }
            submittedSettings = settingsEditor.values;
            applyPending = profiles::apply(settingsEditor.values);
            refreshSettingsView(); localResult(applyPending); break;
        case Calibrate: calibrationRequested = true; break;
        case ToggleDemo:
            if (simulation::snapshot().enabled) { showNotice("Encerre SIM USB antes de ativar DEMO"); break; }
            demoEnabled = !demoEnabled;
            demoStartedAt = millis(); demoUpdatedAt = demoStartedAt - 250;
            refreshSettingsView();
            if (demoEnabled) requestPage(Page::Panel);
            break;
        case StopUsbSimulation:
            simulation::endAnySession();
            demoEnabled = false;
            showNotice("SIM USB encerrado", false);
            refreshSettingsView();
            break;
        case ExportProfile:
            if (applyPending || importPending) showNotice("Aguarde a operacao anterior");
            else localResult(profiles::exportSd());
            break;
        case RefreshCard:
            if (applyPending || importPending) showNotice("Aguarde a operacao anterior");
            else localResult(profiles::refreshSd());
            break;
        case ImportProfile:
            if (applyPending || importPending) showNotice("Aguarde a operacao anterior");
            else { confirmation = Confirmation::Import; requestPage(Page::Confirm); }
            break;
        case ToggleWifi: localResult(radio::enableWifi(!radioView.wifiEnabled)); break;
        case ScanWifi: localResult(radio::scanWifi()); break;
        case ShowNetworks: networkPage = 0; requestPage(Page::Networks); break;
        case ManualSsid: selectedNetwork.fill(0); openKeyboard(true); break;
        case UseSavedWifi:
            if (radioView.wifiProfileAvailable) localResult(radio::connectWifi(radioView.savedSsid, nullptr));
            else showNotice("Nenhuma rede salva nesta placa");
            break;
        case ToggleBle: localResult(radio::enableBle(!radioView.bleEnabled)); break;
        case DiscoverBle: localResult(radio::discoverBle(120)); break;
        case EraseBonds: confirmation = Confirmation::Bonds; requestPage(Page::Confirm); break;
        case PreviousNetworks: networkPage = 0; refreshNetworks(); break;
        case NextNetworks: networkPage = 1; refreshNetworks(); break;
        case BackWifi: settingsGroup = 5; requestPage(Page::Settings); break;
        case CancelKeyboard: textEntry.clear(); settingsGroup = 5; requestPage(Page::Settings); break;
        case Backspace: textEntry.backspace(); refreshKeyboard(); break;
        case PreviousKeys: keyboardBank = (keyboardBank + 5) % 6; refreshKeyboard(); break;
        case NextKeys: keyboardBank = (keyboardBank + 1) % 6; refreshKeyboard(); break;
        case SubmitKeyboard:
            if (keyboardForSsid) {
                if (!textEntry.size()) { setText(detailLabel, "SSID nao pode ficar vazio"); break; }
                std::snprintf(selectedNetwork.data(), selectedNetwork.size(), "%s", textEntry.text());
                openKeyboard(false);
            } else if (!textEntry.validPassword()) {
                setText(detailLabel, "Senha: 8-63 ASCII ou 64 hexadecimais");
            } else {
                const bool accepted = radio::connectWifi(selectedNetwork.data(), textEntry.text());
                // The radio command owns its bounded copy; never retain this UI secret.
                textEntry.clear(); refreshKeyboard();
                if (accepted) { settingsGroup = 5; requestPage(Page::Settings); }
                else setText(detailLabel, "Nao enviado. Redigite e tente de novo.");
            }
            break;
        case ConfirmYes:
            if (confirmation == Confirmation::Import) {
                if (applyPending || importPending) { setText(modeLabel, "AGUARDE"); break; }
                importPending = profiles::importSd();
                if (!importPending) { setText(modeLabel, "NAO ENVIADO"); break; }
                settingsGroup = 4;
            } else {
                if (!radio::clearBonds()) { setText(modeLabel, "NAO ENVIADO"); break; }
                settingsGroup = 6;
            }
            confirmation = Confirmation::None; requestPage(Page::Settings); break;
        case ConfirmNo: confirmation = Confirmation::None; requestPage(Page::Settings); break;
        case AcceptPairing:
        case RejectPairing: {
            radio::Snapshot latest;
            if (!radio::snapshot(latest) || !latest.blePairingPending || latest.blePairingId != pairingShownId) {
                setText(pairStatusLabel, "Pedido mudou; confira o novo codigo"); break;
            }
            const bool accepted = radio::confirmPairing(action == AcceptPairing);
            pairingAnswered = accepted;
            setText(pairStatusLabel, accepted ? "Resposta enviada" : "Nao enviado. Tente novamente.");
            if (accepted) {
                lv_obj_add_state(pairAccept, LV_STATE_DISABLED);
                lv_obj_add_state(pairReject, LV_STATE_DISABLED);
            }
            break;
        }
        default: break;
    }
}

void refreshPairing() {
    if (benchmarkPhase != BenchmarkPhase::Idle) {
        if (radioView.blePairingPending && !pairPendingBanner) {
            pairPendingBanner = label(lv_scr_act(), "BLE pendente: confirme apos o teste", 8, 222, 304,
                                      &lv_font_montserrat_12, accent);
            lv_obj_set_style_bg_color(pairPendingBanner, lv_color_hex(background), 0);
            lv_obj_set_style_bg_opa(pairPendingBanner, LV_OPA_COVER, 0);
        }
        return;
    }
    if (pairPendingBanner) { lv_obj_del(pairPendingBanner); pairPendingBanner = nullptr; }
    if (!radioView.blePairingPending) {
        if (pairOverlay) lv_obj_del(pairOverlay);
        pairOverlay = pairCodeLabel = pairStatusLabel = pairAccept = pairReject = nullptr;
        pairingShownId = 0;
        return;
    }
    if (calibrating) return;
    if (page == Page::Keyboard) {
        // Release the keyboard before allocating a modal in the fixed 32 KiB pool.
        textEntry.clear();
        selectedNetwork.fill(0);
        credentialActive = false;
        queuedAction = 0;
        settingsGroup = 6;
        showNotice("Entrada Wi-Fi cancelada para parear BLE");
        requestPage(Page::Settings);
        return;
    }
    if (pageChangeRequested) return;
    if (!pairOverlay) {
        pairOverlay = surface(lv_scr_act(), {0, 0, 320, 240}, background, 0);
        label(pairOverlay, "CONFIRMAR BLUETOOTH", 12, 12, 296, &lv_font_montserrat_16, accent);
        label(pairOverlay, "Compare o codigo no dispositivo.\nAceite apenas se os dois coincidirem.",
              12, 44, 296, &lv_font_montserrat_12);
        pairCodeLabel = label(pairOverlay, "", 12, 84, 296, &lv_font_montserrat_28);
        lv_obj_set_style_text_align(pairCodeLabel, LV_TEXT_ALIGN_CENTER, 0);
        pairStatusLabel = label(pairOverlay, "Pedido expira em ate 30 s", 12, 132, 296, &lv_font_montserrat_12, secondary);
        pairAccept = actionButton(pairOverlay, "Confere", {8, 172, 148, 44}, AcceptPairing);
        pairReject = actionButton(pairOverlay, "Rejeitar", {164, 172, 148, 44}, RejectPairing);
    }
    if (pairingShownId != radioView.blePairingId) {
        pairingShownId = radioView.blePairingId;
        pairingAnswered = false;
        char code[12];
        std::snprintf(code, sizeof(code), "%06lu", static_cast<unsigned long>(radioView.blePasskey));
        setText(pairCodeLabel, code);
        setText(pairStatusLabel, "Pedido expira em ate 30 s");
        lv_obj_clear_state(pairAccept, LV_STATE_DISABLED);
        lv_obj_clear_state(pairReject, LV_STATE_DISABLED);
    }
}

void pollServices(bool force = false) {
    const auto now = millis();
    if (!force && std::uint32_t(now - lastServicesAt) < 500) return;
    lastServicesAt = now;
    radio::Snapshot radios;
    if (radio::snapshot(radios)) radioView = radios;
    refreshPairing();
    if (benchmarkPhase != BenchmarkPhase::Idle) return;
    profiles::Snapshot profile;
    if (profiles::snapshot(profile) && profile.ready && (!settingsLoaded || profile.revision != profileView.revision)) {
        const bool applyCompleted = applyPending && profile.settings == submittedSettings &&
            (profile.status == profiles::Status::Saved || profile.status == profiles::Status::StorageError);
        const bool importCompleted = importPending &&
            (profile.status == profiles::Status::Imported || profile.status == profiles::Status::RecoveredBackup ||
             profile.status == profiles::Status::StorageError || profile.status == profiles::Status::NoCard ||
             profile.status == profiles::Status::MissingProfile || profile.status == profiles::Status::ReadError);
        const bool successfulImport = importCompleted &&
            (profile.status == profiles::Status::Imported || profile.status == profiles::Status::RecoveredBackup);
        if (successfulImport) settingsEditor.load(profile.settings);
        else if (applyCompleted && profile.persisted) settingsEditor.acknowledge(profile.settings);
        else if (!applyPending && !importPending && !settingsEditor.dirty()) settingsEditor.load(profile.settings);
        else if (!settingsLoaded && profile.persisted) settingsEditor.acknowledge(profile.settings);
        profileView = profile;
        settingsLoaded = true;
        if (applyCompleted) applyPending = false;
        if (importCompleted) importPending = false;
        if (!localNoticeFailure && (applyCompleted || importCompleted || (!applyPending && !importPending))) {
            localNoticeActive = false;
        }
        applyPreview();
    }
    refreshSettingsView(); refreshNetworks(); updateMenuStatus();
    if (page == Page::SettingsHome) setText(modeLabel, settingsEditor.dirty() || !profileView.persisted ? "NAO SALVO" : "SALVO");
    if (page == Page::System && detailLabel) {
        char value[80];
        std::snprintf(value, sizeof(value), "Heap livre: %lu KiB\nMaior bloco: %lu KiB",
                      static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024),
                      static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
        setText(detailLabel, value);
    }
}

void serviceUsbSimulation() {
    const auto source = simulation::snapshot();
    if (!source.enabled || source.frame.session != usbRpmSession) usbRpmWarning = RpmWarning::Normal;
    usbRpmSession = source.enabled ? source.frame.session : 0;
    const auto view = simulation::present(source, settingsEditor.values.speedUnit,
                                          settingsEditor.values.tempUnit, usbRpmWarning);
    usbRpmWarning = view.rpmWarning;
    if (calibrating) return;  // Observe TTL even while the calibration overlay owns the UI.
    if (!source.enabled && !usbWasEnabled) return;
    if (!source.enabled) demoEnabled = false;
    usbWasEnabled = source.enabled;
    const bool dataPage = page == Page::Panel || page == Page::Sensors || page == Page::Menu;
    if (dataPage || (page == Page::Settings && settingsGroup == 3)) {
        setText(modeLabel, source.enabled ? (view.live ? "SIM USB" : "SIM SEM LINK") :
                (page == Page::Settings ? "LOCAL" : "BANCADA"));
        setTextColor(modeLabel, source.enabled ? warning : accent);
    }
    if (page == Page::Panel) {
        setHidden(gearLabel, !source.enabled); setHidden(gearValue, !source.enabled);
        setHidden(rpmScaleLabel, source.enabled); setHidden(rpmSignal, source.enabled);
        setNumber(rpmValue, view.rpmValid, view.rpm);
        setNumber(speedValue, view.speedValid, view.speed);
        setNumber(temperatureValue, view.temperatureValid, view.temperature);
        setTextColor(rpmValue, source.enabled && !view.rpmValid ? secondary : rpmWarningColor(view.rpmWarning));
        setTextColor(speedValue, source.enabled && !view.speedValid ? secondary : text);
        const bool hot = view.live && (source.frame.faults & simulation::Overheat);
        setTextColor(temperatureValue, hot ? danger : text);
        setText(oilValue, !view.oilValid ? noSignal : (view.oil ? "BAIXO" : "OK (SIM)"));
        setTextColor(oilValue, view.oilValid && view.oil ? danger : text);
        if (!view.gearValid) setText(gearValue, unknown);
        else if (view.gear == -1) setText(gearValue, "R");
        else if (view.gear == 0) setText(gearValue, "N");
        else setNumber(gearValue, true, view.gear);
        updateRpmBar(static_cast<unsigned>(view.rpm), view.rpmValid, source.enabled);
        char value[24];
        if (source.enabled) {
            if (view.tpsValid) std::snprintf(value, sizeof(value), "TPS %ld%%", static_cast<long>(view.tps));
            else std::snprintf(value, sizeof(value), "TPS --");
            setText(speedSignal, value);
        } else {
            setText(speedSignal, noSignal);
            setText(rpmSignal, noSignal);
        }
        const bool alert = source.enabled && view.alert != simulation::Alert::None;
        setHidden(speedSignal, alert); setHidden(dashboardHintLabel, alert);
        setHidden(faultFooterLabel, !alert);
        setText(faultFooterLabel, simulation::alertText(view.alert));
        setTextColor(faultFooterLabel, !alert ? secondary :
            (view.alert == simulation::Alert::HighRpm ? warning : danger));
    } else if (page == Page::Sensors) {
        const bool valid[]{view.rpmValid, view.temperatureValid, view.speedValid, view.oilValid};
        const std::int32_t number[]{view.rpm, view.temperature, view.speed};
        const char* units[]{"rpm", settingsEditor.values.tempUnit == TempUnit::Celsius ? "C" : "F",
                            settingsEditor.values.speedUnit == SpeedUnit::Kmh ? "km/h" : "mph"};
        for (unsigned index = 0; index < sensorValues.size(); ++index) {
            char value[32];
            if (!valid[index]) std::snprintf(value, sizeof(value), "-- / SEM SINAL");
            else if (index == 3) std::snprintf(value, sizeof(value), "%s", view.oil ? "BAIXO (SIM)" : "OK (SIM)");
            else std::snprintf(value, sizeof(value), "%ld %s", static_cast<long>(number[index]), units[index]);
            setText(sensorValues[index], value);
            const auto color = index == 0 ?
                (source.enabled && !view.rpmValid ? secondary : rpmWarningColor(view.rpmWarning)) :
                (index == 3 && view.oilValid && view.oil ? danger : text);
            setTextColor(sensorValues[index], color);
        }
        setText(sensorSourceLabel, source.enabled ? (view.live ? "SIM USB / dados sinteticos" : "SIM SEM LINK / dados indisponiveis") :
                "Entradas fisicas desabilitadas");
        setTextColor(sensorSourceLabel, source.enabled ? warning : accent);
    }
}

void serviceDemo() {
    if (!demoEnabled || simulation::snapshot().enabled || page != Page::Panel || calibrating || pairOverlay || benchmarkPhase != BenchmarkPhase::Idle) return;
    const auto now = millis();
    const auto period = settingsEditor.values.reducedMotion ? 250U : framePeriodMs(settingsEditor.values.refreshMode);
    if (std::uint32_t(now - demoUpdatedAt) < period) return;
    demoUpdatedAt = now;
    const auto position = std::uint32_t(now - demoStartedAt) % 10000U;
    const auto ramp = position < 5000 ? position : 10000 - position;
    char value[16];
    const auto speed = static_cast<unsigned>(std::lround(speedDisplay(float(ramp) / 50.0F, settingsEditor.values.speedUnit)));
    std::snprintf(value, sizeof(value), "%u", speed); setText(speedValue, value);
    std::snprintf(value, sizeof(value), "%u", unsigned(800 + ramp)); setText(rpmValue, value);
    setText(modeLabel, "DEMO ATIVO"); setText(speedSignal, "SIMULADO"); setText(rpmSignal, "SIMULADO");
    updateRpmBar(800 + ramp, true);
}

void requestBenchmarkPresentation(std::uint32_t now) {
    benchmarkRequestedAt = now;
    benchmarkAwaiting = true;
    benchmarkFrameDone = false;
}

void finishBenchmarkReport() {
    lv_mem_monitor_t memory{};
    lv_mem_monitor(&memory);
    benchmarkReport.durationMs = benchmarkDurationMs;
    benchmarkReport.presentations = partialTimes.count();
    benchmarkReport.partialP95Us = partialTimes.p95();
    benchmarkReport.partialMaxUs = partialTimes.max();
    benchmarkReport.touchMaxGapUs = benchmarkTouchMaxGap;
    benchmarkReport.fullP95Us = fullTimes.p95();
    benchmarkReport.heapFree = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    benchmarkReport.heapMin = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    benchmarkReport.heapLargest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    benchmarkReport.lvglUsedPercent = memory.used_pct;
    benchmarkReport.lvglFragPercent = memory.frag_pct;
    benchmarkPhase = BenchmarkPhase::Idle;
    benchmarkReportReady = true;
    applyPreview();
}

void serviceBenchmark() {
    if (benchmarkPhase == BenchmarkPhase::Idle || pendingFlush) return;
    const auto now = clockUs();
    if (benchmarkPhase == BenchmarkPhase::Preparing) {
        benchmarkPhase = BenchmarkPhase::Full;
        requestBenchmarkPresentation(now);
        renderPage(Page::Panel);
        setText(modeLabel, "DEMO TESTE");
        setText(speedValue, "0");
        setText(rpmValue, "800");
        setText(speedSignal, "SIMULADO");
        setText(rpmSignal, "SIMULADO");
        setText(unitLabel, "km/h");
        updateRpmBar(800, true);
        lv_obj_invalidate(pageContent);
    } else if (benchmarkPhase == BenchmarkPhase::Full && benchmarkFrameDone) {
        if (fullTimes.count() < 20) {
            requestBenchmarkPresentation(now);
            lv_obj_invalidate(pageContent);
        } else {
            benchmarkPhase = BenchmarkPhase::Partial;
            benchmarkFrameDone = false;
            benchmarkPartialAt = now;
            benchmarkUpdatedAt = now - 17000;
        }
    } else if (benchmarkPhase == BenchmarkPhase::Partial && !benchmarkAwaiting) {
        const auto elapsed = elapsedUs(now, benchmarkPartialAt);
        if (elapsed >= 10000000) {
            benchmarkDurationMs = elapsed / 1000;
            benchmarkPhase = BenchmarkPhase::Restoring;
            requestBenchmarkPresentation(now);
            // All synthetic labels are destroyed; reopening Panel creates -- again.
            renderPage(Page::Menu);
        } else if (elapsedUs(now, benchmarkUpdatedAt) >= 17000) {
            benchmarkUpdatedAt = now;
            requestBenchmarkPresentation(now);
            const auto position = elapsed / 1000;
            const auto ramp = position < 5000 ? position : 10000 - position;
            char number[16];
            std::snprintf(number, sizeof(number), "%u", unsigned(ramp / 50));
            setText(speedValue, number);
            std::snprintf(number, sizeof(number), "%u", unsigned(800 + ramp));
            setText(rpmValue, number);
            updateRpmBar(800 + ramp, true);
            // The first step may still equal the full-screen scene's value.
            lv_obj_invalidate(rpmValue);
        }
    } else if (benchmarkPhase == BenchmarkPhase::Restoring && benchmarkFrameDone) {
        finishBenchmarkReport();
    }
}

bool loadCalibration() {
    Preferences preferences;
    if (!preferences.begin("ngk-touch", true)) return false;
    const auto length = preferences.getBytesLength("affine-v1");
    const auto bytes = length == sizeof(calibration.coefficients)
        ? preferences.getBytes("affine-v1", calibration.coefficients.data(), length) : 0;
    preferences.end();
    return bytes == sizeof(calibration.coefficients) && calibration.valid();
}

bool saveCalibration() {
    Preferences preferences;
    if (!preferences.begin("ngk-touch", false)) return false;
    const auto count = preferences.putBytes("affine-v1", calibration.coefficients.data(),
                                           sizeof(calibration.coefficients));
    preferences.end();
    return count == sizeof(calibration.coefficients);
}

void showCalibrationTarget() {
    const auto target = calibrationTargets[calibrationStep];
    lv_obj_set_pos(calibrationTarget, int(target.x) - 22, int(target.y) - 22);
    char message[64];
    std::snprintf(message, sizeof(message), "Toque e solte o alvo  %u/5",
                  unsigned(calibrationStep + 1));
    setText(calibrationInstruction, message);
}

void startCalibration() {
    if (calibrating) return;
    calibrating = true;
    calibrationFinished = false;
    calibrationStep = 0;
    calibrationContact = false;
    calibrationReleasing = false;
    calibrationSamples = 0;
    debounce.reset();
    dashboardGesture.reset();
    pageChangeRequested = false;
    gesturePosition = {};
    calibrationOverlay = surface(lv_scr_act(), {0, 0, 320, 240}, background, 0);
    label(calibrationOverlay, "NGK ECU  /  CALIBRAR TOQUE", 8, 8, 304,
          &lv_font_montserrat_14);
    calibrationInstruction = label(calibrationOverlay, "", 8, 208, 304,
                                    &lv_font_montserrat_12, secondary);
    calibrationTarget = surface(calibrationOverlay, {0, 0, 44, 44}, buttonColor, 22);
    lv_obj_set_style_border_color(calibrationTarget, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(calibrationTarget, 2, 0);
    auto* plus = label(calibrationTarget, "+", 0, 0, 40, &lv_font_montserrat_28, accent);
    lv_obj_set_style_text_align(plus, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(plus);
    showCalibrationTarget();
}

void sampleCalibration(bool down, Point raw, std::uint32_t now) {
    if (calibrationFinished) return;
    if (down) {
        calibrationReleasing = false;
        if (!calibrationContact || std::fabs(raw.x - calibrationAnchor.x) > 120 ||
            std::fabs(raw.y - calibrationAnchor.y) > 120) {
            calibrationContact = true;
            calibrationAnchor = raw;
            calibrationPressAt = now;
            calibrationSum = {};
            calibrationSamples = 0;
        }
        if (calibrationSamples < 32) {
            calibrationSum.x += raw.x;
            calibrationSum.y += raw.y;
            ++calibrationSamples;
        }
        return;
    }
    if (!calibrationContact) return;
    if (!calibrationReleasing) {
        calibrationReleasing = true;
        calibrationReleaseAt = now;
    }
    if (std::uint32_t(now - calibrationReleaseAt) < 50) return;
    calibrationContact = false;
    if (calibrationSamples < 5 || std::uint32_t(calibrationReleaseAt - calibrationPressAt) < 70) return;
    calibrationRaw[calibrationStep] = {
        calibrationSum.x / calibrationSamples, calibrationSum.y / calibrationSamples};
    ++calibrationStep;
    if (calibrationStep < calibrationTargets.size()) showCalibrationTarget();
    else if (calibration.fit(calibrationRaw, calibrationTargets)) calibrationFinished = true;
    else {
        calibrationStep = 0;
        showCalibrationTarget();
        setText(calibrationInstruction, "Pontos inconsistentes. Repita o alvo 1/5");
    }
}

void readTouch(lv_indev_drv_t*, lv_indev_data_t* data) {
    if (benchmarkPhase == BenchmarkPhase::Full || benchmarkPhase == BenchmarkPhase::Partial) {
        const auto now = clockUs();
        if (benchmarkTouchSeen) {
            benchmarkTouchMaxGap = std::max(benchmarkTouchMaxGap, elapsedUs(now, benchmarkTouchAt));
        }
        benchmarkTouchAt = now;
        benchmarkTouchSeen = true;
    }
    Point raw{};
    const bool down = touch.sample(raw);
    if (benchmarkPhase != BenchmarkPhase::Idle) {
        data->state = LV_INDEV_STATE_REL;
        data->point = {0, 0};
        return;
    }
    if (calibrating) {
        sampleCalibration(down, raw, millis());
        data->state = LV_INDEV_STATE_REL;
        data->point = {0, 0};
        return;
    }
    const auto point = down ? calibration.map(raw) : Point{};
    const bool inside = down && point.x >= 0 && point.x < 320 && point.y >= 0 && point.y < 240;
    const auto now = millis();
    const bool pressed = debounce.update(inside, point, now);
    if (down) gesturePosition = point;
    if (page == Page::Panel && !pairOverlay && dashboardGesture.update(pressed, gesturePosition, now)) {
        // Defer widget deletion until tick(), after LVGL finishes this handler.
        requestedPage = Page::Menu;
        pageChangeRequested = true;
    }
    data->state = pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    data->point.x = lv_coord_t(std::clamp(int(debounce.position().x), 0, 319));
    data->point.y = lv_coord_t(std::clamp(int(debounce.position().y), 0, 239));
}
}

bool begin() {
    if (ready) return true;
    constexpr auto pixels = BoardProfile::width * BoardProfile::bufferLines;
    bufferA = static_cast<lv_color_t*>(heap_caps_malloc(pixels * sizeof(lv_color_t),
                                                       MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    bufferB = static_cast<lv_color_t*>(heap_caps_malloc(pixels * sizeof(lv_color_t),
                                                       MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!bufferA || !bufferB) {
        heap_caps_free(bufferA);
        heap_caps_free(bufferB);
        bufferA = bufferB = nullptr;
        return false;
    }
    if (!display.init()) {
        heap_caps_free(bufferA);
        heap_caps_free(bufferB);
        bufferA = bufferB = nullptr;
        return false;
    }
    display.setRotation(1);
    display.setBrightness(180);
    display.initDMA();
    touch.begin();
    lv_init();
    lv_disp_draw_buf_init(&drawBuffer, bufferA, bufferB, pixels);
    lv_disp_drv_init(&displayDriver);
    displayDriver.hor_res = BoardProfile::width;
    displayDriver.ver_res = BoardProfile::height;
    displayDriver.draw_buf = &drawBuffer;
    displayDriver.flush_cb = flushDisplay;
    displayDriver.wait_cb = waitFlush;
    registeredDisplay = lv_disp_drv_register(&displayDriver);
    if (!registeredDisplay) return false;
    lv_indev_drv_init(&inputDriver);
    inputDriver.type = LV_INDEV_TYPE_POINTER;
    inputDriver.read_cb = readTouch;
    if (!lv_indev_drv_register(&inputDriver)) return false;
    auto* screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    noScroll(screen);
    pageContent = lv_obj_create(screen);
    lv_obj_remove_style_all(pageContent);
    lv_obj_set_pos(pageContent, 0, 0);
    lv_obj_set_size(pageContent, 320, 240);
    noScroll(pageContent);
    // Children handle clicks; the dashboard gesture uses the input stream.
    lv_obj_clear_flag(pageContent, LV_OBJ_FLAG_CLICKABLE);
    renderPage(Page::Menu);
    ready = true;
    pollServices(true);
    applyPreview();
    calibrationStored = loadCalibration();
    if (!calibrationStored) startCalibration();
    return true;
}

void tick() {
    if (!ready) return;
    completeFlush();
    serviceBenchmark();
    pollServices();
    serviceUsbSimulation();
    serviceDemo();
    lv_timer_handler();
    completeFlush();
    serviceBenchmark();
    if (benchmarkPhase != BenchmarkPhase::Idle) return;
    handleAction();
    if (calibrating && calibrationFinished) {
        calibrationStored = saveCalibration();
        lv_obj_del(calibrationOverlay);
        calibrationOverlay = calibrationTarget = calibrationInstruction = nullptr;
        calibrating = false;
        debounce.reset();
        renderPage(Page::Menu);
    } else if (!calibrating && calibrationRequested) {
        calibrationRequested = false;
        startCalibration();
    } else if (!calibrating && pageChangeRequested) {
        pageChangeRequested = false;
        renderPage(requestedPage);
    }
}

void setStorage(bool mounted, std::uint32_t capacityMiB) {
    if (mounted == storageMounted && capacityMiB == storageCapacity) return;
    storageMounted = mounted;
    storageCapacity = capacityMiB;
    if (ready) { updateStorage(); updateMenuStatus(); }
}

bool startBenchmark() {
    radio::Snapshot latest;
    if (radio::snapshot(latest)) radioView = latest;
    if (!ready || simulation::snapshot().enabled || calibrating || calibrationRequested || benchmarkReportReady ||
        credentialActive || pairOverlay || radioView.blePairingPending ||
        radioView.wifiScanInProgress || radioView.wifiConnecting || applyPending || importPending ||
        page == Page::Confirm || benchmarkPhase != BenchmarkPhase::Idle) return false;
    partialTimes = {};
    fullTimes = {};
    benchmarkReport = {};
    benchmarkAwaiting = benchmarkFrameDone = benchmarkTouchSeen = false;
    benchmarkTouchMaxGap = benchmarkDurationMs = 0;
    pageChangeRequested = false;
    queuedAction = 0;
    demoEnabled = false;
    lv_timer_set_period(registeredDisplay->refr_timer, LV_DISP_DEF_REFR_PERIOD);
    benchmarkPhase = BenchmarkPhase::Preparing;
    return true;
}

bool takeBenchmarkReport(BenchmarkReport& report) {
    if (!benchmarkReportReady) return false;
    report = benchmarkReport;
    benchmarkReportReady = false;
    return true;
}

bool benchmarkRunning() { return benchmarkPhase != BenchmarkPhase::Idle; }

bool startUsbSimulation() {
    radio::Snapshot latest;
    if (radio::snapshot(latest)) radioView = latest;
    if (!ready || calibrating || calibrationRequested || pairOverlay || radioView.blePairingPending ||
        credentialActive || applyPending || importPending || benchmarkPhase != BenchmarkPhase::Idle) return false;
    // A stopped session may reuse its token before the next UI tick. Starting
    // a session always clears warning history, even when that token is equal.
    usbRpmWarning = RpmWarning::Normal;
    usbRpmSession = 0;
    demoEnabled = false;
    queuedAction = 0;
    requestPage(Page::Panel);
    return true;
}
}
