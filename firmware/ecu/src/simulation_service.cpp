#include "simulation_service.hpp"

#include <Arduino.h>

namespace ecu::simulation {
namespace {
State state;
std::uint32_t nowMs() { return static_cast<std::uint32_t>(millis()); }
}  // namespace

bool beginSession(std::uint32_t token) { return state.start(token, nowMs()); }
bool endSession(std::uint32_t token) { return state.stop(token); }
bool submit(const Frame& frame) { return state.accept(frame, nowMs()); }
Snapshot snapshot() { return state.snapshot(nowMs()); }
void endAnySession() { state = State{}; }
}  // namespace ecu::simulation
