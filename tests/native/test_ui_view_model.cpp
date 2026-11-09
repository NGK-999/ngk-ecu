#include "ui_layout.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
void check(bool okay, const char* why) {
    if (!okay) { std::fprintf(stderr, "FAIL: %s\n", why); std::exit(1); }
}
}
int main() {
    using namespace ecu;
    using namespace ecu::ui;
    SettingsDraft draft;
    draft.load(UiSettings{});
    check(!draft.dirty(), "loaded settings start saved");
    check(draft.adjust(SettingField::Brightness, 1) && draft.values.brightness == 80 && draft.dirty(),
          "editing brightness creates a real unsaved draft");
    const auto submitted = draft.values;
    draft.adjust(SettingField::Brightness, 1);
    draft.acknowledge(submitted);
    check(draft.values.brightness == 90 && draft.dirty(),
          "late save acknowledgement preserves a newer edit");
    for (int step = 0; step < 50; ++step) draft.adjust(SettingField::Yellow, 1);
    check(validSettings(draft.values) && draft.values.rpmYellowPercent < draft.values.rpmRedPercent,
          "visual yellow band cannot cross the red band");
    for (int step = 0; step < 50; ++step) draft.adjust(SettingField::RpmScale, -1);
    check(draft.values.rpmScale == 1000, "RPM scale stops at its validated minimum");
    for (int step = 0; step < 50; ++step) draft.adjust(SettingField::RpmScale, 1);
    check(draft.values.rpmScale == 12000, "RPM scale stops at its validated maximum");
    draft.adjust(SettingField::Speed, 1);
    draft.adjust(SettingField::Temperature, 1);
    check(draft.values.speedUnit == SpeedUnit::Mph && draft.values.tempUnit == TempUnit::Fahrenheit,
          "unit controls update the units used by the view");
    LocalTextEntry entry;
    entry.begin(true, 64);
    for (char character : "abcd1234") if (character) entry.append(character);
    char visible[20];
    entry.preview(visible, sizeof(visible));
    check(std::strcmp(visible, "********") == 0 && entry.validPassword(),
          "password preview never contains its input bytes");
    entry.backspace();
    check(entry.size() == 7 && !entry.validPassword(), "short password remains invalid");
    entry.clear();
    check(entry.size() == 0 && entry.text()[0] == 0, "cancelling erases local text");
    entry.begin(false, 32);
    for (int index = 0; index < 33; ++index) entry.append('x');
    check(entry.size() == 32 && !entry.append('\n'), "SSID input bounds and rejects control bytes");
    entry.begin(true, 64);
    for (int index = 0; index < 64; ++index) entry.append('a');
    check(entry.validPassword(), "64 hex digits are accepted as a PSK");
    entry.backspace(); entry.append('z');
    check(!entry.validPassword(), "64 nonhex bytes cannot be submitted as a PSK");
    for (unsigned code = 32; code <= 126; ++code) {
        check(LocalTextEntry::key((code - 32) / 18, (code - 32) % 18) == char(code),
              "paged keyboard reaches every printable ASCII byte");
    }
    check(LocalTextEntry::key(5, 17) == 0, "last keyboard bank does not emit non-ASCII bytes");
    std::puts("UI drafts and private local keyboard: PASS");
}
