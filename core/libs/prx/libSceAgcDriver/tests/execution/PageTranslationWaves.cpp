#include "NewPathFixture.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <cstdlib>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32

namespace {

using namespace NewPath;

constexpr std::uint32_t Lanes = 64;
constexpr std::uint32_t Untouched = 0xdeadbeefu;
constexpr std::uint32_t StoredMark = 0xa1000000u;
constexpr std::uint32_t WalkPages = 6;
constexpr std::uint32_t ReloadOffset = 0x400;

int failures = 0;

void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

namespace Ops {

using namespace Isa;

constexpr std::uint32_t GlobalLoadDwordX4 = 0x0eu;
constexpr std::uint32_t GlobalStoreDwordX4 = 0x1eu;
constexpr std::uint32_t VOrB32 = 0x1cu;
constexpr std::uint32_t MoveS6ToS9 = 0xbe890306u;
constexpr std::uint32_t AddLiteralToS0 = 0x8000ff00u;
constexpr std::uint32_t AddCarryToS1 = 0x82018001u;
constexpr std::uint32_t AddLiteralToS2 = 0x8002ff02u;
constexpr std::uint32_t AddCarryToS3 = 0x82038003u;
constexpr std::uint32_t DecrementS9 = 0x80898109u;
constexpr std::uint32_t CompareS9NotZero = 0xbf078009u;
constexpr std::uint32_t BranchBackWhileNotZero = 0xbf85fff1u;

}

const Program& GatherProgram() {
    static const Program program = [] {
        using namespace Ops;
        Program built;
        built << Vop2(VLshlRevB32, 1, InlineInteger(2), 0)
              << Global(GlobalLoadDword, 2, 1, 0, 2)
              << WaitAll
              << Global(GlobalLoadDword, 3, 2, 0, 0)
              << Mubuf(BufferLoadDword, 4, 2, 8)
              << WaitAll
              << Vop2(VLshlRevB32, 5, InlineInteger(3), 0)
              << Global(GlobalStoreDword, 0, 5, 3, 4)
              << Global(GlobalStoreDword, 0, 5, 4, 4, 4)
              << EndProgram;
        return built;
    }();
    return program;
}

const Program& ScatterProgram() {
    static const Program program = [] {
        using namespace Ops;
        Program built;
        built << Vop2(VLshlRevB32, 1, InlineInteger(2), 0)
              << Global(GlobalLoadDword, 2, 1, 0, 2)
              << WaitAll
              << Vop2(VLshlRevB32, 3, InlineInteger(8), 0)
              << Vop2(VOrB32, 3, Literal, 3) << StoredMark
              << Vop2(VAddNcU32, 4, InlineInteger(1), 3)
              << Vop2(VAddNcU32, 5, InlineInteger(2), 3)
              << Vop2(VAddNcU32, 6, InlineInteger(3), 3)
              << Global(GlobalStoreDwordX4, 0, 2, 3, 0)
              << EndProgram;
        return built;
    }();
    return program;
}

const Program& UpdateProgram() {
    static const Program program = [] {
        using namespace Ops;
        Program built;
        built << Vop2(VLshlRevB32, 1, InlineInteger(2), 0)
              << Global(GlobalLoadDword, 2, 1, 0, 0)
              << WaitAll
              << Vop2(VAddNcU32, 2, InlineInteger(1), 2)
              << Global(GlobalStoreDword, 0, 1, 2, 0, ReloadOffset)
              << Global(GlobalLoadDword, 3, 1, 0, 0, ReloadOffset)
              << WaitAll
              << Global(GlobalStoreDword, 0, 1, 3, 2)
              << EndProgram;
        return built;
    }();
    return program;
}

const Program& WalkProgram() {
    static const Program program = [] {
        using namespace Ops;
        Program built;
        built << Vop2(VLshlRevB32, 1, InlineInteger(2), 0)
              << MoveS6ToS9
              << Global(GlobalLoadDword, 2, 1, 0, 0)
              << WaitAll
              << Vop2(VAddNcU32, 2, InlineInteger(1), 2)
              << Global(GlobalStoreDword, 0, 1, 2, 2)
              << AddLiteralToS0 << static_cast<std::uint32_t>(PageBytes)
              << AddCarryToS1
              << AddLiteralToS2 << static_cast<std::uint32_t>(PageBytes)
              << AddCarryToS3
              << DecrementS9
              << CompareS9NotZero
              << BranchBackWhileNotZero
              << EndProgram;
        return built;
    }();
    return program;
}

class GuestBlock {
public:
    explicit GuestBlock(std::uint32_t units) : bytes(units * PageBytes), mapped(units, false) {
        base = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(bytes, PageBytes));
        Require(base != nullptr, "page translation waves: cannot reserve guest pages");
        GuestArena::GuestArenaCommit_nid_postfix(base, bytes, PAGE_READWRITE, 16384);
    }

    ~GuestBlock() {
        for (std::uint32_t unit = 0; unit < mapped.size(); ++unit) {
            if (!mapped[unit]) continue;
            try {
                GuestAllocations::Mutation().Remove(base + unit * PageBytes);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: page translation waves: a range could not be removed: %s\n", error.what());
            }
        }
        GuestArena::GuestArenaReset_nid_postfix(base, bytes);
        GuestArena::GuestArenaRelease_nid_postfix(base, bytes);
    }

    GuestBlock(const GuestBlock&) = delete;
    GuestBlock& operator=(const GuestBlock&) = delete;

    void Map(std::uint32_t unit, bool writable = true) {
        GuestAllocations::Mutation().Add(base + unit * PageBytes, PageBytes, true, writable, true);
        mapped[unit] = true;
    }

    void MapAll() {
        for (std::uint32_t unit = 0; unit < mapped.size(); ++unit) Map(unit);
    }

    std::uint64_t Address(std::uint64_t offset = 0) const { return reinterpret_cast<std::uint64_t>(base) + offset; }
    volatile std::uint32_t* Words() const { return reinterpret_cast<volatile std::uint32_t*>(base); }
    std::uint64_t Bytes() const { return bytes; }
    std::uint32_t Units() const { return static_cast<std::uint32_t>(mapped.size()); }

private:
    std::uint8_t* base = nullptr;
    std::uint64_t bytes = 0;
    std::vector<bool> mapped;
};

std::uint32_t Pattern(std::uint64_t word) {
    return 0x40000000u | static_cast<std::uint32_t>(word & 0x3fffffffu);
}

void FillPattern(const GuestBlock& block) {
    for (std::uint64_t word = 0; word < block.Bytes() / 4u; ++word) block.Words()[word] = Pattern(word);
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

std::vector<std::string> Reports(const std::string& log) {
    std::vector<std::string> reports;
    for (auto at = log.find("BDA access failed"); at != std::string::npos; at = log.find("BDA access failed", at + 1u)) {
        const auto begin = log.rfind('\n', at);
        const auto first = begin == std::string::npos ? 0u : begin + 1u;
        reports.push_back(log.substr(first, log.find('\n', at) - first));
    }
    return reports;
}

std::uint64_t ReportedAddress(const std::string& report) {
    const auto at = report.find("address=0x");
    return at != std::string::npos ? std::strtoull(report.c_str() + at + 10, nullptr, 16) : 0u;
}

void ExpectClean(const Outcome& outcome, const std::string& what) {
    Expect(outcome.failure.empty() && Reports(outcome.log).empty(), what + ": the dispatch reported a failure: " + outcome.failure + "\n" + outcome.log);
}

using Ranges = std::vector<std::pair<std::uint64_t, std::uint64_t>>;

void ExpectFaults(const Outcome& outcome, std::uint32_t reason, const Ranges& accessed, const std::string& what) {
    Expect(outcome.failure.empty(), what + ": the fault stopped the driver: " + outcome.failure);
    const auto reports = Reports(outcome.log);
    Expect(!reports.empty(), what + ": no fault was reported:\n" + outcome.log);
    for (const auto& report : reports) {
        const auto address = ReportedAddress(report);
        const bool inside = std::ranges::any_of(accessed, [&](const auto& range) { return address >= range.first && address < range.second; });
        Expect(report.find("reason=" + std::to_string(reason)) != std::string::npos && inside, what + ": a report does not give reason " + std::to_string(reason) + " at an address a lane accessed on the faulting page: " + report);
    }
}

std::uint64_t Collect(const GuestBlock& block) {
    AgcDriver::GuestMemory::CollectWritesUncached(block.Address(), block.Bytes());
    return AgcDriver::GuestMemory::TrackerGeneration();
}

void ExpectWrittenBlocks(const GuestBlock& block, std::uint64_t generation, const std::vector<bool>& stored, const std::string& what) {
    const std::vector<std::uint64_t> generations(block.Units(), generation);
    std::vector<std::uint8_t> changed(block.Units());
    std::vector<std::uint8_t> cpu(block.Units());
    AgcDriver::GuestMemory::CollectWritesUncached(block.Address(), block.Bytes());
    AgcDriver::GuestMemory::ChangedBlocks(block.Address(), block.Bytes(), generations, changed, cpu);
    for (std::uint32_t unit = 0; unit < block.Units(); ++unit) {
        if (stored[unit]) Expect(changed[unit] == AgcDriver::GuestMemory::BlockWritten && cpu[unit] == 0u, what + ": 64 KiB block " + std::to_string(unit) + ", which the wave stored to, was not marked written by the GPU");
        else Expect(changed[unit] == AgcDriver::GuestMemory::BlockUnchanged, what + ": 64 KiB block " + std::to_string(unit) + ", which no lane stored to, was marked written");
    }
}

struct Scratch {
    PrivatePages table{1};
    PrivatePages output{1};

    void Offsets(const std::vector<std::uint32_t>& offsets) {
        Require(offsets.size() == Lanes, "page translation waves: a lane table needs one offset per lane");
        for (std::uint32_t lane = 0; lane < Lanes; ++lane) table.Words()[lane] = offsets[lane];
        for (std::uint64_t word = 0; word < output.Bytes() / 4u; ++word) output.Words()[word] = Untouched;
    }

    std::vector<std::uint32_t> Output(std::uint32_t words) const {
        AgcDriver::GuestMemory::FlushGpuWrites(output.Address(), std::uint64_t{words} * 4u);
        std::vector<std::uint32_t> read(words);
        for (std::uint32_t word = 0; word < words; ++word) read[word] = output.Words()[word];
        return read;
    }
};

Outcome Gather(const GuestBlock& block, Scratch& scratch, const std::vector<std::uint32_t>& offsets) {
    scratch.Offsets(offsets);
    const auto buffer = BufferResource(block.Address(), static_cast<std::uint32_t>(block.Bytes()));
    const std::array<std::uint32_t, 12> userData{Low(block.Address()), High(block.Address()), Low(scratch.table.Address()), High(scratch.table.Address()), Low(scratch.output.Address()), High(scratch.output.Address()), 0u, 0u, buffer[0], buffer[1], buffer[2], buffer[3]};
    Commands commands;
    commands.Dispatch(GatherProgram(), userData, Lanes);
    return Submitted(commands);
}

void ExpectGathered(const Scratch& scratch, const std::vector<std::uint32_t>& offsets, const std::vector<bool>& reachable, const std::string& what) {
    const auto words = scratch.Output(Lanes * 2u);
    std::size_t wrong = 0;
    std::string first;
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
        if (!reachable[lane]) continue;
        const auto expected = Pattern(offsets[lane] / 4u);
        for (std::uint32_t kind = 0; kind < 2u; ++kind) {
            const auto actual = words[lane * 2u + kind];
            if (actual == expected) continue;
            if (wrong++ == 0u) first = std::string(kind == 0u ? "the GLOBAL" : "the V#") + " load of lane " + std::to_string(lane) + " at +" + Hex(offsets[lane]) + " read " + Hex(actual) + ", expected " + Hex(expected);
        }
    }
    Expect(wrong == 0u, what + ": " + std::to_string(wrong) + " loads read the wrong word, first " + first);
}

void OnePage() {
    GuestBlock block(1);
    block.MapAll();
    FillPattern(block);
    Scratch scratch;
    std::vector<std::uint32_t> offsets(Lanes);
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) offsets[lane] = ((Lanes - 1u - lane) * 977u * 4u) % static_cast<std::uint32_t>(PageBytes);
    const std::string what = "loads of 64 lanes scattered over one page";
    const auto outcome = Gather(block, scratch, offsets);
    ExpectClean(outcome, what);
    ExpectGathered(scratch, offsets, std::vector<bool>(Lanes, true), what);
}

void TwoPages() {
    GuestBlock block(4);
    block.MapAll();
    FillPattern(block);
    Scratch scratch;
    std::vector<std::uint32_t> offsets(Lanes);
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) offsets[lane] = static_cast<std::uint32_t>((lane % 2u == 0u ? 0u : 3u * PageBytes) + (lane * 1031u * 4u) % PageBytes);
    const std::string what = "loads of the even lanes from page 0 and the odd lanes from page 3";
    const auto outcome = Gather(block, scratch, offsets);
    ExpectClean(outcome, what);
    ExpectGathered(scratch, offsets, std::vector<bool>(Lanes, true), what);
}

void UnmappedLoads() {
    GuestBlock block(3);
    block.Map(0);
    block.Map(2);
    FillPattern(block);
    Scratch scratch;
    std::vector<std::uint32_t> offsets(Lanes);
    std::vector<bool> reachable(Lanes);
    Ranges accessed;
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
        const auto page = lane % 3u;
        offsets[lane] = static_cast<std::uint32_t>(page * PageBytes + 0x100u + lane * 16u);
        reachable[lane] = page != 1u;
        if (page == 1u) accessed.emplace_back(block.Address(offsets[lane]), block.Address(offsets[lane]) + 4u);
    }
    const std::string what = "loads of lanes interleaved over pages 0, 1 and 2, page 1 unmapped";
    const auto outcome = Gather(block, scratch, offsets);
    ExpectFaults(outcome, 1u, accessed, what);
    ExpectGathered(scratch, offsets, reachable, what);
}

Outcome Scatter(const GuestBlock& block, Scratch& scratch, const std::vector<std::uint32_t>& offsets) {
    scratch.Offsets(offsets);
    const std::array<std::uint32_t, 4> userData{Low(block.Address()), High(block.Address()), Low(scratch.table.Address()), High(scratch.table.Address())};
    Commands commands;
    commands.Dispatch(ScatterProgram(), userData, Lanes);
    return Submitted(commands);
}

void ExpectScattered(const GuestBlock& block, const std::vector<std::uint32_t>& offsets, const std::vector<bool>& landed, const std::vector<bool>& readable, const std::string& what) {
    AgcDriver::GuestMemory::FlushGpuWrites(block.Address(), block.Bytes());
    std::vector<std::uint32_t> expected(static_cast<std::size_t>(block.Bytes() / 4u));
    for (std::size_t word = 0; word < expected.size(); ++word) expected[word] = Pattern(word);
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
        if (!landed[lane]) continue;
        for (std::uint32_t component = 0; component < 4u; ++component) expected[offsets[lane] / 4u + component] = (StoredMark | (lane << 8u)) + component;
    }
    std::size_t wrong = 0;
    std::string first;
    for (std::size_t word = 0; word < expected.size(); ++word) {
        if (!readable[word * 4u / PageBytes]) continue;
        const auto actual = block.Words()[word];
        if (actual == expected[word]) continue;
        if (wrong++ == 0u) first = "byte " + Hex(word * 4u) + " holds " + Hex(actual) + ", expected " + Hex(expected[word]);
    }
    Expect(wrong == 0u, what + ": " + std::to_string(wrong) + " words differ from the lanes' stores over the previous bytes, first " + first);
}

void CrossingStores() {
    GuestBlock block(3);
    block.MapAll();
    FillPattern(block);
    Scratch scratch;
    constexpr std::uint32_t First = static_cast<std::uint32_t>(PageBytes) - Lanes * 8u - 8u;
    std::vector<std::uint32_t> offsets(Lanes);
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) offsets[lane] = First + lane * 16u;
    const std::string what = "16-byte stores of 64 lanes in a run from " + Hex(First) + ", one of them across the boundary of pages 0 and 1";
    const auto generation = Collect(block);
    const auto outcome = Scatter(block, scratch, offsets);
    ExpectClean(outcome, what);
    ExpectScattered(block, offsets, std::vector<bool>(Lanes, true), std::vector<bool>(3, true), what);
    ExpectWrittenBlocks(block, generation, {true, true, false}, what);
}

void UnmappedStores() {
    GuestBlock block(3);
    block.Map(0);
    block.Map(2);
    FillPattern(block);
    Scratch scratch;
    std::vector<std::uint32_t> offsets(Lanes);
    std::vector<bool> landed(Lanes);
    Ranges accessed;
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
        const auto page = lane % 3u;
        offsets[lane] = static_cast<std::uint32_t>(page * PageBytes + 0x200u + lane * 16u);
        landed[lane] = page != 1u;
        if (page == 1u) accessed.emplace_back(block.Address(offsets[lane]), block.Address(offsets[lane]) + 16u);
    }
    const std::string what = "16-byte stores of lanes interleaved over pages 0, 1 and 2, page 1 unmapped";
    const auto generation = Collect(block);
    const auto outcome = Scatter(block, scratch, offsets);
    ExpectFaults(outcome, 1u, accessed, what);
    ExpectScattered(block, offsets, landed, std::vector<bool>(3, true), what);
    ExpectWrittenBlocks(block, generation, {true, false, true}, what);
}

void LoadAndStoreOnePage(bool writable) {
    GuestBlock block(1);
    block.Map(0, writable);
    FillPattern(block);
    Scratch scratch;
    for (std::uint64_t word = 0; word < scratch.output.Bytes() / 4u; ++word) scratch.output.Words()[word] = Untouched;
    const std::string what = std::string("a load, a store and a reload of one wave on one ") + (writable ? "writable" : "read-only") + " page";
    const auto generation = Collect(block);
    const std::array<std::uint32_t, 4> userData{Low(block.Address()), High(block.Address()), Low(scratch.output.Address()), High(scratch.output.Address())};
    Commands commands;
    commands.Dispatch(UpdateProgram(), userData, Lanes);
    const auto outcome = Submitted(commands);
    if (writable) {
        ExpectClean(outcome, what);
    } else {
        ExpectFaults(outcome, 2u, {{block.Address(ReloadOffset), block.Address(ReloadOffset + Lanes * 4u)}}, what);
    }
    AgcDriver::GuestMemory::FlushGpuWrites(block.Address(), block.Bytes());
    const auto reloaded = scratch.Output(Lanes);
    std::size_t wrong = 0;
    std::string first;
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
        const auto stored = block.Words()[ReloadOffset / 4u + lane];
        const auto expected = writable ? Pattern(lane) + 1u : Pattern(ReloadOffset / 4u + lane);
        if (stored == expected && reloaded[lane] == expected) continue;
        if (wrong++ == 0u) first = "lane " + std::to_string(lane) + " left " + Hex(stored) + " and reloaded " + Hex(reloaded[lane]) + ", expected " + Hex(expected);
    }
    Expect(wrong == 0u, what + ": " + std::to_string(wrong) + " lanes did not " + (writable ? "store and reload their loaded word plus one" : "keep and reload the previous word") + ", first " + first);
    ExpectWrittenBlocks(block, generation, {writable}, what);
}

void PageWalk() {
    GuestBlock source(WalkPages);
    GuestBlock target(WalkPages + 1u);
    source.MapAll();
    target.MapAll();
    FillPattern(source);
    for (std::uint64_t word = 0; word < target.Bytes() / 4u; ++word) target.Words()[word] = Untouched;
    const std::string what = "a loop whose load and store move one page per iteration over " + std::to_string(WalkPages) + " pages";
    const auto generation = Collect(target);
    const std::array<std::uint32_t, 7> userData{Low(source.Address()), High(source.Address()), Low(target.Address()), High(target.Address()), 0u, 0u, WalkPages};
    Commands commands;
    commands.Dispatch(WalkProgram(), userData, Lanes);
    const auto outcome = Submitted(commands);
    ExpectClean(outcome, what);
    AgcDriver::GuestMemory::FlushGpuWrites(target.Address(), target.Bytes());
    std::size_t wrong = 0;
    std::string first;
    for (std::uint32_t page = 0; page <= WalkPages; ++page) {
        for (std::uint32_t word = 0; word < Lanes * 2u; ++word) {
            const auto at = std::uint64_t{page} * WordsPerPage + word;
            const auto expected = page < WalkPages && word < Lanes ? Pattern(at) + 1u : Untouched;
            const auto actual = target.Words()[at];
            if (actual == expected) continue;
            if (wrong++ == 0u) first = "page " + std::to_string(page) + " word " + std::to_string(word) + " holds " + Hex(actual) + ", expected " + Hex(expected);
        }
    }
    Expect(wrong == 0u, what + ": " + std::to_string(wrong) + " words differ, first " + first);
    std::vector<bool> stored(WalkPages + 1u, true);
    stored[WalkPages] = false;
    ExpectWrittenBlocks(target, generation, stored, what);
}

}

#endif

int main() {
#ifdef _WIN32
    {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
    }
    const std::array<std::pair<const char*, void (*)()>, 8> cases{{
        {"one page", OnePage},
        {"two pages", TwoPages},
        {"unmapped loads", UnmappedLoads},
        {"crossing stores", CrossingStores},
        {"unmapped stores", UnmappedStores},
        {"load and store on a writable page", [] { LoadAndStoreOnePage(true); }},
        {"load and store on a read-only page", [] { LoadAndStoreOnePage(false); }},
        {"page walk", PageWalk},
    }};
    for (const auto& [name, run] : cases) {
        try {
            run();
        } catch (const std::exception& error) {
            Expect(false, std::string(name) + ": exception: " + error.what());
        }
    }
    try {
        AgcDriverShutdown_nid_postfix();
    } catch (const std::exception& error) {
        Expect(false, std::string("shutdown: ") + error.what());
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d page translation wave checks failed\n", failures);
        return 1;
    }
    std::printf("page translation wave tests passed\n");
    return 0;
#else
    std::printf("skipped, the guest arena is Windows-only here\n");
    return VulkanTestSkipped;
#endif
}
