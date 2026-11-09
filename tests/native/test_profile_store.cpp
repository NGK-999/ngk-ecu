#include "profile_store.hpp"
#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <iostream>

struct MemoryFiles {
    std::map<std::string, std::string> files;
    bool failWrite = false, failCommit = false, failPrimaryRead = false;
    bool directory() { return true; }
    bool exists(const char* path) { return files.count(path); }
    bool remove(const char* path) { return files.erase(path) != 0; }
    bool rename(const char* from, const char* to) {
        if (failCommit && std::string(from) == ecu::profiles::temporaryPath) return false;
        const auto found = files.find(from);
        if (found == files.end() || exists(to)) return false;
        files[to] = found->second; files.erase(found); return true;
    }
    bool write(const char* path, const char* bytes, std::size_t size) {
        files[path] = std::string(bytes, failWrite ? size / 2 : size);
        return !failWrite;
    }
    bool read(const char* path, char* bytes, std::size_t capacity, std::size_t& size) {
        if (failPrimaryRead && std::string(path) == ecu::profiles::primaryPath) return false;
        const auto found = files.find(path);
        if (found == files.end() || found->second.size() > capacity) return false;
        size = found->second.size(); std::memcpy(bytes, found->second.data(), size); return true;
    }
};

int main() {
    MemoryFiles files;
    ecu::UiSettings first, second; second.brightness = 90;
    ecu::UiSettings output; output.brightness = 30;
    bool recovered = false;
    assert(!ecu::profiles::loadProfile(files, output, recovered));
    assert(output.brightness == 30);
    assert(ecu::profiles::saveProfile(files, first));
    assert(ecu::profiles::loadProfile(files, output, recovered));
    assert(output == first && !recovered);
    assert(ecu::profiles::saveProfile(files, second));
    assert(ecu::profiles::loadProfile(files, output, recovered) && output == second);
    files.files[ecu::profiles::primaryPath][0] = '!';
    assert(ecu::profiles::loadProfile(files, output, recovered));
    assert(output == first && recovered);

    MemoryFiles torn;
    assert(ecu::profiles::saveProfile(torn, first));
    torn.failWrite = true;
    assert(!ecu::profiles::saveProfile(torn, second));
    assert(ecu::profiles::loadProfile(torn, output, recovered) && output == first);

    MemoryFiles renameFailure;
    assert(ecu::profiles::saveProfile(renameFailure, first));
    renameFailure.failCommit = true;
    assert(!ecu::profiles::saveProfile(renameFailure, second));
    assert(ecu::profiles::loadProfile(renameFailure, output, recovered) && output == first);

    MemoryFiles bad;
    bad.files[ecu::profiles::primaryPath] = std::string(513, 'x');
    output.brightness = 30;
    assert(!ecu::profiles::loadProfile(bad, output, recovered) && output.brightness == 30);
    auto invalid = first; invalid.rpmScale = 1;
    assert(!ecu::profiles::saveProfile(bad, invalid));
    assert(bad.files[ecu::profiles::primaryPath].size() == 513);
    MemoryFiles readFailure;
    assert(ecu::profiles::saveProfile(readFailure, first));
    const auto preserved = readFailure.files[ecu::profiles::primaryPath];
    readFailure.failPrimaryRead = true;
    assert(!ecu::profiles::saveProfile(readFailure, second));
    assert(readFailure.files[ecu::profiles::primaryPath] == preserved);
    readFailure.failPrimaryRead = false;
    assert(ecu::profiles::loadProfile(readFailure, output, recovered) && output == first);
    std::cout << "profile store: round trip, recovery and failed-write preservation PASS\n";
}
