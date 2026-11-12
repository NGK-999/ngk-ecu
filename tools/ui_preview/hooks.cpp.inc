// Appended ONLY to a private copy of ui.cpp. No layout or production source edits.
namespace ecu::ui {
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
    settingsEditor.values.tempUnit = imperial ? TempUnit::Fahrenheit : TempUnit::Celsius;
    settingsEditor.values.speedUnit = imperial ? SpeedUnit::Mph : SpeedUnit::Kmh;
    usbPaintPending = usbDetailsPending = true;
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
void previewStale() {
    previewReadings("--", "--", "--", "--");
    previewRequire(std::strcmp(lv_label_get_text(modeLabel), "SIM SEM LINK") == 0,
                   "stale synthetic source remains identified");
    previewRequire(!lv_obj_is_visible(faultFooterPanel), "stale clears complete warning strip");
}
}
