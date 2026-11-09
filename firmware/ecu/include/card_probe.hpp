#pragma once

#include "card_identity.hpp"

#include <Arduino.h>
#include <SPI.h>

namespace ecu {
namespace card_probe_detail {

class Transaction final {
 public:
  Transaction(SPIClass& spi, int chipSelect) : spi_(spi), chipSelect_(chipSelect) {
    spi_.beginTransaction(SPISettings(1'000'000, MSBFIRST, SPI_MODE0));
    digitalWrite(chipSelect_, LOW);
  }
  ~Transaction() {
    digitalWrite(chipSelect_, HIGH);
    spi_.transfer(0xFF);  // Release MISO while deselected before unlocking SPI.
    spi_.endTransaction();
  }
  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;

 private:
  SPIClass& spi_;
  int chipSelect_;
};

// Bound both elapsed time and traffic, even if the timer stops advancing.
inline bool ready(SPIClass& spi) {
  const uint32_t started = millis();
  for (uint32_t count = 0; count < 16'384; ++count) {
    if (spi.transfer(0xFF) == 0xFF) {
      return true;
    }
    if (static_cast<uint32_t>(millis() - started) >= 100) {
      break;
    }
  }
  return false;
}

inline bool dataToken(SPIClass& spi) {
  const uint32_t started = millis();
  for (uint32_t count = 0; count < 32'768; ++count) {
    const uint8_t token = spi.transfer(0xFF);
    if (token != 0xFF) {
      return token == 0xFE;
    }
    if (static_cast<uint32_t>(millis() - started) >= 250) {
      break;
    }
  }
  return false;
}

}  // namespace card_probe_detail

// Preconditions: sdcard_init + disk_initialize already succeeded on this SPI
// object, CS5 is configured as output, and one storage worker owns ALL card
// access. No nested transaction or concurrent SD/disk operation is permitted.
// This sends only CMD10: no sector/file access, writes, reset, or retries.
// The caller must recheck identity immediately before physical formatting;
// software cannot prevent a physical swap after this transaction completes.
inline bool readCardIdentity(SPIClass& spi, int chipSelect, uint64_t& identity) {
  identity = 0;
  if (chipSelect != 5) {
    return false;
  }

  card_probe_detail::Transaction transaction(spi, chipSelect);
  if (!card_probe_detail::ready(spi)) {
    return false;
  }

  uint8_t command[] = {0x40 | 10, 0, 0, 0, 0, 0};
  command[5] = static_cast<uint8_t>((sdCrc7(command, 5) << 1) | 1);
  for (const uint8_t byte : command) {
    spi.transfer(byte);
  }

  uint8_t response = 0xFF;
  for (int count = 0; count < 9; ++count) {
    response = spi.transfer(0xFF);
    if ((response & 0x80) == 0) {
      break;
    }
  }
  if (response != 0 || !card_probe_detail::dataToken(spi)) {
    return false;
  }

  uint8_t cid[16] = {};
  for (uint8_t& byte : cid) {
    byte = spi.transfer(0xFF);
  }
  const uint16_t crcHigh = spi.transfer(0xFF);
  const uint16_t crcLow = spi.transfer(0xFF);
  return cardIdentityFromCid(cid, sizeof(cid),
                             static_cast<uint16_t>((crcHigh << 8) | crcLow),
                             identity);
}

}  // namespace ecu
