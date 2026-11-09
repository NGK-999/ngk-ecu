#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ecu::radio::policy {

inline void erase(void* address, std::size_t length) {
    auto* byte = static_cast<volatile unsigned char*>(address);
    while (length--) *byte++ = 0;
}

inline std::size_t boundedLength(const char* value, std::size_t maximum) {
    if (!value) return maximum + 1;
    std::size_t length = 0;
    while (length <= maximum && value[length]) ++length;
    return length;
}

inline bool validSsid(const char* ssid) {
    const auto length = boundedLength(ssid, 32);
    return length > 0 && length <= 32;
}

struct PublicProfile {
    char ssid[33]{};
    bool configured = false;
    bool open = false;
};

// Private worker/pool data only. Never embed this type in a UI snapshot, SD
// profile, diagnostic or FreeRTOS queue (queue storage retains popped bytes).
struct Credentials {
    char ssid[33]{};
    char password[65]{};
    bool open = false;

    void clear() { erase(this, sizeof(*this)); }

    bool assign(const char* network, const char* secret, bool knownOpen = false) {
        clear();
        if (!validSsid(network) || !secret) return false;
        const auto length = boundedLength(secret, 64);
        if (length == 0) {
            if (!knownOpen) return false;
        } else if (length == 64) {
            for (std::size_t index = 0; index < length; ++index) {
                const auto byte = secret[index];
                if (!((byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'F') ||
                      (byte >= 'a' && byte <= 'f'))) return false;
            }
        } else {
            if (length < 8 || length > 63) return false;
            for (std::size_t index = 0; index < length; ++index) {
                const auto byte = static_cast<unsigned char>(secret[index]);
                if (byte < 32 || byte > 126) return false;
            }
        }
        std::memcpy(ssid, network, boundedLength(network, 32));
        std::memcpy(password, secret, length);
        open = length == 0;
        return true;
    }

    PublicProfile redacted() const {
        PublicProfile result;
        result.configured = validSsid(ssid);
        if (result.configured) std::memcpy(result.ssid, ssid, sizeof(result.ssid));
        result.open = result.configured && open;
        return result;
    }
};

enum class BleCommand : uint8_t { Pending, Rejected, Ping, Info };

class BleFramer {
public:
    void reset() {
        used_ = 0;
        invalid_ = false;
        cr_ = false;
        text_[0] = '\0';
    }

    BleCommand push(char character) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte == '\n') {
            const auto result = invalid_ ? BleCommand::Rejected :
                (std::strcmp(text_, "PING") == 0 ? BleCommand::Ping :
                 (std::strcmp(text_, "INFO") == 0 ? BleCommand::Info :
                  (used_ == 0 ? BleCommand::Pending : BleCommand::Rejected)));
            reset();
            return result;
        }
        if (invalid_) return BleCommand::Pending;
        if (byte == '\r' && !cr_) {
            cr_ = true;
            return BleCommand::Pending;
        }
        if (cr_ || byte < 32 || byte > 126 || used_ == sizeof(text_) - 1) {
            invalid_ = true;
            text_[0] = '\0';
            used_ = 0;
            return BleCommand::Pending;
        }
        text_[used_++] = character;
        text_[used_] = '\0';
        return BleCommand::Pending;
    }

private:
    char text_[17]{};
    std::size_t used_ = 0;
    bool invalid_ = false;
    bool cr_ = false;
};

inline bool bleAuthorized(bool encrypted, bool authenticated, uint8_t keySize) {
    return encrypted && authenticated && keySize == 16;
}

inline std::size_t fragmentLength(std::size_t remaining, uint16_t mtu) {
    if (mtu <= 3) return 0;
    const auto capacity = static_cast<std::size_t>(mtu - 3);
    return remaining < capacity ? remaining : capacity;
}

class Deadline {
public:
    void start(uint32_t now, uint32_t duration) {
        started_ = now;
        duration_ = duration;
        active_ = true;
    }
    void cancel() { active_ = false; }
    bool expired(uint32_t now) const { return active_ && uint32_t(now - started_) >= duration_; }
private:
    uint32_t started_ = 0;
    uint32_t duration_ = 0;
    bool active_ = false;
};

enum class Radio : uint8_t { Wifi, Ble };

inline bool memoryEnough(std::size_t freeBytes, std::size_t largestBlock, Radio radio) {
    // Conservative start guards, not a guarantee of allocation success. Runtime
    // failures remain visible. Keep UI/DMA reserve on this non-PSRAM board.
    const std::size_t reserve = radio == Radio::Wifi ? 80000 : 90000;
    return freeBytes >= reserve && largestBlock >= 32768;
}

}  // namespace ecu::radio::policy
