#include "NewPathFixture.hpp"
#include "VulkanTestDevice.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32

namespace {

using namespace NewPath;

constexpr std::uint32_t Lanes = 16;
constexpr std::uint32_t SlotBytes = Lanes * 4u;
constexpr std::uint32_t Untouched = 0xdeadbeefu;
constexpr std::uint64_t WriteOffset = 0x1000;
constexpr std::uint32_t OutputSlots = static_cast<std::uint32_t>(PageBytes / SlotBytes);
constexpr std::uint32_t Rebuilds = 8;
constexpr std::uint32_t RaceSubmissions = 100;
constexpr std::uint32_t RaceDispatches = 4;
constexpr std::uint32_t RaceSlotBytes = 2u * SlotBytes;
constexpr auto FreeLimit = std::chrono::seconds(10);
constexpr auto PromptFree = std::chrono::seconds(2);

int failures = 0;

void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

const Program& CopyProgram() {
    static const Program program = [] {
        using namespace Isa;
        Program built;
        built << Vop2(VLshlRevB32, 1, InlineInteger(2), 0)
              << Global(GlobalLoadDword, 2, 1, 0, 0)
              << WaitAll
              << Vop2(VAddNcU32, 2, InlineInteger(1), 2)
              << Global(GlobalStoreDword, 0, 1, 2, 2)
              << EndProgram;
        return built;
    }();
    return program;
}

const Program& TwoReadsProgram() {
    static const Program program = [] {
        using namespace Isa;
        Program built;
        built << Vop2(VLshlRevB32, 1, InlineInteger(2), 0)
              << Global(GlobalLoadDword, 2, 1, 0, 0)
              << WaitAll
              << Global(GlobalStoreDword, 0, 1, 2, 4)
              << Global(GlobalLoadDword, 3, 1, 0, 2)
              << WaitAll
              << Global(GlobalStoreDword, 0, 1, 3, 4, SlotBytes)
              << EndProgram;
        return built;
    }();
    return program;
}

class GuestBlock {
public:
    explicit GuestBlock(std::uint32_t units) : bytes(units * PageBytes), lengths(units, 0u) {
        base = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(bytes, PageBytes));
        Require(base != nullptr, "address space mappings: cannot reserve guest pages");
        GuestArena::GuestArenaCommit_nid_postfix(base, bytes, PAGE_READWRITE, 16384);
        std::memset(base, 0, bytes);
    }

    ~GuestBlock() {
        for (std::uint32_t unit = 0; unit < lengths.size(); ++unit) {
            if (lengths[unit] == 0u) continue;
            try {
                GuestAllocations::Mutation().Remove(base + unit * PageBytes);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: address space mappings: a range could not be removed: %s\n", error.what());
            }
        }
        GuestArena::GuestArenaReset_nid_postfix(base, bytes);
        GuestArena::GuestArenaRelease_nid_postfix(base, bytes);
    }

    GuestBlock(const GuestBlock&) = delete;
    GuestBlock& operator=(const GuestBlock&) = delete;

    void Map(std::uint32_t unit, std::uint32_t units, bool writable = true) {
        GuestAllocations::Mutation().Add(base + unit * PageBytes, units * PageBytes, true, writable, true);
        lengths[unit] = units;
    }

    void Unmap(std::uint32_t unit) {
        GuestAllocations::Mutation().Remove(base + unit * PageBytes);
        lengths[unit] = 0u;
    }

    void Unmapped(std::uint32_t unit) { lengths[unit] = 0u; }

    void Protect(std::uint32_t unit, bool writable) {
        GuestAllocations::Mutation().Protect(base + unit * PageBytes, lengths[unit] * PageBytes, true, writable, true, [] {});
    }

    void* Base(std::uint32_t unit = 0) const { return base + unit * PageBytes; }
    std::uint64_t Address(std::uint32_t unit = 0, std::uint64_t offset = 0) const { return reinterpret_cast<std::uint64_t>(base) + unit * PageBytes + offset; }
    volatile std::uint32_t* Words(std::uint32_t unit = 0, std::uint64_t offset = 0) const { return reinterpret_cast<volatile std::uint32_t*>(base + unit * PageBytes + offset); }

private:
    std::uint8_t* base = nullptr;
    std::uint64_t bytes = 0;
    std::vector<std::uint32_t> lengths;
};

std::size_t Count(const std::string& text, const std::string& what) {
    std::size_t count = 0;
    for (auto at = text.find(what); at != std::string::npos; at = text.find(what, at + 1u)) ++count;
    return count;
}

std::string LineWith(const std::string& text, const std::string& what) {
    const auto at = text.find(what);
    if (at == std::string::npos) return {};
    const auto begin = text.rfind('\n', at);
    const auto first = begin == std::string::npos ? 0u : begin + 1u;
    return text.substr(first, text.find('\n', at) - first);
}

std::uint64_t ReportedAddress(const std::string& report) {
    const auto at = report.find("address=0x");
    return at != std::string::npos ? std::strtoull(report.c_str() + at + 10, nullptr, 16) : 0u;
}

struct Outcome {
    std::string failure;
    std::string log;
};

Outcome Submitted(Commands& commands) {
    Outcome outcome;
    StderrCapture capture;
    try {
        commands.Submit(0);
        WaitIdle();
        Settle();
    } catch (const std::exception& error) {
        outcome.failure = error.what();
    }
    outcome.log = capture.Finish();
    return outcome;
}

void ExpectClean(const Outcome& outcome, const std::string& what) {
    Expect(outcome.failure.empty() && Count(outcome.log, "BDA access failed") == 0u && Count(outcome.log, "deferred write-back failed") == 0u, what + ": the dispatch reported a failure: " + outcome.failure + "\n" + outcome.log);
}

void ExpectFault(const Outcome& outcome, std::uint32_t reason, std::uint64_t begin, const std::string& what) {
    Expect(outcome.failure.empty(), what + ": the fault stopped the driver: " + outcome.failure);
    Expect(Count(outcome.log, "BDA access failed") == 1u, what + ": the access was not reported exactly once as a fault:\n" + outcome.log);
    const auto report = LineWith(outcome.log, "BDA access failed");
    Expect(report.find("reason=" + std::to_string(reason)) != std::string::npos, what + ": the report does not give reason " + std::to_string(reason) + ": " + report);
    const auto address = ReportedAddress(report);
    Expect(address >= begin && address < begin + SlotBytes, what + ": the report names " + Hex(address) + ", not an address the dispatch accessed at " + Hex(begin) + ": " + report);
}

std::array<std::uint32_t, 4> Pointers(std::uint64_t source, std::uint64_t destination) {
    return {Low(source), High(source), Low(destination), High(destination)};
}

std::uint32_t SourceWord(std::uint32_t lane) {
    return 0x51000000u | lane;
}

std::uint32_t RangeWord(std::uint32_t unit, std::uint32_t lane) {
    return 0x71000000u | (unit << 12u) | lane;
}

class Stable {
public:
    Stable() : block(2) {
        block.Map(0, 2);
        for (std::uint32_t lane = 0; lane < Lanes; ++lane) block.Words()[lane] = SourceWord(lane);
    }

    std::uint64_t Source() const { return block.Address(); }

    std::uint64_t TakeOutput() {
        Require(next < OutputSlots, "address space mappings: more reads than output slots");
        const auto address = block.Address(1, std::uint64_t{next++} * SlotBytes);
        for (std::uint32_t lane = 0; lane < Lanes; ++lane) Words(address)[lane] = Untouched;
        return address;
    }

    static volatile std::uint32_t* Words(std::uint64_t address) { return reinterpret_cast<volatile std::uint32_t*>(static_cast<std::uintptr_t>(address)); }

private:
    GuestBlock block;
    std::uint32_t next = 0;
};

void ExpectRead(Stable& stable, std::uint64_t source, std::uint32_t unit, bool reachable, const std::string& what) {
    const auto output = stable.TakeOutput();
    Commands commands;
    commands.Dispatch(CopyProgram(), Pointers(source, output), Lanes);
    const auto outcome = Submitted(commands);
    AgcDriver::GuestMemory::FlushGpuWrites(output, SlotBytes);
    std::size_t copied = 0;
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) copied += Stable::Words(output)[lane] == RangeWord(unit, lane) + 1u ? 1u : 0u;
    if (reachable) {
        ExpectClean(outcome, what);
        Expect(copied == Lanes, what + ": " + std::to_string(Lanes - copied) + " of " + std::to_string(Lanes) + " lanes did not read the mapped range");
        return;
    }
    ExpectFault(outcome, 1, source, what);
    Expect(copied == 0u, what + ": " + std::to_string(copied) + " lanes still read the contents of the range that is no longer mapped");
}

enum class Store { Lands, Unmapped, ReadOnly };

void ExpectWrite(Stable& stable, std::uint64_t target, Store store, const std::string& what) {
    auto* words = Stable::Words(target);
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) words[lane] = Untouched;
    Commands commands;
    commands.Dispatch(CopyProgram(), Pointers(stable.Source(), target), Lanes);
    const auto outcome = Submitted(commands);
    if (store != Store::Unmapped) AgcDriver::GuestMemory::FlushGpuWrites(target, SlotBytes);
    std::size_t landed = 0;
    std::size_t untouched = 0;
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
        landed += words[lane] == SourceWord(lane) + 1u ? 1u : 0u;
        untouched += words[lane] == Untouched ? 1u : 0u;
    }
    if (store == Store::Lands) {
        ExpectClean(outcome, what);
        Expect(landed == Lanes, what + ": " + std::to_string(Lanes - landed) + " of " + std::to_string(Lanes) + " stores did not land in the mapped range");
        return;
    }
    ExpectFault(outcome, store == Store::Unmapped ? 1u : 2u, target, what);
    Expect(untouched == Lanes, what + ": " + std::to_string(Lanes - untouched) + " stores landed in a range the guest " + (store == Store::Unmapped ? "no longer maps" : "maps read-only"));
}

void MappingChanges(Stable& stable) {
    GuestBlock range(2);
    for (std::uint32_t unit = 0; unit < 2u; ++unit) {
        for (std::uint32_t lane = 0; lane < Lanes; ++lane) range.Words(unit)[lane] = RangeWord(unit, lane);
    }
    const auto first = range.Address(0);
    const auto second = range.Address(1);
    range.Map(0, 1);
    ExpectRead(stable, first, 0, true, "a newly mapped range, read");
    ExpectWrite(stable, first + WriteOffset, Store::Lands, "a newly mapped range, written");

    range.Unmap(0);
    ExpectRead(stable, first, 0, false, "the range after the guest unmapped it, read");
    ExpectWrite(stable, first + WriteOffset, Store::Unmapped, "the range after the guest unmapped it, written");

    range.Map(0, 1);
    ExpectRead(stable, first, 0, true, "the range mapped again, read");
    ExpectWrite(stable, first + WriteOffset, Store::Lands, "the range mapped again, written");

    range.Protect(0, false);
    ExpectRead(stable, first, 0, true, "the range protected read-only, read");
    ExpectWrite(stable, first + WriteOffset, Store::ReadOnly, "the range protected read-only, written");

    range.Protect(0, true);
    ExpectWrite(stable, first + WriteOffset, Store::Lands, "the range made writable again, written");

    range.Unmap(0);
    range.Map(0, 2);
    ExpectRead(stable, second, 1, true, "the range grown to two units, read past its old end");
    ExpectWrite(stable, second + WriteOffset, Store::Lands, "the range grown to two units, written past its old end");
    ExpectRead(stable, first, 0, true, "the range grown to two units, read at its start");

    range.Unmap(0);
    range.Map(0, 1);
    ExpectRead(stable, second, 1, false, "the range shrunk back to one unit, read past its new end");
    ExpectWrite(stable, second + WriteOffset, Store::Unmapped, "the range shrunk back to one unit, written past its new end");
    ExpectRead(stable, first, 0, true, "the range shrunk back to one unit, read at its start");
}

std::chrono::milliseconds TimedFree(GuestBlock& block, const std::string& what) {
    auto done = std::make_shared<std::promise<std::string>>();
    auto result = done->get_future();
    void* base = block.Base();
    const auto start = std::chrono::steady_clock::now();
    std::thread([base, done] {
        try {
            GuestAllocations::Mutation().Remove(base);
            done->set_value({});
        } catch (const std::exception& error) {
            done->set_value(error.what());
        }
    }).detach();
    if (result.wait_for(FreeLimit) != std::future_status::ready) {
        Expect(false, what + ": the guest's free did not return within " + std::to_string(FreeLimit.count()) + " s: the range is still pinned");
        std::fflush(stderr);
        std::_Exit(1);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    const auto failure = result.get();
    Expect(failure.empty(), what + ": the guest's free failed: " + failure);
    if (failure.empty()) block.Unmapped(0);
    return elapsed;
}

void ExpectPromptFree(GuestBlock& block, const std::string& what) {
    StderrCapture capture;
    const auto elapsed = TimedFree(block, what);
    const auto log = capture.Finish();
    Expect(elapsed < PromptFree, what + ": the guest's free took " + std::to_string(elapsed.count()) + " ms");
    Expect(Count(log, "still pinned") == 0u, what + ": the guest's free found the range still pinned:\n" + log);
}

void Copy(Stable& stable, std::uint64_t destination, const std::string& what) {
    Commands commands;
    commands.Dispatch(CopyProgram(), Pointers(stable.Source(), destination), Lanes);
    ExpectClean(Submitted(commands), what);
}

void FreesAfterRebuilds(Stable& stable) {
    GuestBlock target(1);
    target.Map(0, 1);
    for (std::uint32_t round = 0; round < Rebuilds; ++round) {
        const auto slot = target.Address(0, std::uint64_t{round} * SlotBytes);
        {
            GuestBlock other(1);
            other.Map(0, 1);
            Copy(stable, slot, "rebuild " + std::to_string(round) + ", a dispatch after a new range was mapped");
            ExpectPromptFree(other, "rebuild " + std::to_string(round) + ", the free of the new range after a dispatch used its address space");
        }
        Copy(stable, slot, "rebuild " + std::to_string(round) + ", a dispatch after the new range was freed");
    }
    AgcDriver::GuestMemory::FlushGpuWrites(target.Address(), PageBytes);
    std::size_t wrong = 0;
    for (std::uint32_t word = 0; word < Rebuilds * Lanes; ++word) wrong += target.Words()[word] == SourceWord(word % Lanes) + 1u ? 0u : 1u;
    Expect(wrong == 0u, "frees after rebuilds: " + std::to_string(wrong) + " of the dispatches' stores did not land");
    ExpectPromptFree(target, "the free of a range " + std::to_string(2u * Rebuilds) + " address space rebuilds used");
}

void RetireWhilePreparing(Stable& stable) {
    GuestBlock toggled(1);
    GuestBlock output(1);
    toggled.Map(0, 1);
    output.Map(0, 1);
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) toggled.Words()[lane] = RangeWord(0, lane);
    for (std::uint32_t word = 0; word < PageBytes / 4u; ++word) output.Words()[word] = Untouched;
    std::atomic<bool> stop{false};
    std::atomic<std::uint32_t> toggles{0};
    std::string guestFailure;
    void* toggledBase = toggled.Base();
    std::thread guest([&] {
        try {
            while (!stop.load()) {
                GuestAllocations::Mutation().Remove(toggledBase);
                GuestAllocations::Mutation().Add(toggledBase, PageBytes, true, true, true);
                toggles.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
        } catch (const std::exception& error) {
            guestFailure = error.what();
        }
    });
    std::string failure;
    std::string log;
    {
        StderrCapture capture;
        try {
            for (std::uint32_t submission = 0; submission < RaceSubmissions; ++submission) {
                Commands commands;
                for (std::uint32_t dispatch = 0; dispatch < RaceDispatches; ++dispatch) {
                    const auto slot = output.Address(0, std::uint64_t{submission * RaceDispatches + dispatch} * RaceSlotBytes);
                    const std::array<std::uint32_t, 6> userData{Low(stable.Source()), High(stable.Source()), Low(toggled.Address()), High(toggled.Address()), Low(slot), High(slot)};
                    commands.Dispatch(TwoReadsProgram(), userData, Lanes);
                }
                commands.Submit(0);
                WaitIdle();
            }
            Settle();
        } catch (const std::exception& error) {
            failure = error.what();
        }
        stop.store(true);
        guest.join();
        log = capture.Finish();
    }
    const auto what = std::to_string(RaceSubmissions * RaceDispatches) + " dispatches reading a range another thread unmaps and maps again " + std::to_string(toggles.load()) + " times";
    Expect(failure.empty(), what + ": a submission failed: " + failure);
    Expect(guestFailure.empty(), what + ": the guest thread's mapping change failed: " + guestFailure);
    Expect(toggles.load() > 0u, what + ": the guest thread changed no mapping while the dispatches ran");
    Expect(Count(log, "deferred write-back failed") == Count(log, "BDA access failed"), what + ": a completion failed other than with a fault of the toggled range:\n" + log);
    AgcDriver::GuestMemory::FlushGpuWrites(output.Address(), PageBytes);
    std::size_t wrong = 0;
    std::string first;
    for (std::uint32_t dispatch = 0; dispatch < RaceSubmissions * RaceDispatches; ++dispatch) {
        for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
            const auto actual = output.Words(0, std::uint64_t{dispatch} * RaceSlotBytes)[lane];
            if (actual == SourceWord(lane)) continue;
            if (wrong++ == 0u) first = "dispatch " + std::to_string(dispatch) + " lane " + std::to_string(lane) + " stored " + Hex(actual) + ", expected " + Hex(SourceWord(lane));
        }
    }
    Expect(wrong == 0u, what + ": " + std::to_string(wrong) + " words read from the stable range are wrong, first " + first);
}

}

#endif

int main() {
#ifdef _WIN32
    {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
    }
    try {
        Stable stable;
        const std::array<std::pair<const char*, void (*)(Stable&)>, 3> cases{{
            {"mapping changes between dispatches", MappingChanges},
            {"frees after rebuilds", FreesAfterRebuilds},
            {"imports retired while dispatches prepare", RetireWhilePreparing},
        }};
        for (const auto& [name, run] : cases) {
            try {
                run(stable);
            } catch (const std::exception& error) {
                Expect(false, std::string(name) + ": exception: " + error.what());
            }
        }
        WaitIdle();
    } catch (const std::exception& error) {
        Expect(false, std::string("exception: ") + error.what());
    }
    try {
        AgcDriverShutdown_nid_postfix();
    } catch (const std::exception& error) {
        Expect(false, std::string("shutdown: ") + error.what());
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d address space mapping checks failed\n", failures);
        return 1;
    }
    std::printf("address space mapping tests passed\n");
    return 0;
#else
    std::printf("skipped, the guest arena is Windows-only here\n");
    return VulkanTestSkipped;
#endif
}
