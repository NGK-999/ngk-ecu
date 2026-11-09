#include "card_identity.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

namespace {

using Cid = std::array<uint8_t, 16>;

void seal(Cid& cid) {
  cid[15] = static_cast<uint8_t>((ecu::sdCrc7(cid.data(), 15) << 1) | 1);
}

Cid syntheticCid() {
  // Synthetic metadata only: never captured from a physical card.
  Cid cid = {0x7F, 'E', 'C', 'U', 'T', 'E', 'S', 'T',
             0x10, 0x01, 0x02, 0x03, 0x04, 0x01, 0xA5, 0};
  seal(cid);
  return cid;
}

bool parse(const Cid& cid, uint64_t& identity) {
  return ecu::cardIdentityFromCid(cid.data(), cid.size(),
                                   ecu::sdCrc16(cid.data(), cid.size()), identity);
}

void matches_independent_crc_vectors() {
  constexpr uint8_t cmd0[] = {0x40, 0, 0, 0, 0};
  constexpr uint8_t cmd8[] = {0x48, 0, 0, 1, 0xAA};
  constexpr uint8_t cmd17[] = {0x51, 0, 0, 0, 0};
  constexpr uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  assert(((ecu::sdCrc7(cmd0, sizeof(cmd0)) << 1) | 1) == 0x95);
  assert(((ecu::sdCrc7(cmd8, sizeof(cmd8)) << 1) | 1) == 0x87);
  assert(((ecu::sdCrc7(cmd17, sizeof(cmd17)) << 1) | 1) == 0x55);
  assert(ecu::sdCrc16(check, sizeof(check)) == 0x31C3);
  std::array<uint8_t, 512> allOnes{};
  allOnes.fill(0xFF);
  assert(ecu::sdCrc16(allOnes.data(), allOnes.size()) == 0x7FA1);
  assert(ecu::sdCrc7(nullptr, 0) == 0);
  assert(ecu::sdCrc16(nullptr, 0) == 0);
}

void identity_is_stable_and_changes_for_a_different_card() {
  const Cid first = syntheticCid();
  uint64_t firstIdentity = 0;
  uint64_t repeatedIdentity = 0;
  assert(parse(first, firstIdentity));
  assert(firstIdentity != 0);
  assert(parse(first, repeatedIdentity));
  assert(firstIdentity == repeatedIdentity);

  Cid second = first;
  second[12] ^= 1;  // A different synthetic serial number, same capacity.
  seal(second);
  uint64_t secondIdentity = 0;
  assert(parse(second, secondIdentity));
  assert(secondIdentity != firstIdentity);
}

void rejects_invalid_buffer_and_erases_stale_output() {
  const Cid cid = syntheticCid();
  const uint16_t wireCrc = ecu::sdCrc16(cid.data(), cid.size());
  uint64_t identity = 123;
  assert(!ecu::cardIdentityFromCid(nullptr, 16, wireCrc, identity));
  assert(identity == 0);
  for (const std::size_t length : {std::size_t{0}, std::size_t{15},
                                 std::size_t{17}}) {
    identity = 123;
    assert(!ecu::cardIdentityFromCid(cid.data(), length, wireCrc, identity));
    assert(identity == 0);
  }
}

void rejects_empty_or_disconnected_card_metadata() {
  for (const uint8_t fill : {uint8_t{0}, uint8_t{0xFF}}) {
    Cid cid{};
    cid.fill(fill);
    uint64_t identity = 123;
    assert(!parse(cid, identity));
    assert(identity == 0);
  }
}

void rejects_transfer_crc_and_register_crc_errors() {
  Cid cid = syntheticCid();
  uint64_t identity = 123;
  assert(!ecu::cardIdentityFromCid(
      cid.data(), cid.size(), ecu::sdCrc16(cid.data(), cid.size()) ^ 1, identity));
  assert(identity == 0);

  cid[12] ^= 1;
  identity = 123;
  assert(!parse(cid, identity));  // Valid wire CRC, invalid CID's internal CRC7.
  assert(identity == 0);
}

void rejects_malformed_register_fields_even_with_valid_crcs() {
  const Cid original = syntheticCid();
  Cid endBit = original;
  endBit[15] &= 0xFE;
  uint64_t identity = 123;
  assert(!parse(endBit, identity));
  assert(identity == 0);

  Cid reserved = original;
  reserved[13] |= 0x10;
  seal(reserved);
  assert(!parse(reserved, identity));

  for (const uint8_t month : {uint8_t{0}, uint8_t{13}, uint8_t{15}}) {
    Cid cid = original;
    cid[14] = static_cast<uint8_t>((cid[14] & 0xF0) | month);
    seal(cid);
    assert(!parse(cid, identity));
  }

  Cid invalidAscii = original;
  invalidAscii[3] = 0x01;
  seal(invalidAscii);
  assert(!parse(invalidAscii, identity));
}

}  // namespace

int main() {
  matches_independent_crc_vectors();
  identity_is_stable_and_changes_for_a_different_card();
  rejects_invalid_buffer_and_erases_stale_output();
  rejects_empty_or_disconnected_card_metadata();
  rejects_transfer_crc_and_register_crc_errors();
  rejects_malformed_register_fields_even_with_valid_crcs();
  std::cout << "card identity: 6 scenarios passed\n";
}
