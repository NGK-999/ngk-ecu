#pragma once

#include "simulation.hpp"

namespace ecu::simulation {
// Single owner: call only from the main/UI task. No radio, LVGL, Serial, SD,
// queues or background task is used. The integrator gates maintenance mode.
bool beginSession(std::uint32_t token);
bool endSession(std::uint32_t token);
bool submit(const Frame& frame);
Snapshot snapshot();
void endAnySession();
}  // namespace ecu::simulation
