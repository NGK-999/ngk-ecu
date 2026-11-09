#include "maintenance_policy.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

namespace {

constexpr uint32_t kSectors = 4'194'304;
constexpr uint32_t kToken = 123'456;
constexpr uint64_t kGiB = uint64_t{1024} * 1024 * 1024;

bool request(ecu::MaintenancePolicy& policy, const char* line) {
  return policy.request(line, kSectors, kToken, true);
}

void accepts_only_explicit_complete_command() {
  ecu::MaintenancePolicy policy;
  assert(!policy.consumed());
  assert(request(policy, "FORMAT_SD 4194304 123456"));
  assert(policy.consumed());
}

void rejects_replay_even_after_format_failure() {
  ecu::MaintenancePolicy policy;
  assert(request(policy, "FORMAT_SD 4194304 123456"));
  // The caller may now fail to format; the authorization stays consumed.
  assert(!request(policy, "FORMAT_SD 4194304 123456"));
  assert(!policy.request("FORMAT_SD 4194304 123457", kSectors, kToken + 1,
                         true));
}

void rejects_wrong_geometry_and_stale_boot_token() {
  ecu::MaintenancePolicy policy;
  assert(!request(policy, "FORMAT_SD 4194305 123456"));
  assert(!request(policy, "FORMAT_SD 4194304 123455"));
  assert(!policy.consumed());
  assert(request(policy, "FORMAT_SD 4194304 123456"));
}

void absence_and_removal_invalidate_the_boot() {
  ecu::MaintenancePolicy absent;
  assert(!absent.request("FORMAT_SD 4194304 123456", kSectors, kToken,
                         false));
  assert(!request(absent, "FORMAT_SD 4194304 123456"));

  ecu::MaintenancePolicy removed;
  removed.invalidateCard();
  removed.invalidateCard();
  assert(!request(removed, "FORMAT_SD 4194304 123456"));

  ecu::MaintenancePolicy accepted;
  assert(request(accepted, "FORMAT_SD 4194304 123456"));
  accepted.invalidateCard();
  assert(accepted.consumed());
  assert(!request(accepted, "FORMAT_SD 4194304 123456"));
}

void rejects_incomplete_or_non_decimal_input() {
  const char* invalid[] = {
      nullptr,
      "",
      "FORMAT_SD",
      "FORMAT_SD ",
      "FORMAT_SD 4194304",
      "FORMAT_SD 4194304 ",
      "FORMAT_SD  4194304 123456",
      " FORMAT_SD 4194304 123456",
      "FORMAT_SD\t4194304 123456",
      "FORMAT_SD 4194304\t123456",
      "FORMAT_SD 4194304  123456",
      "FORMAT_SD -4194304 123456",
      "FORMAT_SD +4194304 123456",
      "FORMAT_SD 4194304 -123456",
      "FORMAT_SD 4194304 +123456",
      "FORMAT_SD 0x400000 123456",
      "FORMAT_SD 4194304 1.23456e5",
      "FORMAT_SD 4194304 123456 ",
      "FORMAT_SD 4194304 123456\r",
      "FORMAT_SD 4194304 123456\n",
      "FORMAT_SD 4194304 123456 garbage",
      "FORMAT_SD 4194304 123456;FORMAT_SD 4194304 123456",
      "FORMAT_SD_FAT32 4194304 123456",
      "format_sd 4194304 123456",
      "FORMAT_SDX 4194304 123456",
      "FORMAT_SD 4294967296 123456",
      "FORMAT_SD 18446744073709551616 123456",
      "FORMAT_SD 4194304 4294967296",
      "FORMAT_SD 4194304 18446744073709551616",
      "FORMAT_SD 00000000000004194304 123456",
  };
  for (const char* line : invalid) {
    ecu::MaintenancePolicy policy;
    assert(!request(policy, line));
    assert(!policy.consumed());
    assert(request(policy, "FORMAT_SD 4194304 123456"));
  }

  ecu::MaintenancePolicy policy;
  const std::string longLine(4096, '9');
  assert(!request(policy, longLine.c_str()));
  // A 32-byte non-terminated buffer must be rejected without reading past it.
  const char unterminated[32] = {
      'F', 'O', 'R', 'M', 'A', 'T', '_', 'S', 'D', ' ', '9', '9', '9', '9', '9',
      '9', '9', '9', '9', '9', ' ', '9', '9', '9', '9', '9', '9', '9', '9', '9',
      '9', '9'};
  assert(!request(policy, unterminated));
}

void respects_capacity_boundaries_without_integer_overflow() {
  ecu::MaintenancePolicy zero;
  assert(!zero.request("FORMAT_SD 0 123456", 0, kToken, true));

  ecu::MaintenancePolicy smallest;
  assert(smallest.request("FORMAT_SD 1 0", 1, 0, true));

  ecu::MaintenancePolicy maximum;
  assert(maximum.request("FORMAT_SD 67108864 4294967295", 67'108'864,
                         std::numeric_limits<uint32_t>::max(), true));

  ecu::MaintenancePolicy oversized;
  assert(!oversized.request("FORMAT_SD 67108865 123456", 67'108'865,
                            kToken, true));

  ecu::MaintenancePolicy overflow;
  assert(!overflow.request("FORMAT_SD 4294967295 123456",
                           std::numeric_limits<uint32_t>::max(), kToken, true));
}

void selects_filesystem_by_capacity() {
  assert(!ecu::fat32ForCapacity(0));
  assert(!ecu::fat32ForCapacity(2 * kGiB - 1));
  assert(!ecu::fat32ForCapacity(2 * kGiB));
  assert(ecu::fat32ForCapacity(2 * kGiB + 1));
  assert(ecu::fat32ForCapacity(32 * kGiB));
  assert(!ecu::fat32ForCapacity(32 * kGiB + 1));
  assert(!ecu::fat32ForCapacity(std::numeric_limits<uint64_t>::max()));
}

}  // namespace

int main() {
  accepts_only_explicit_complete_command();
  rejects_replay_even_after_format_failure();
  rejects_wrong_geometry_and_stale_boot_token();
  absence_and_removal_invalidate_the_boot();
  rejects_incomplete_or_non_decimal_input();
  respects_capacity_boundaries_without_integer_overflow();
  selects_filesystem_by_capacity();
  std::cout << "maintenance policy: 7 scenarios passed\n";
}
