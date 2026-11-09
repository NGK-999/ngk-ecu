#include "ui_layout.hpp"

#include <cstdio>
#include <cstdlib>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}
bool tap(ecu::ui::DashboardDoubleTap& gesture, ecu::ui::Point point,
         std::uint32_t pressAt, std::uint32_t releaseAt) {
    check(!gesture.update(true, point, pressAt), "a press alone never returns to the menu");
    return gesture.update(false, point, releaseAt);
}
}

int main() {
    using namespace ecu::ui;
    DashboardDoubleTap gesture;
    check(!gesture.update(false, {50, 50}, 10), "a release without a press cannot count as a tap");
    check(!tap(gesture, {50, 50}, 100, 160), "one complete tap keeps the dashboard visible");
    check(tap(gesture, {70, 65}, 250, 320), "two nearby completed taps return to the menu");
    check(!gesture.update(false, {70, 65}, 330), "the same release cannot trigger twice");
    check(!tap(gesture, {50, 50}, 400, 450), "a successful pair is consumed");

    gesture.reset();
    check(!tap(gesture, {50, 50}, 100, 150), "new pair begins with one tap");
    check(!tap(gesture, {50, 50}, 500, 551), "second release after 400 ms does not qualify");
    check(tap(gesture, {50, 50}, 600, 650), "late second tap can begin a new pair");
    gesture.reset();
    tap(gesture, {50, 50}, 100, 150);
    check(tap(gesture, {98, 50}, 500, 550), "400 ms window and 48 px proximity include their boundaries");
    gesture.reset();
    tap(gesture, {50, 50}, 100, 150);
    check(!tap(gesture, {99, 50}, 200, 250), "two far-apart taps cannot return to the menu");

    gesture.reset();
    tap(gesture, {50, 50}, 100, 150);
    check(!tap(gesture, {50, 50}, 170, 521), "a hold longer than 350 ms is not a tap");
    check(!tap(gesture, {50, 50}, 550, 600), "a long hold also cancels the previous first tap");
    gesture.reset();
    tap(gesture, {50, 50}, 100, 150);
    check(tap(gesture, {50, 50}, 170, 520), "a 350 ms hold remains within the tap limit");

    gesture.reset();
    tap(gesture, {50, 50}, 100, 150);
    gesture.update(true, {50, 50}, 200);
    gesture.update(true, {100, 50}, 220);
    gesture.update(true, {50, 50}, 240);
    check(!gesture.update(false, {50, 50}, 260), "dragging out and back never qualifies as a tap");
    check(!tap(gesture, {50, 50}, 300, 350), "dragging cancels the pending pair");
    gesture.reset();
    tap(gesture, {50, 50}, 100, 150);
    gesture.update(true, {50, 50}, 200);
    gesture.update(true, {68, 50}, 220);
    check(gesture.update(false, {68, 50}, 260), "small movement up to 18 px remains a tap");
    gesture.reset();
    tap(gesture, {10, 50}, 100, 150);
    gesture.update(true, {10, 50}, 200);
    gesture.update(true, {-1, 50}, 220);
    check(!gesture.update(false, {-1, 50}, 260), "leaving the screen invalidates the current tap");

    gesture.reset();
    tap(gesture, {50, 50}, 100, 150);
    gesture.reset();
    check(!tap(gesture, {50, 50}, 200, 250), "entering or leaving a page clears any pending tap");
    gesture.reset();
    tap(gesture, {50, 50}, 0xFFFFFF80U, 0xFFFFFFC0U);
    check(tap(gesture, {50, 50}, 20, 60), "tap duration and pairing survive uint32 wraparound");

    // Real debouncer and gesture together: release noise cannot create a pair.
    gesture.reset();
    TouchDebouncer debounce;
    Point last{50, 50};
    const auto sample = [&](bool down, Point point, std::uint32_t now) {
        if (down) last = point;
        return gesture.update(debounce.update(down, point, now), last, now);
    };
    check(!sample(false, {}, 0), "initial raw release is harmless");
    sample(true, {50, 50}, 100);
    sample(true, {50, 50}, 126);
    sample(false, {}, 140);
    sample(true, {50, 50}, 150);
    sample(false, {}, 180);
    check(!sample(false, {}, 206), "one bouncing raw contact is only one complete tap");
    sample(true, {50, 50}, 240);
    sample(true, {50, 50}, 266);
    sample(true, {100, 50}, 280);
    sample(false, {}, 300);
    check(!sample(false, {}, 326), "actual mapped movement rejects a drag despite frozen LVGL position");
    sample(true, {50, 50}, 400);
    sample(true, {50, 50}, 426);
    sample(false, {}, 450);
    check(!sample(false, {}, 476), "first clean debounced tap starts a fresh pair");
    sample(true, {50, 50}, 500);
    sample(true, {50, 50}, 526);
    sample(false, {}, 550);
    check(sample(false, {}, 576), "two clean debounced contacts return to the menu");

    if (failures) return EXIT_FAILURE;
    std::puts("dashboard gesture: complete taps, timing, movement and debounce PASS");
    return EXIT_SUCCESS;
}
