// Appended ONLY to a private copy of ui.cpp. No layout or production source edits.
namespace ecu::ui {
namespace {
unsigned previewDashboardDeletes = 0, previewRampDraws = 0;
unsigned previewOverlayDeletes = 0;
lv_obj_t* previewWatchedOverlay = nullptr;
std::array<std::uint16_t, rpmBar.w * rpmBar.h> previewRampBefore{};
auto previewRampPixels() {
    decltype(previewRampBefore) pixels{};
    for (int y = 0; y < rpmBar.h; ++y)
        std::copy_n(framebuffer + (rpmBar.y + y) * 320 + rpmBar.x,
                    rpmBar.w, pixels.begin() + y * rpmBar.w);
    return pixels;
}
}
void previewRequire(bool okay, const char* message) {
    if (!okay) { std::fprintf(stderr, "PREVIEW FAIL: %s\n", message); std::abort(); }
}
void previewShow(unsigned requested) {
    previewRequire(requested <= unsigned(Page::SettingsHome), "valid preview page");
    // No NVS exists in the host. Dismiss the boot calibration overlay in this
    // navigation hook; no calibration value is written or claimed as verified.
    if (calibrationOverlay) {
        lv_obj_del(calibrationOverlay);
        calibrationOverlay = calibrationTarget = calibrationInstruction = nullptr;
    }
    calibrating = calibrationRequested = calibrationFinished = false;
    pageChangeRequested = false;
    renderPage(static_cast<Page>(requested));
}
void previewUnits(bool imperial) {
    // Exercise the same actions and navigation as the settings UI.
    requestPage(Page::Settings); tick();
    for (auto field : {SettingField::Speed, SettingField::Temperature}) {
        queuedAction = EditSetting + unsigned(field) * 2 + (imperial ? 1 : 0);
        tick();
    }
    requestPage(Page::Panel); tick();
}
void previewScreenSettings() {
    requestPage(Page::SettingsHome); tick();
    queuedAction = CategoryInput; tick();
    previewRequire(std::strcmp(lv_label_get_text(lv_obj_get_child(pageContent, 0)), "TELA") == 0,
                   "screen settings scene selects its category independently of prior navigation");
}
void previewBounds() {
    lv_obj_update_layout(pageContent);
    for (auto* object : {rpmValue, speedValue, gearValue, temperatureValue, oilValue,
                         faultFooterLabel, faultCountLabel, rpmSignal, rpmScaleLabel,
                         modeLabel, unitLabel}) {
        if (!object || !lv_obj_is_visible(object)) continue;
        lv_area_t bounds{}, parent{};
        lv_obj_get_coords(object, &bounds);
        lv_obj_get_content_coords(lv_obj_get_parent(object), &parent);
        previewRequire(bounds.x1 >= parent.x1 && bounds.x2 <= parent.x2 &&
                       bounds.y1 >= parent.y1 && bounds.y2 <= parent.y2,
                       "instrument fits actual LVGL parent");
        previewRequire(bounds.x1 >= 0 && bounds.x2 < 320 && bounds.y1 >= 0 && bounds.y2 < 240,
                       "instrument fits 320x240 screen");
        lv_point_t measured{};
        lv_txt_get_size(&measured, lv_label_get_text(object), lv_obj_get_style_text_font(object, 0),
                        lv_obj_get_style_text_letter_space(object, 0),
                        lv_obj_get_style_text_line_space(object, 0), LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        previewRequire(measured.x <= lv_obj_get_content_width(object) &&
                       measured.y <= lv_obj_get_content_height(object), "actual glyphs fit label");
    }
    if (rpmValue && speedValue && lv_obj_is_visible(rpmValue) && lv_obj_is_visible(speedValue)) {
        lv_area_t speed{}, unit{};
        lv_obj_get_coords(speedValue, &speed); lv_obj_get_coords(unitLabel, &unit);
        previewRequire(speed.x2 < unit.x1 || speed.y2 < unit.y1 || speed.y1 > unit.y2,
                       "speed digits and unit do not overlap");
    }
}
void previewReadings(const char* rpm, const char* speed, const char* gear, const char* temperature) {
    previewRequire(std::strcmp(lv_label_get_text(rpmValue), rpm) == 0, "expected RPM");
    previewRequire(std::strcmp(lv_label_get_text(speedValue), speed) == 0, "expected speed");
    previewRequire(std::strcmp(lv_label_get_text(gearValue), gear) == 0, "expected gear");
    previewRequire(std::strcmp(lv_label_get_text(temperatureValue), temperature) == 0, "expected temperature");
}
void previewBoot() {
    // Observe begin() before any preview navigation can hide the startup page.
    previewRequire(page == Page::Panel && rpmValue && speedValue,
                   "boot opens the dashboard without menu navigation");
    previewReadings("--", "--", "--", "--");
    previewRequire(!demoEnabled && !simulation::snapshot().enabled,
                   "boot never enables a synthetic source");
    // Host NVS is absent: the required calibration overlay must still appear.
    previewRequire(calibrating && calibrationOverlay, "missing touch calibration is preserved");
    calibrationFinished = true;
    tick();
    previewRequire(!calibrating && page == Page::Panel,
                   "initial calibration finishes on the dashboard");
    std::puts("PASS boot dashboard, unavailable readings and initial calibration");
}
void previewPanelUnits(const char* speed, const char* temperature) {
    previewRequire(page == Page::Panel && std::strcmp(lv_label_get_text(unitLabel), speed) == 0,
                   "dashboard speed legend matches effective units");
    bool found = false;
    for (unsigned i = 0; i < lv_obj_get_child_cnt(pageContent); ++i) {
        auto* object = lv_obj_get_child(pageContent, i);
        if (lv_obj_check_type(object, &lv_label_class) &&
            std::strcmp(lv_label_get_text(object), temperature) == 0) found = true;
    }
    previewRequire(found, "dashboard temperature legend matches effective units");
}
void previewStartImport() {
    requestPage(Page::Settings); tick();
    queuedAction = ImportProfile; tick();
    previewRequire(page == Page::Confirm, "import opens confirmation");
    queuedAction = ConfirmYes; tick();
    previewRequire(importPending, "accepted import remains pending until worker completion");
    returnMenu(nullptr); tick();
    previewRequire(page == Page::Menu, "return to menu while import is pending");
    requestPage(Page::Panel); tick();
    previewRequire(page == Page::Panel && importPending,
                   "reenter dashboard before asynchronous import completes");
}
void previewWatchDashboard() {
    previewDashboardDeletes = previewRampDraws = 0;
    lv_obj_add_event_cb(rpmValue, [](lv_event_t*) { ++previewDashboardDeletes; }, LV_EVENT_DELETE, nullptr);
    lv_obj_add_event_cb(rpmRampObject, [](lv_event_t*) { ++previewRampDraws; }, LV_EVENT_DRAW_MAIN, nullptr);
}
void previewDashboardPreserved() {
    previewRequire(previewDashboardDeletes == 0, "profile updates preserve the existing dashboard objects");
}
void previewRpmPixelsBefore() {
    lv_refr_now(registeredDisplay); completeFlush();
    previewRampBefore = previewRampPixels();
}
void previewRpmPixelsAfter() {
    previewRequire(std::strcmp(lv_label_get_text(rpmScaleLabel), "MAX 10000") == 0,
                   "late profile applies 10000 RPM scale");
    const char* captions[]{"0", "2500", "5000", "7500"};
    for (unsigned index = 0; index < rpmGraduations.size(); ++index)
        previewRequire(std::strcmp(rpmGraduations[index].caption.data(), captions[index]) == 0,
                       "10000 RPM scale uses quarter-scale graduation values");
    const auto presented = previewRampPixels();
    previewRequire(presented != previewRampBefore,
                   "late RPM scale redraws graduation pixels without a new sensor frame");
    // A forced repaint is an independent rendering oracle: the displayed
    // graduations must already match a fresh draw using the current settings.
    lv_obj_invalidate(rpmRampObject);
    lv_refr_now(registeredDisplay); completeFlush();
    previewRequire(previewRampPixels() == presented,
                   "RPM graduations are current before any forced repaint");
    previewRampDraws = 0;
}
void previewRampIdle() {
    previewDashboardPreserved();
    previewRequire(previewRampDraws == 0, "unchanged profile and idle dashboard never redraw the RPM ramp");
}
void previewWatchOverlay(bool calibration) {
    if (calibration) startCalibration();
    previewWatchedOverlay = calibration ? calibrationOverlay : pairOverlay;
    previewRequire(previewWatchedOverlay != nullptr, "expected calibration or pairing overlay is present");
    previewOverlayDeletes = 0;
    lv_obj_add_event_cb(previewWatchedOverlay, [](lv_event_t*) { ++previewOverlayDeletes; },
                        LV_EVENT_DELETE, nullptr);
    previewWatchDashboard();
}
void previewOverlayPreserved(bool calibration) {
    previewDashboardPreserved();
    previewRequire(previewOverlayDeletes == 0 &&
                   previewWatchedOverlay == (calibration ? calibrationOverlay : pairOverlay),
                   "unit updates preserve the active calibration or pairing overlay");
    if (calibration) {
        calibrationFinished = true; tick();
        previewRequire(page == Page::Panel && !calibrating,
                       "profile update during calibration preserves its return to the dashboard");
    }
}
void previewManualCalibration() {
    // Manual recalibration from settings keeps its existing return to the menu.
    previewShow(unsigned(Page::Settings));
    startCalibration();
    calibrationFinished = true;
    tick();
    previewRequire(!calibrating && page == Page::Menu,
                   "manual calibration still returns to the menu");
    std::puts("PASS manual calibration returns to menu");
}
void previewStale() {
    previewReadings("--", "--", "--", "--");
    previewRequire(std::strcmp(lv_label_get_text(modeLabel), "SIM SEM LINK") == 0,
                   "stale synthetic source remains identified");
    previewRequire(!lv_obj_is_visible(faultFooterPanel), "stale clears complete warning strip");
}
}
