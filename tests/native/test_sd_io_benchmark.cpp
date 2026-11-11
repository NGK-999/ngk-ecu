#include "sd_io_benchmark.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {
constexpr uint32_t kSize = 1024U * 1024U;

struct MemoryFile {
    std::vector<uint8_t> data = std::vector<uint8_t>(kSize);
    uint64_t clock = 0;
    uint32_t position = 0;
    uint32_t writes = 0;
    uint32_t reads = 0;
    uint32_t syncs = 0;
    uint32_t seeks = 0;
    uint32_t closes = 0;
    uint32_t removals = 0;
    uint32_t started = 0;
    uint32_t finished = 0;
    uint32_t verifyReads = 0;
    uint32_t firstPhasePosition = kSize + 1;
    uint32_t phaseWrites = 0;
    uint32_t phaseReads = 0;
    uint32_t phaseSyncs = 0;
    bool existing = false;
    bool owned = false;
    bool measuring = false;
    bool corruptRead = false;
    bool skipPhaseWrite = false;
    bool shortWrite = false;
    bool shortRead = false;
    bool failSeek = false;
    bool failSync = false;
    bool failPhaseSync = false;
    bool failClose = false;
    bool failRemove = false;
    bool failCreate = false;
    bool rewindOnStart = false;
    uint64_t writeCostUs = 250;
    uint64_t readCostUs = 250;
    uint64_t syncCostUs = 250;
    uint64_t yieldCostUs = 1000;
    uint64_t closeCostUs = 100;
    uint8_t setupFirstByte = 0;
    uint8_t phaseFirstByte = 0;

    bool createExclusive() {
        clock += 100;
        if (existing || failCreate) return false;
        existing = true;
        owned = true;
        position = 0;
        return true;
    }
    size_t write(const uint8_t* src, size_t count) {
        clock += writeCostUs;
        ++writes;
        if (measuring) {
            ++phaseWrites;
            if (firstPhasePosition > kSize) firstPhasePosition = position;
            if (phaseWrites == 1) phaseFirstByte = src[0];
        } else if (writes == 1) {
            setupFirstByte = src[0];
        }
        if (shortWrite) return count - 1;
        if (skipPhaseWrite && measuring) {
            position += static_cast<uint32_t>(count);
            return count;
        }
        assert(position + count <= data.size());
        for (size_t i = 0; i < count; ++i) data[position + i] = src[i];
        position += static_cast<uint32_t>(count);
        return count;
    }
    size_t read(uint8_t* dest, size_t count) {
        clock += readCostUs;
        ++reads;
        if (measuring) {
            ++phaseReads;
            if (firstPhasePosition > kSize) firstPhasePosition = position;
        } else if (finished) ++verifyReads;
        if (shortRead) return count - 1;
        assert(position + count <= data.size());
        for (size_t i = 0; i < count; ++i) dest[i] = data[position + i];
        if (corruptRead) dest[0] ^= 1;
        position += static_cast<uint32_t>(count);
        return count;
    }
    bool seek(uint32_t offset) {
        clock += 100;
        ++seeks;
        if (failSeek) return false;
        position = offset;
        return true;
    }
    bool sync() {
        clock += syncCostUs;
        ++syncs;
        if (measuring) ++phaseSyncs;
        return !failSync && !(failPhaseSync && measuring);
    }
    bool close() {
        clock += closeCostUs;
        ++closes;
        return !failClose;
    }
    bool removeOwn() {
        clock += 100;
        ++removals;
        if (failRemove || !owned) return false;
        existing = false;
        owned = false;
        return true;
    }
    uint64_t nowUs() { return clock; }
    void yield() { clock += yieldCostUs; }
    void measurementStarted() {
        ++started;
        measuring = true;
        if (rewindOnStart) clock = 0;
        phaseWrites = 0;
        phaseReads = 0;
        phaseSyncs = 0;
        firstPhasePosition = kSize + 1;
    }
    void measurementFinished() {
        ++finished;
        measuring = false;
    }
};

void completedModes() {
    using namespace ecu::sdbench;
    for (Mode mode : {Mode::Write512, Mode::Write4096, Mode::Batch4096, Mode::Read4096}) {
        MemoryFile file;
        const Result result = run(file, mode, 5);
        assert(result.okay && result.created && result.removed);
        assert(result.error == static_cast<uint32_t>(Error::None));
        assert(file.started == 1 && file.finished == 1 && file.closes == 1 && file.removals == 1);
        assert(!file.existing && result.setupMs > 0 && result.durationMs >= 5);
        assert(file.writes >= 256 && file.syncs >= 1 && file.seeks >= 1);
        assert(file.firstPhasePosition == 0);
        assert(result.write.operations == file.phaseWrites);
        assert(result.read.operations == file.phaseReads);
        assert(result.sync.operations == file.phaseSyncs);
        assert(file.verifyReads == 256);
        if (mode == Mode::Read4096) {
            assert(result.read.operations > 0 && result.write.operations == 0);
            assert(result.sync.operations == 0);
        } else {
            assert(result.write.operations > 0 && result.read.operations == 0);
            assert(result.write.bytes == result.write.operations *
                       (mode == Mode::Write512 ? 512U : 4096U));
            assert(result.sync.operations > 0);
            assert(file.setupFirstByte != file.phaseFirstByte);
        }
        assert(result.write.p95UpperUs >= result.write.maxUs);
    }
}

void invalidAndExistingAreUntouched() {
    using namespace ecu::sdbench;
    MemoryFile invalid;
    const Result bad = run(invalid, static_cast<Mode>(99), 5);
    assert(!bad.okay && !bad.created && !bad.removed);
    assert(bad.error == static_cast<uint32_t>(Error::InvalidMode));
    assert(invalid.writes == 0 && invalid.removals == 0 && invalid.started == 0);

    MemoryFile existing;
    existing.existing = true;
    existing.data[0] = 0xA5;
    const Result refused = run(existing, Mode::Write512, 5);
    assert(!refused.okay && !refused.created && !refused.removed);
    assert(refused.error == static_cast<uint32_t>(Error::Create));
    assert(existing.data[0] == 0xA5 && existing.removals == 0 && existing.closes == 0);
}

void failuresCleanOwnFile() {
    using namespace ecu::sdbench;
    for (int fault = 0; fault < 7; ++fault) {
        MemoryFile file;
        if (fault == 0) file.shortWrite = true;
        if (fault == 1) file.failSync = true;
        if (fault == 2) file.failSeek = true;
        if (fault == 3) file.shortRead = true;
        if (fault == 4) file.corruptRead = true;
        if (fault == 5) file.failClose = true;
        if (fault == 6) file.failRemove = true;
        const Mode mode = fault == 3 || fault == 4 ? Mode::Read4096 : Mode::Write4096;
        const Result result = run(file, mode, 5);
        assert(!result.okay && result.created);
        assert(file.closes == 1 && file.removals == 1);
        if (file.started) assert(file.finished == 1);
        assert(result.removed == (fault != 6));
        assert(result.error != static_cast<uint32_t>(Error::None));
    }
}

void globalDeadlineIncludesSetup() {
    using namespace ecu::sdbench;
    MemoryFile file;
    file.writeCostUs = 61'000'000;
    const Result result = run(file, Mode::Read4096, 5);
    assert(!result.okay && result.error == static_cast<uint32_t>(Error::Timeout));
    assert(result.created && result.removed && file.started == 0);
    assert(file.writes == 1 && file.closes == 1 && file.removals == 1);
}

void finalReadbackDetectsAcknowledgedMissingWrite() {
    using namespace ecu::sdbench;
    MemoryFile file;
    file.skipPhaseWrite = true;
    const Result result = run(file, Mode::Write4096, 5);
    assert(!result.okay && result.error == static_cast<uint32_t>(Error::Corrupt));
    assert(file.finished == 1 && file.verifyReads >= 1);
    assert(result.read.operations == 0 && result.durationMs >= 5);
    assert(result.removed);
}

void phaseNeverWritesPastOneMiB() {
    using namespace ecu::sdbench;
    MemoryFile file;
    file.yieldCostUs = 1000;
    const Result result = run(file, Mode::Write4096, 1200);
    assert(result.okay);
    assert(file.phaseWrites > 256 && file.seeks > 1);
    assert(result.write.bytes == static_cast<uint64_t>(file.phaseWrites) * 4096U);
}

void cleanupOverrunIsReported() {
    using namespace ecu::sdbench;
    MemoryFile file;
    file.closeCostUs = 61'000'000;
    const Result result = run(file, Mode::Write4096, 5);
    assert(!result.okay && result.error == static_cast<uint32_t>(Error::Timeout));
    assert(file.finished == 1 && result.removed);
    assert(result.durationMs < 60'000);
}

void clockRegressionIsReportedWithoutWrappedDurations() {
    using namespace ecu::sdbench;
    MemoryFile file;
    file.rewindOnStart = true;
    file.clock = 10'000'000;
    const Result result = run(file, Mode::Write4096, 5);
    assert(!result.okay && result.error == static_cast<uint32_t>(Error::Clock));
    assert(result.created && result.removed && file.finished == 1);
    assert(result.setupMs < 60'000 && result.durationMs < 60'000);
}

void phaseSyncFailureStillFinishesMeasurement() {
    using namespace ecu::sdbench;
    MemoryFile file;
    file.failPhaseSync = true;
    const Result result = run(file, Mode::Write4096, 5);
    assert(!result.okay && result.error == static_cast<uint32_t>(Error::Sync));
    assert(file.started == 1 && file.finished == 1 && result.removed);
    assert(result.sync.operations == 1 && file.verifyReads == 0);
}

void verificationUsesGlobalDeadline() {
    using namespace ecu::sdbench;
    MemoryFile file;
    file.readCostUs = 61'000'000;
    const Result result = run(file, Mode::Write4096, 5);
    assert(!result.okay && result.error == static_cast<uint32_t>(Error::Timeout));
    assert(file.finished == 1 && file.verifyReads == 1 && result.removed);
    assert(result.durationMs < 60'000 && result.read.operations == 0);
}
}  // namespace

int main() {
    completedModes();
    invalidAndExistingAreUntouched();
    failuresCleanOwnFile();
    globalDeadlineIncludesSetup();
    finalReadbackDetectsAcknowledgedMissingWrite();
    phaseNeverWritesPastOneMiB();
    cleanupOverrunIsReported();
    clockRegressionIsReportedWithoutWrappedDurations();
    phaseSyncFailureStillFinishesMeasurement();
    verificationUsesGlobalDeadline();
    std::cout << "sd io benchmark: PASS\n";
}
