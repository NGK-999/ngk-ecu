#pragma once
#include <cstdint>

namespace ecu::ui {
// These functions belong exclusively to the main UI task.
bool begin();
void tick();
void setStorage(bool mounted, std::uint32_t capacityMiB);
}
