#pragma once
#include <cstdint>
namespace ecu {
constexpr bool shouldProbeStorage(bool mounted, bool explicitRefresh,
                                  std::uint32_t now, std::uint32_t previousAttempt) {
    return explicitRefresh || (!mounted && std::uint32_t(now - previousAttempt) >= 5000);
}
}
