#pragma once

#include <cstddef>
#include <cstdint>

namespace ecu {

// SD polynomial x^7 + x^3 + 1, initial value zero. The return value is the
// seven-bit CRC; command/register callers append the mandatory end bit.
inline uint8_t sdCrc7(const uint8_t* bytes, std::size_t length) noexcept {
  uint8_t crc = 0;
  for (std::size_t i = 0; i < length; ++i) {
    uint8_t value = bytes[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = static_cast<uint8_t>(crc << 1);
      if (((crc ^ value) & 0x80) != 0) {
        crc ^= 0x09;
      }
      value = static_cast<uint8_t>(value << 1);
    }
  }
  return crc & 0x7F;
}

// SD data CRC16: polynomial x^16 + x^12 + x^5 + 1, initial zero,
// non-reflected, no final XOR (CRC-16/XMODEM).
inline uint16_t sdCrc16(const uint8_t* bytes, std::size_t length) noexcept {
  uint16_t crc = 0;
  for (std::size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(uint16_t{bytes[i]} << 8);
    for (int bit = 0; bit < 8; ++bit) {
      crc = static_cast<uint16_t>((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
  }
  return crc;
}

// Validate exactly one SD CID register plus its SPI data-block CRC16.
// FNV-1a is an accidental-swap fingerprint, not cryptographic authentication:
// cloned CIDs/hash collisions cannot be distinguished. Keep identity in RAM;
// never log, transmit, or persist it or the CID. Output is cleared on failure.
inline bool cardIdentityFromCid(const uint8_t* cid, std::size_t length,
                                uint16_t transmittedCrc,
                                uint64_t& identity) noexcept {
  identity = 0;
  if (cid == nullptr || length != 16) {
    return false;
  }

  bool allZero = true;
  bool allOnes = true;
  for (std::size_t i = 0; i < length; ++i) {
    allZero = allZero && cid[i] == 0;
    allOnes = allOnes && cid[i] == 0xFF;
  }
  if (allZero || allOnes || sdCrc16(cid, length) != transmittedCrc ||
      (cid[15] & 1) != 1 || (cid[15] >> 1) != sdCrc7(cid, 15)) {
    return false;
  }

  // SD CID: reserved bits 23:20 are zero, MDT month is 1..12;
  // OID (two characters) and PNM (five characters) are printable ASCII.
  const uint8_t month = cid[14] & 0x0F;
  if ((cid[13] & 0xF0) != 0 || month == 0 || month > 12) {
    return false;
  }
  for (std::size_t i = 1; i <= 7; ++i) {
    if (cid[i] < 0x20 || cid[i] > 0x7E) {
      return false;
    }
  }

  uint64_t fingerprint = UINT64_C(14695981039346656037);
  for (std::size_t i = 0; i < length; ++i) {
    fingerprint ^= cid[i];
    fingerprint *= UINT64_C(1099511628211);
  }
  if (fingerprint == 0) {
    return false;  // Reserve zero for invalid/unavailable identity.
  }
  identity = fingerprint;
  return true;
}

}  // namespace ecu
