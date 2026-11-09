#include "radio_policy.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}
using namespace ecu::radio::policy;
BleCommand feed(BleFramer& parser, const std::string& input) {
    BleCommand result = BleCommand::Pending;
    for (const auto byte : input) result = parser.push(byte);
    return result;
}
}

int main() {
    Credentials credentials;
    check(credentials.assign("bench", "synthetic-pass"), "valid WPA credential accepted");
    check(!credentials.assign("", "synthetic-pass"), "empty SSID rejected");
    check(!credentials.assign(std::string(33, 's').c_str(), "synthetic-pass"),
          "overlong SSID cannot be truncated into another network");
    check(credentials.assign(std::string(32, 's').c_str(), "12345678"),
          "32 SSID bytes and 8 password bytes accepted");
    check(!credentials.assign("bench", "1234567"), "7-byte WPA password rejected");
    check(credentials.assign("bench", std::string(63, 'p').c_str()), "63-byte passphrase accepted");
    check(credentials.assign("bench", std::string(64, 'A').c_str()), "64 hexadecimal PSK accepted");
    check(!credentials.assign("bench", std::string(64, 'g').c_str()), "64 nonhex bytes rejected");
    check(!credentials.assign("bench", std::string(65, 'A').c_str()), "65-byte password rejected");
    check(!credentials.assign("bench", "1234\n678"), "control byte in password rejected");
    check(!credentials.assign("bench", ""), "empty password cannot silently select open security");
    check(credentials.assign("bench", "", true), "explicitly known open network accepted");
    check(!credentials.assign(nullptr, "12345678"), "null SSID rejected");
    check(!credentials.assign("bench", nullptr), "null password cannot become an open network");
    check(credentials.assign("bench", "synthetic-pass"), "credential prepared for redaction");
    const auto visible = credentials.redacted();
    check(visible.configured && std::strcmp(visible.ssid, "bench") == 0,
          "public profile identifies the locally entered network");
    const std::string publicBytes(reinterpret_cast<const char*>(&visible), sizeof(visible));
    check(publicBytes.find("synthetic-pass") == std::string::npos,
          "public profile does not carry the password");
    credentials.clear();
    const std::string cleared(reinterpret_cast<const char*>(&credentials), sizeof(credentials));
    check(cleared == std::string(sizeof(credentials), '\0'), "clearing erases every credential byte");
    credentials.assign("bench", "synthetic-pass");
    credentials.assign("", "invalid");
    check(credentials.password[0] == '\0' && !credentials.redacted().configured,
          "invalid replacement cannot retain a previous secret");

    BleFramer parser;
    check(feed(parser, "PI") == BleCommand::Pending, "BLE command waits across MTU fragments");
    check(feed(parser, "NG\n") == BleCommand::Ping, "fragmented PING accepted only after LF");
    check(feed(parser, "INFO\r\n") == BleCommand::Info, "INFO with CRLF accepted");
    check(feed(parser, "FORMAT\n") == BleCommand::Rejected, "FORMAT cannot reach any handler");
    check(feed(parser, "SET RPM 5000\n") == BleCommand::Rejected, "remote configuration rejected");
    check(feed(parser, std::string("PING\0INFO\n", 10)) == BleCommand::Rejected,
          "NUL cannot hide a BLE suffix");
    check(feed(parser, "PI\rNG\n") == BleCommand::Rejected, "internal CR cannot join a command");
    check(feed(parser, std::string(80, 'x') + "PING\n") == BleCommand::Rejected,
          "overflow discards complete line without executing its suffix");
    check(feed(parser, "PING\n") == BleCommand::Ping, "new line recovers from overflow");
    check(feed(parser, "PING ") == BleCommand::Pending, "partial command remains pending");
    parser.reset();
    check(feed(parser, "INFO\n") == BleCommand::Info, "new BLE session clears old fragments");
    check(!bleAuthorized(true, false, 16) && !bleAuthorized(false, true, 16) &&
              !bleAuthorized(true, true, 7) && bleAuthorized(true, true, 16),
          "BLE commands require encryption, MITM authentication and full-size keys");
    check(fragmentLength(50, 23) == 20 && fragmentLength(5, 23) == 5 &&
              fragmentLength(50, 3) == 0 && fragmentLength(50, 247) == 50,
          "notifications respect negotiated ATT payload capacity");

    Deadline deadline;
    check(!deadline.expired(100000), "inactive scan deadline is not an immediate timeout");
    deadline.start(100, 12000);
    check(!deadline.expired(12099) && deadline.expired(12100), "scan expires at its bounded deadline");
    deadline.start(0xfffffff0U, 40);
    check(!deadline.expired(0x17U) && deadline.expired(0x18U), "deadline survives millis wraparound");
    deadline.cancel();
    check(!deadline.expired(100000), "cancelled scan cannot expire later");
    check(!memoryEnough(100000, 1000, Radio::Wifi) &&
              !memoryEnough(20000, 20000, Radio::Ble) &&
              memoryEnough(150000, 80000, Radio::Wifi) &&
              memoryEnough(150000, 80000, Radio::Ble),
          "radio start preserves free-heap and contiguous-allocation reserve");
    if (failures) return EXIT_FAILURE;
    std::puts("radio policy: bounded credentials, redaction, BLE framing/auth and timeouts PASS");
    return EXIT_SUCCESS;
}
