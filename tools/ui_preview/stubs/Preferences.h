#pragma once
#include <cstddef>
class Preferences { public:
 bool begin(const char*, bool) { return false; }
 std::size_t getBytesLength(const char*) { return 0; }
 std::size_t getBytes(const char*, void*, std::size_t) { return 0; }
 std::size_t putBytes(const char*, const void*, std::size_t) { return 0; }
 void end() {}
};
