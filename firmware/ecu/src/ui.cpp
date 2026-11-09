#include "ui.hpp"
#include "board.hpp"
#include "ui_layout.hpp"

#include <Preferences.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <lvgl.h>

namespace ecu::ui {
static_assert(sizeof(lv_color_t) == 2 && LV_COLOR_16_SWAP == 1,
              "Display DMA requires byte-swapped RGB565");
namespace {
constexpr std::uint32_t background = 0x0B111C;
constexpr std::uint32_t card = 0x20242D;
constexpr std::uint32_t buttonColor = 0x282D37;
constexpr std::uint32_t text = 0xF5F5F7;
constexpr std::uint32_t secondary = 0xB4BAC6;
constexpr std::uint32_t accent = 0x70ADFF;
constexpr std::uint32_t border = 0x424752;
constexpr const char* unknown = "--";
constexpr const char* noSignal = "SEM SINAL";
enum class Page : std::uint8_t { Menu, Panel, Sensors, Settings, System };

Display display;
ResistiveTouch touch;
Calibration calibration;
TouchDebouncer debounce;
DashboardDoubleTap dashboardGesture;
Point gesturePosition{};
lv_disp_draw_buf_t drawBuffer;
lv_disp_drv_t displayDriver;
lv_indev_drv_t inputDriver;
lv_disp_drv_t* pendingFlush = nullptr;
lv_color_t* bufferA = nullptr;
lv_color_t* bufferB = nullptr;
lv_obj_t* pageContent = nullptr;
lv_obj_t* storageLabel = nullptr;
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

void completeFlush() {
    if (pendingFlush && !display.dmaBusy()) {
        display.endWrite();
        auto* completed = pendingFlush;
        pendingFlush = nullptr;
        // LVGL may reuse a tile only after DMA no longer owns its memory.
        lv_disp_flush_ready(completed);
    }
}

void flushDisplay(lv_disp_drv_t* driver, const lv_area_t* area, lv_color_t* pixels) {
    display.waitDMA();
    completeFlush();
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

lv_obj_t* surface(lv_obj_t* parent, Rect rect, std::uint32_t color = card, int radius = 16) {
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
    lv_obj_set_style_radius(object, 14, 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(border), 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(0x304764), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(object, lv_color_hex(accent), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(object, lv_color_hex(accent), LV_STATE_CHECKED);
    noScroll(object);
    auto* caption = label(object, value, 0, 0, rect.w - 4, font);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(caption);
    lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, userData);
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
        requestedPage = static_cast<Page>(index + 1);
        pageChangeRequested = true;
    }
}

void returnMenu(lv_event_t*) {
    requestedPage = Page::Menu;
    pageChangeRequested = true;
}

void recalibrate(lv_event_t*) { calibrationRequested = true; }

void centeredLabel(lv_obj_t* parent, const char* value, int y, int width,
                   const lv_font_t* font, std::uint32_t color = text) {
    auto* object = label(parent, value, 4, y, width - 8, font, color);
    lv_obj_set_style_text_align(object, LV_TEXT_ALIGN_CENTER, 0);
}

void renderPage(Page next) {
    page = next;
    storageLabel = nullptr;
    dashboardGesture.reset();
    debounce.reset();
    gesturePosition = {};
    lv_obj_clean(pageContent);
    label(pageContent, "NGK ECU", 8, 7, 180, &lv_font_montserrat_16);
    auto* bench = label(pageContent, "BANCADA", 216, 8, 96, &lv_font_montserrat_12, accent);
    lv_obj_set_style_text_align(bench, LV_TEXT_ALIGN_RIGHT, 0);
    if (page == Page::Menu) {
        constexpr std::array<const char*, 4> names{{"Painel", "Sensores", "Ajustes", "Sistema"}};
        for (std::size_t i = 0; i < names.size(); ++i) {
            button(pageContent, names[i], navigation[i], selectPage,
                   reinterpret_cast<void*>(std::uintptr_t(i)), &lv_font_montserrat_28);
        }
    } else if (page == Page::Panel) {
        auto* speed = surface(pageContent, speedCard);
        centeredLabel(speed, "VELOCIDADE", 8, speedCard.w, &lv_font_montserrat_12, secondary);
        centeredLabel(speed, unknown, 35, speedCard.w, &lv_font_montserrat_48);
        centeredLabel(speed, "km/h", 91, speedCard.w, &lv_font_montserrat_14, secondary);
        centeredLabel(speed, noSignal, 109, speedCard.w, &lv_font_montserrat_12, secondary);
        auto* rpm = surface(pageContent, rpmCard);
        centeredLabel(rpm, "RPM", 8, rpmCard.w, &lv_font_montserrat_16, secondary);
        centeredLabel(rpm, unknown, 35, rpmCard.w, &lv_font_montserrat_48);
        centeredLabel(rpm, noSignal, 104, rpmCard.w, &lv_font_montserrat_14, secondary);
        auto* temperature = surface(pageContent, temperatureCard);
        label(temperature, "BLOCO °C", 10, 5, 130, &lv_font_montserrat_12, secondary);
        label(temperature, unknown, 10, 16, 44, &lv_font_montserrat_28);
        label(temperature, noSignal, 54, 27, 88, &lv_font_montserrat_12, secondary);
        auto* oil = surface(pageContent, oilStrip);
        label(oil, "OLEO", 10, 5, 130, &lv_font_montserrat_12, secondary);
        label(oil, unknown, 10, 16, 44, &lv_font_montserrat_28);
        label(oil, noSignal, 54, 27, 88, &lv_font_montserrat_12, secondary);
        auto* hint = label(pageContent, "2 toques: menu", dashboardHint.x, dashboardHint.y,
                           dashboardHint.w, &lv_font_montserrat_12, secondary);
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    } else if (page == Page::Sensors) {
        auto* panel = surface(pageContent, content);
        label(panel, "SENSORES", 12, 10, 278, &lv_font_montserrat_16);
        constexpr std::array<const char*, 4> names{{"RPM", "Bloco °C", "Velocidade", "Oleo"}};
        for (std::size_t i = 0; i < names.size(); ++i) {
            label(panel, names[i], 12, 35 + int(i) * 23, 110, &lv_font_montserrat_14);
            label(panel, "-- / SEM SINAL", 130, 36 + int(i) * 23, 162,
                  &lv_font_montserrat_12, secondary);
        }
        label(panel, "Entradas do motor desabilitadas", 12, 125, 282,
              &lv_font_montserrat_12, accent);
    } else if (page == Page::Settings) {
        auto* panel = surface(pageContent, content);
        label(panel, "AJUSTES DE BANCADA", 12, 10, 278, &lv_font_montserrat_16);
        label(panel, "Toque por clique. Sem arrastar.\nCalibracao local de cinco pontos.",
              12, 38, 280, &lv_font_montserrat_12, secondary);
        label(panel, calibrationStored ? "Calibracao salva na placa" : "Calibracao desta sessao",
              12, 69, 280, &lv_font_montserrat_12, accent);
        button(panel, "Calibrar toque", {12, 86, 278, 44}, recalibrate);
    } else {
        auto* panel = surface(pageContent, content);
        label(panel, "SISTEMA", 12, 10, 278, &lv_font_montserrat_16);
        storageLabel = label(panel, "", 12, 39, 278, &lv_font_montserrat_16);
        updateStorage();
        label(panel, "Firmware de bancada\nSem sensores ou atuacao habilitados",
              12, 96, 282, &lv_font_montserrat_12, secondary);
    }
    if (page != Page::Menu && page != Page::Panel) {
        button(pageContent, "Menu", menuReturn, returnMenu);
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
    Point raw{};
    const bool down = touch.sample(raw);
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
    if (page == Page::Panel && dashboardGesture.update(pressed, gesturePosition, now)) {
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
    if (!lv_disp_drv_register(&displayDriver)) return false;
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
    calibrationStored = loadCalibration();
    if (!calibrationStored) startCalibration();
    return true;
}

void tick() {
    if (!ready) return;
    completeFlush();
    lv_timer_handler();
    completeFlush();
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
    if (ready) updateStorage();
}
}
