#include "settings.hpp"
#include "config_codec.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}
bool near(float actual, float expected) {
    return std::fabs(actual - expected) < 0.001F;
}

void testSettingsModel() {
    using namespace ecu;
    const UiSettings defaults;
    check(validSettings(defaults), "boot defaults can be applied without correction");
    check(defaults.brightness == 70 && defaults.refreshMode == RefreshMode::Smooth &&
              defaults.speedUnit == SpeedUnit::Kmh && defaults.tempUnit == TempUnit::Celsius &&
              defaults.rpmScale == 8000 && !defaults.reducedMotion &&
              defaults.rpmYellowPercent == 75 && defaults.rpmRedPercent == 90,
          "boot defaults preserve the agreed display behavior");
    check(defaults == UiSettings{}, "equal settings compare equal");

    auto candidate = defaults;
    for (const auto brightness : {0U, 9U, 101U, 255U}) {
        candidate.brightness = static_cast<std::uint8_t>(brightness);
        check(!validSettings(candidate), "brightness outside 10..100 cannot be applied");
    }
    for (const auto brightness : {10U, 100U}) {
        candidate.brightness = static_cast<std::uint8_t>(brightness);
        check(validSettings(candidate), "brightness endpoints can be applied");
        check(!(candidate == defaults), "brightness participates in equality");
    }
    candidate = defaults;
    for (unsigned value = 0; value <= 255; ++value) {
        candidate.refreshMode = static_cast<RefreshMode>(value);
        check(validSettings(candidate) == (value <= 2), "only declared refresh modes are valid");
    }
    candidate = defaults;
    for (unsigned value = 0; value <= 255; ++value) {
        candidate.speedUnit = static_cast<SpeedUnit>(value);
        check(validSettings(candidate) == (value <= 1), "only declared speed units are valid");
    }
    candidate = defaults;
    for (unsigned value = 0; value <= 255; ++value) {
        candidate.tempUnit = static_cast<TempUnit>(value);
        check(validSettings(candidate) == (value <= 1), "only declared temperature units are valid");
    }
    candidate = defaults;
    for (unsigned value = 0; value <= 65535; ++value) {
        candidate.rpmScale = static_cast<std::uint16_t>(value);
        const bool expected = value >= 1000 && value <= 12000 && value % 500 == 0;
        check(validSettings(candidate) == expected, "RPM scale enforces range and 500 RPM steps");
    }
    candidate = defaults;
    for (unsigned yellow = 0; yellow <= 255; ++yellow) {
        for (unsigned red = 0; red <= 255; ++red) {
            candidate.rpmYellowPercent = static_cast<std::uint8_t>(yellow);
            candidate.rpmRedPercent = static_cast<std::uint8_t>(red);
            const bool expected = yellow >= 1 && red <= 100 && yellow < red;
            check(validSettings(candidate) == expected,
                  "visual RPM bands require 1..100 percent and yellow below red");
        }
    }
    for (unsigned field = 0; field < 7; ++field) {
        candidate = defaults;
        switch (field) {
            case 0: candidate.refreshMode = RefreshMode::Balanced; break;
            case 1: candidate.speedUnit = SpeedUnit::Mph; break;
            case 2: candidate.tempUnit = TempUnit::Fahrenheit; break;
            case 3: candidate.rpmScale = 8500; break;
            case 4: candidate.reducedMotion = true; break;
            case 5: candidate.rpmYellowPercent = 74; break;
            case 6: candidate.rpmRedPercent = 91; break;
        }
        check(!(candidate == defaults), "every display setting participates in equality");
    }
    check(framePeriodMs(RefreshMode::Smooth) == 17, "smooth targets a 17 ms period");
    check(framePeriodMs(RefreshMode::Balanced) == 33, "balanced targets a 33 ms period");
    check(framePeriodMs(RefreshMode::Economy) == 50, "economy targets a 50 ms period");
    check(near(speedDisplay(160.9344F, SpeedUnit::Kmh), 160.9344F),
          "km/h display preserves the canonical speed");
    check(near(speedDisplay(160.9344F, SpeedUnit::Mph), 100.0F),
          "mph display converts the canonical km/h value");
    check(near(temperatureDisplay(100.0F, TempUnit::Celsius), 100.0F),
          "Celsius display preserves the canonical temperature");
    check(near(temperatureDisplay(100.0F, TempUnit::Fahrenheit), 212.0F) &&
              near(temperatureDisplay(-40.0F, TempUnit::Fahrenheit), -40.0F),
          "Fahrenheit display converts positive and negative temperatures");
}

// Independent profile fixtures let semantic rejection tests carry a valid checksum.
std::string signBody(std::string body) {
    std::uint32_t checksum = 2166136261U;
    for (unsigned char byte : body) {
        checksum ^= byte;
        checksum *= 16777619U;
    }
    char trailer[32];
    std::snprintf(trailer, sizeof(trailer), "checksum=%08lX\n",
                  static_cast<unsigned long>(checksum));
    return body + trailer;
}

const std::string defaultBody =
    "NGK_UI_SETTINGS=1\n"
    "brightness=70\n"
    "refresh_mode=0\n"
    "speed_unit=0\n"
    "temp_unit=0\n"
    "rpm_scale=8000\n"
    "reduced_motion=0\n"
    "rpm_yellow_percent=75\n"
    "rpm_red_percent=90\n";

void expectRejected(std::string_view input, const char* message) {
    ecu::UiSettings output;
    output.brightness = 35;
    output.reducedMotion = true;
    const auto previous = output;
    check(!ecu::decodeSettings(input.data(), input.size(), output), message);
    check(output == previous, "rejected profile never changes the previous configuration");
}

std::string changedBody(std::string_view original, std::string_view replacement) {
    std::string body = defaultBody;
    const auto position = body.find(original);
    check(position != std::string::npos, "semantic fixture refers to an existing field");
    if (position != std::string::npos) body.replace(position, original.size(), replacement);
    return body;
}

void testSettingsCodec() {
    using namespace ecu;
    char encoded[513]{};
    const auto length = encodeSettings(UiSettings{}, encoded, sizeof(encoded));
    check(length > 0 && length <= 512, "a valid profile fits the bounded ASCII format");
    // Filled from a separately calculated FNV-1a fixture, not the encoder.
    const std::string expected = defaultBody + "checksum=FBEBB1ED\n";
    check(std::string_view(encoded, length) == expected,
          "encoding preserves the versioned format and known integrity checksum");
    check(length < sizeof(encoded) && encoded[length] == '\0',
          "successful encoding supplies a terminator excluded from its byte length");
    for (std::size_t index = 0; index < length; ++index) {
        check(encoded[index] == '\n' || (encoded[index] >= 32 && encoded[index] <= 126),
              "encoded profile uses printable ASCII plus LF");
    }
    UiSettings output;
    output.brightness = 35;
    check(decodeSettings(encoded, length, output) && output == UiSettings{},
          "a profile decodes using its explicit byte length without including NUL");
    check(decodeSettings(expected.data(), expected.size(), output) && output == UiSettings{},
          "a known independent default fixture decodes");

    UiSettings alternate;
    alternate.brightness = 10;
    alternate.refreshMode = RefreshMode::Economy;
    alternate.speedUnit = SpeedUnit::Mph;
    alternate.tempUnit = TempUnit::Fahrenheit;
    alternate.rpmScale = 12000;
    alternate.reducedMotion = true;
    alternate.rpmYellowPercent = 1;
    alternate.rpmRedPercent = 100;
    const auto alternateLength = encodeSettings(alternate, encoded, sizeof(encoded));
    check(alternateLength > 0 && decodeSettings(encoded, alternateLength, output) &&
              output == alternate,
          "nondefault values and valid endpoints survive profile round trips");
    alternate.brightness = 100;
    alternate.refreshMode = RefreshMode::Balanced;
    alternate.rpmScale = 1000;
    const auto secondLength = encodeSettings(alternate, encoded, sizeof(encoded));
    check(secondLength > 0 && decodeSettings(encoded, secondLength, output) && output == alternate,
          "the remaining refresh mode and range endpoints survive a profile round trip");

    char guarded[513];
    std::memset(guarded, '!', sizeof(guarded));
    check(encodeSettings(UiSettings{}, guarded, expected.size()) == 0,
          "a buffer without space for the terminating NUL is rejected");
    check(guarded[0] == '!' && guarded[sizeof(guarded) - 1] == '!',
          "capacity failure does not expose a partial profile or cross the buffer");
    check(encodeSettings(UiSettings{}, guarded, 0) == 0 &&
              encodeSettings(UiSettings{}, nullptr, sizeof(guarded)) == 0,
          "empty capacity and null output are rejected");
    auto invalid = UiSettings{};
    invalid.rpmYellowPercent = 95;
    check(encodeSettings(invalid, guarded, sizeof(guarded)) == 0 && guarded[0] == '!',
          "invalid settings cannot be serialized as an applicable profile");
    check(encodeSettings(UiSettings{}, guarded, expected.size() + 1) == expected.size(),
          "the exact required capacity including NUL succeeds");

    for (std::size_t index = 0; index < expected.size(); ++index) {
        auto corrupt = expected;
        corrupt[index] ^= 1;
        expectRejected(corrupt, "changing any single encoded byte is rejected");
    }
    for (std::size_t size = 0; size < expected.size(); ++size) {
        expectRejected(std::string_view(expected.data(), size), "every truncated profile is rejected");
    }
    expectRejected(expected + "junk", "trailing junk is rejected");
    expectRejected(expected + "\n", "an extra trailing line is rejected");
    expectRejected(std::string(expected + '\0'), "an included NUL is rejected");
    expectRejected(std::string(513, 'X'), "input exceeding 512 bytes is rejected");
    check(!decodeSettings(nullptr, expected.size(), output), "null input is rejected");

    const auto maximumProfile = signBody(changedBody("brightness=70\n",
        "brightness=" + std::string(512 - expected.size(), '0') + "70\n"));
    check(maximumProfile.size() == 512 &&
              decodeSettings(maximumProfile.data(), maximumProfile.size(), output) &&
              output == UiSettings{},
          "a complete valid profile can reach the 512-byte boundary");
    expectRejected(signBody(changedBody("brightness=70\n",
        "brightness=" + std::string(513 - expected.size(), '0') + "70\n")),
        "an otherwise valid 513-byte profile exceeds the explicit input bound");

    for (const auto& field : {"brightness=70\n", "refresh_mode=0\n", "speed_unit=0\n",
                              "temp_unit=0\n", "rpm_scale=8000\n", "reduced_motion=0\n",
                              "rpm_yellow_percent=75\n", "rpm_red_percent=90\n"}) {
        expectRejected(signBody(changedBody(field, "")), "each required key must be present");
        expectRejected(signBody(defaultBody + field), "duplicate keys are rejected");
    }
    for (const auto& value : {"0", "9", "101", "256", "4294967296", "18446744073709551616",
                              "-1", "+70", "70 ", " 70", "70x", "", "7\t0", "7\r0"}) {
        expectRejected(signBody(changedBody("brightness=70\n", std::string("brightness=") + value + "\n")),
                       "brightness rejects range violations, overflow and malformed integers");
    }
    for (const auto& replacement : {"refresh_mode=3\n", "refresh_mode=256\n", "speed_unit=2\n",
                                    "temp_unit=2\n", "rpm_scale=999\n", "rpm_scale=12500\n",
                                    "rpm_scale=65536\n", "rpm_scale=8250\n", "reduced_motion=2\n",
                                    "rpm_yellow_percent=0\n", "rpm_yellow_percent=90\n",
                                    "rpm_yellow_percent=91\n", "rpm_red_percent=0\n",
                                    "rpm_red_percent=101\n", "rpm_red_percent=256\n"}) {
        const std::string_view line = replacement;
        const auto separator = line.find('=');
        const auto start = defaultBody.find(line.substr(0, separator + 1));
        const auto end = defaultBody.find('\n', start);
        expectRejected(signBody(changedBody(std::string_view(defaultBody).substr(start, end - start + 1), line)),
                       "all display fields are validated before the profile is applied");
    }
    expectRejected(signBody(changedBody("NGK_UI_SETTINGS=1\n", "NGK_UI_SETTINGS=2\n")),
                   "unknown format versions are rejected");
    expectRejected(signBody(defaultBody + "password=secret\n"), "unknown keys are rejected");
    expectRejected(signBody(defaultBody + "checksum=00000000\n"), "duplicate checksum is rejected");
    expectRejected(signBody(changedBody("brightness=70\n", "brightness=7\xFF\n")),
                   "non-ASCII bytes are rejected");

    const std::string reordered =
        "NGK_UI_SETTINGS=1\n"
        "rpm_red_percent=90\n"
        "rpm_yellow_percent=75\n"
        "reduced_motion=0\n"
        "rpm_scale=8000\n"
        "temp_unit=0\n"
        "speed_unit=0\n"
        "refresh_mode=0\n"
        "brightness=70\n";
    const auto reorderedProfile = signBody(reordered);
    check(decodeSettings(reorderedProfile.data(), reorderedProfile.size(), output) && output == UiSettings{},
          "unique complete fields can be read in a different order");
}
}

int main() {
    testSettingsModel();
    testSettingsCodec();
    if (failures) return EXIT_FAILURE;
    std::puts("settings: display validation, conversions and bounded profile codec PASS");
    return EXIT_SUCCESS;
}
