#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace ecu {

inline constexpr uint64_t kMaximumFormatBytes = uint64_t{32} * 1024 * 1024 * 1024;

// Filesystem selection only: false also covers unsupported capacities.
// The caller must obtain authorization before invoking any physical formatter.
constexpr bool fat32ForCapacity(uint64_t bytes) noexcept {
  return bytes > uint64_t{2} * 1024 * 1024 * 1024 &&
         bytes <= kMaximumFormatBytes;
}

// Keep one instance for the entire maintenance boot. This policy performs no
// hardware I/O. The boot token must come from that boot; observedSectors must be
// a fresh measurement of the identified card, with 512-byte sectors.
class MaintenancePolicy {
 public:
  static constexpr std::size_t kMaxCommandLength = 31;

  MaintenancePolicy() = default;
  MaintenancePolicy(const MaintenancePolicy&) = delete;
  MaintenancePolicy& operator=(const MaintenancePolicy&) = delete;

  // Exact wire command, after the transport removes its line terminator:
  // FORMAT_SD <decimal sectors> <decimal bootToken>
  // Single ASCII spaces, 1..10 digits per value, no signs or trailing content.
  // A true result is consumed BEFORE hardware work, even if formatting fails.
  // Invalid input does not consume authorization. An absent card invalidates
  // this boot, so reinsertions need a fresh maintenance boot and confirmation.
  bool request(const char* line, uint32_t observedSectors, uint32_t bootToken,
               bool cardPresent) noexcept {
    if (!cardPresent) {
      invalidateCard();
    }
    if (consumed_ || cardInvalidated_ || line == nullptr) {
      return false;
    }

    std::size_t length = 0;
    while (length <= kMaxCommandLength && line[length] != '\0') {
      ++length;
    }
    if (length > kMaxCommandLength) {
      return false;
    }

    constexpr char prefix[] = "FORMAT_SD ";
    constexpr std::size_t prefixLength = sizeof(prefix) - 1;
    if (length <= prefixLength) {
      return false;
    }
    for (std::size_t i = 0; i < prefixLength; ++i) {
      if (line[i] != prefix[i]) {
        return false;
      }
    }

    std::size_t cursor = prefixLength;
    uint32_t expectedSectors = 0;
    uint32_t expectedToken = 0;
    if (!parseDecimal(line, length, cursor, expectedSectors) ||
        cursor == length || line[cursor++] != ' ' ||
        !parseDecimal(line, length, cursor, expectedToken) || cursor != length) {
      return false;
    }

    const uint64_t observedBytes = uint64_t{observedSectors} * 512;
    if (observedSectors == 0 || observedBytes > kMaximumFormatBytes ||
        expectedSectors != observedSectors || expectedToken != bootToken) {
      return false;
    }

    consumed_ = true;
    return true;
  }

  // Call on removal/replacement; geometry cannot identify equal-sized cards.
  // Invalidation is irreversible for this instance, including after success.
  void invalidateCard() noexcept { cardInvalidated_ = true; }

  bool consumed() const noexcept { return consumed_; }

 private:
  static bool parseDecimal(const char* line, std::size_t length,
                           std::size_t& cursor, uint32_t& value) noexcept {
    const std::size_t start = cursor;
    value = 0;
    while (cursor < length && line[cursor] >= '0' && line[cursor] <= '9') {
      const auto digit = static_cast<uint32_t>(line[cursor] - '0');
      if (cursor - start >= 10 ||
          value > (std::numeric_limits<uint32_t>::max() - digit) / 10) {
        return false;
      }
      value = value * 10 + digit;
      ++cursor;
    }
    return cursor > start;
  }

  bool consumed_ = false;
  bool cardInvalidated_ = false;
};

}  // namespace ecu
