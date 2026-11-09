#pragma once
#include "config_codec.hpp"
#include <array>

namespace ecu::profiles {
inline constexpr const char* primaryPath = "/ngk-ecu/ui-profile.cfg";
inline constexpr const char* temporaryPath = "/ngk-ecu/ui-profile.new";
inline constexpr const char* backupPath = "/ngk-ecu/ui-profile.bak";
enum class ProfileRead { Okay, ReadError, Invalid };

template<class Files> ProfileRead inspectProfile(Files& files, const char* path, UiSettings& output) {
    std::array<char, kMaxSettingsEncodedSize + 1> bytes{};
    std::size_t size = 0;
    UiSettings candidate;
    if (!files.read(path, bytes.data(), kMaxSettingsEncodedSize, size)) return ProfileRead::ReadError;
    if (!decodeSettings(bytes.data(), size, candidate)) return ProfileRead::Invalid;
    output = candidate;
    return ProfileRead::Okay;
}
template<class Files> bool readProfile(Files& files, const char* path, UiSettings& output) {
    return inspectProfile(files, path, output) == ProfileRead::Okay;
}

template<class Files> bool loadProfile(Files& files, UiSettings& output, bool& recovered) {
    UiSettings candidate;
    if (readProfile(files, primaryPath, candidate)) {
        output = candidate; recovered = false; return true;
    }
    if (readProfile(files, backupPath, candidate)) {
        output = candidate; recovered = true; return true;
    }
    return false;
}

template<class Files> bool saveProfile(Files& files, const UiSettings& settings) {
    std::array<char, kMaxSettingsEncodedSize + 1> bytes{};
    const auto size = encodeSettings(settings, bytes.data(), bytes.size());
    if (!size || !files.directory() || !files.write(temporaryPath, bytes.data(), size)) return false;
    UiSettings verified;
    if (!readProfile(files, temporaryPath, verified) || !(verified == settings)) return false;
    if (files.exists(primaryPath)) {
        UiSettings previous;
        const auto read = inspectProfile(files, primaryPath, previous);
        if (read == ProfileRead::ReadError) return false;
        if (read == ProfileRead::Okay) {
            if (files.exists(backupPath) && !files.remove(backupPath)) return false;
            if (!files.rename(primaryPath, backupPath)) return false;
        } else if (!files.remove(primaryPath)) return false;
    }
    if (!files.rename(temporaryPath, primaryPath)) {
        if (!files.exists(primaryPath) && files.exists(backupPath)) files.rename(backupPath, primaryPath);
        return false;
    }
    return true;
}
}
