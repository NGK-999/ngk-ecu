#pragma once

#include <cstdint>

namespace ecu {

// Main-task admission only. The storage worker alone owns authorization and
// reports the completed request ID through its copied snapshot.
class MaintenanceSubmission {
 public:
  enum class Result { Refused, QueueFull, Queued };

  template <typename Enqueue>
  Result submit(bool ready, uint32_t completed, Enqueue enqueue) {
    if (!ready || pending_ || completed != reported_) return Result::Refused;
    uint32_t id = sequence_ + 1;
    if (!id) id = 1;
    if (!enqueue(id)) return Result::QueueFull;
    sequence_ = pending_ = id;
    return Result::Queued;
  }

  template <typename Emit>
  bool report(uint32_t completed, bool terminal, Emit emit) {
    if (!completed || completed == reported_) return true;
    // An active/stale snapshot and a full text outbox cannot acknowledge a
    // result. Keep admission closed until this outcome is queued for delivery.
    if (completed != pending_ || !terminal || !emit()) return false;
    reported_ = completed;
    pending_ = 0;
    return true;
  }

 private:
  uint32_t sequence_ = 0;
  uint32_t pending_ = 0;
  uint32_t reported_ = 0;
};

}  // namespace ecu
