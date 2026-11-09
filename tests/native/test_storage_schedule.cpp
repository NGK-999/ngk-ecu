#include "storage_schedule.hpp"
#include <cassert>
#include <iostream>
int main() {
    using ecu::shouldProbeStorage;
    assert(!shouldProbeStorage(true, false, 0, 0));
    assert(!shouldProbeStorage(true, false, 5000, 0));
    assert(!shouldProbeStorage(true, false, 900000, 0));
    assert(shouldProbeStorage(true, true, 1, 0));
    assert(!shouldProbeStorage(false, false, 4999, 0));
    assert(shouldProbeStorage(false, false, 5000, 0));
    assert(shouldProbeStorage(false, false, 5001, 0));
    assert(!shouldProbeStorage(false, false, 0x20, 0xfffffff0U));
    assert(shouldProbeStorage(false, false, 0x2000, 0xfffffff0U));
    std::cout << "storage schedule: stable healthy mount, explicit refresh and bounded retry PASS\n";
}
