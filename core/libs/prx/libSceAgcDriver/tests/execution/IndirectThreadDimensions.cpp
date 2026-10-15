#include "GuestTestMemory.hpp"
#include "NewPathFixture.hpp"
#include "VulkanTestDevice.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <span>
#include <string>
#include <utility>

namespace {

using NewPath::Commands;
using NewPath::Hex;
using NewPath::High;
using NewPath::Low;
using NewPath::Program;
using Counts = std::array<std::uint32_t, 3>;

constexpr std::uint32_t ComputeQueue = 0x20;
constexpr std::uint32_t ThreadDimensions = 0x8061u;
constexpr std::uint32_t GroupDimensions = 0x8041u;
constexpr Counts GroupSize{4, 4, 2};
constexpr Counts Extent{32, 16, 8};
constexpr std::uint32_t RegionWords = Extent[0] * Extent[1] * Extent[2];
constexpr std::uint32_t Regions = 96;
constexpr std::uint32_t SlotWords = 4;
constexpr std::uint32_t Slots = 72;
constexpr std::uint32_t BatchDispatches = 64;
constexpr std::uint32_t BatchFirstRegion = 16;
constexpr std::uint32_t BatchFirstSlot = 8;
constexpr std::uint32_t Marker = 0x40000000u;
constexpr std::uint32_t Stale = 1u;
constexpr std::uint32_t GateLabel = 0;
constexpr std::uint32_t DoneLabel = 16;
constexpr std::uint32_t LabelWords = 32;

TestGuest::Array<std::uint32_t, std::size_t{Regions} * RegionWords> Output;
TestGuest::Array<std::uint32_t, std::size_t{Slots} * SlotWords> Arguments;
TestGuest::Array<std::uint32_t, LabelWords> Labels;
std::uint32_t submissions = 0;
int failures = 0;

void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

namespace Ops {

using namespace NewPath::Isa;

constexpr std::uint32_t VOrB32 = 0x1cu;
constexpr std::uint32_t VMadU32U24 = 0x143u;
constexpr std::uint32_t VLshlOrB32 = 0x36fu;
constexpr std::uint32_t BufferStoreDwordX3 = 0x1fu;

constexpr std::array<std::uint32_t, 2> Vop3(std::uint32_t opcode, std::uint32_t vdst, std::uint32_t src0, std::uint32_t src1, std::uint32_t src2) {
    return {0xd4000000u | (opcode << 16u) | vdst, src0 | (src1 << 9u) | (src2 << 18u)};
}

}

constexpr std::uint32_t RowSgpr = 4;
constexpr std::uint32_t PlaneSgpr = 5;
constexpr std::uint32_t GroupSgpr = 6;
constexpr std::uint32_t IdUserWords = 6;
constexpr std::uint32_t IdResources = (IdUserWords << 1u) | (7u << 7u) | (2u << 11u);
constexpr std::uint32_t WriterUserWords = 7;
constexpr std::uint32_t WriterResources = WriterUserWords << 1u;

const Program& IdProgram() {
    static const Program program = [] {
        using namespace Ops;
        Program built;
        built << Vop3(VMadU32U24, 3, GroupSgpr, InlineInteger(GroupSize[0]), Vgpr(0))
              << Vop3(VMadU32U24, 4, GroupSgpr + 1u, InlineInteger(GroupSize[1]), Vgpr(1))
              << Vop3(VMadU32U24, 5, GroupSgpr + 2u, InlineInteger(GroupSize[2]), Vgpr(2))
              << Vop3(VMadU32U24, 6, Vgpr(4), RowSgpr, Vgpr(3))
              << Vop3(VMadU32U24, 6, Vgpr(5), PlaneSgpr, Vgpr(6))
              << Vop2(VLshlRevB32, 7, InlineInteger(2), 6)
              << Vop3(VLshlOrB32, 8, Vgpr(4), InlineInteger(8), Vgpr(3))
              << Vop3(VLshlOrB32, 8, Vgpr(5), InlineInteger(16), Vgpr(8))
              << Vop2(VOrB32, 8, Literal, 8) << Marker
              << Mubuf(BufferStoreDword, 8, 7, 0)
              << EndProgram;
        return built;
    }();
    return program;
}

const Program& WriterProgram() {
    static const Program program = [] {
        using namespace Ops;
        Program built;
        built << Vop1(VMovB32, 0, 4)
              << Vop1(VMovB32, 1, 5)
              << Vop1(VMovB32, 2, 6)
              << Vop1(VMovB32, 3, InlineInteger(0))
              << Mubuf(BufferStoreDwordX3, 0, 3, 0)
              << EndProgram;
        return built;
    }();
    return program;
}

std::uint64_t Address(const void* pointer) {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

std::uint64_t RegionAddress(std::uint32_t region) {
    return Address(Output.data() + std::size_t{region} * RegionWords);
}

std::uint64_t SlotAddress(std::uint32_t slot) {
    return Address(Arguments.data() + std::size_t{slot} * SlotWords);
}

std::uint64_t LabelAddress(std::uint32_t label) {
    return Address(Labels.data() + label);
}

void SetSlot(std::uint32_t slot, const Counts& counts) {
    for (std::uint32_t axis = 0; axis < 3u; ++axis) Arguments[std::size_t{slot} * SlotWords + axis] = counts[axis];
}

Counts Covered(const Counts& groups) {
    return {groups[0] * GroupSize[0], groups[1] * GroupSize[1], groups[2] * GroupSize[2]};
}

std::string Describe(const Counts& counts) {
    return std::to_string(counts[0]) + "x" + std::to_string(counts[1]) + "x" + std::to_string(counts[2]);
}

void Bind(Commands& commands, const Program& program, const Counts& size, std::uint32_t resources, std::span<const std::uint32_t> userData) {
    commands.Registers(0x207, {size[0], size[1], size[2]});
    commands.Registers(0x20c, {Low(program.Address() >> 8u), Low(program.Address() >> 40u)});
    commands.Registers(0x212, {0u, resources});
    commands.Registers(0x240, userData);
}

void BindIds(Commands& commands, std::uint32_t region) {
    const auto buffer = NewPath::BufferResource(RegionAddress(region), RegionWords * 4u);
    const std::array<std::uint32_t, IdUserWords> userData{buffer[0], buffer[1], buffer[2], buffer[3], Extent[0], Extent[0] * Extent[1]};
    Bind(commands, IdProgram(), GroupSize, IdResources, userData);
}

void DirectIds(Commands& commands, std::uint32_t region, const Counts& counts, std::uint32_t initiator) {
    BindIds(commands, region);
    commands.Append(0x15u, {counts[0], counts[1], counts[2], initiator});
}

void IndirectIds(Commands& commands, std::uint32_t region, std::uint32_t slot, std::uint32_t initiator) {
    BindIds(commands, region);
    const auto arguments = SlotAddress(slot);
    commands.Append(0x16u, {Low(arguments), High(arguments), initiator});
}

void WriteCounts(Commands& commands, std::uint32_t slot, const Counts& counts) {
    const auto buffer = NewPath::BufferResource(SlotAddress(slot), SlotWords * 4u);
    const std::array<std::uint32_t, WriterUserWords> userData{buffer[0], buffer[1], buffer[2], buffer[3], counts[0], counts[1], counts[2]};
    Bind(commands, WriterProgram(), {1u, 1u, 1u}, WriterResources, userData);
    commands.Append(0x15u, {1u, 1u, 1u, GroupDimensions});
}

void Run(Commands& commands, const std::string& what) {
    ++submissions;
    commands.WriteData(LabelAddress(DoneLabel), {submissions});
    commands.Submit(ComputeQueue);
    NewPath::WaitIdle();
    AgcDriver::GuestMemory::FlushGpuWrites(LabelAddress(0), LabelWords * 4u);
    Expect(Labels[DoneLabel] == submissions, what + ": the submission did not reach its end label (" + Hex(Labels[DoneLabel]) + ", expected " + Hex(submissions) + ")");
}

std::uint32_t Id(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return Marker | (z << 16u) | (y << 8u) | x;
}

void ExpectIds(std::uint32_t region, const Counts& expected, const std::string& what) {
    AgcDriver::GuestMemory::FlushGpuWrites(RegionAddress(region), RegionWords * 4u);
    const auto* words = Output.data() + std::size_t{region} * RegionWords;
    std::uint32_t written = 0;
    std::uint32_t wrong = 0;
    std::string first;
    for (std::uint32_t z = 0; z < Extent[2]; ++z) {
        for (std::uint32_t y = 0; y < Extent[1]; ++y) {
            for (std::uint32_t x = 0; x < Extent[0]; ++x) {
                const auto actual = words[(z * Extent[1] + y) * Extent[0] + x];
                const bool inside = x < expected[0] && y < expected[1] && z < expected[2];
                const auto wanted = inside ? Id(x, y, z) : 0u;
                if (actual != 0u) ++written;
                if (actual == wanted) continue;
                if (wrong++ == 0u) first = "thread (" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ") holds " + Hex(actual) + ", expected " + Hex(wanted);
            }
        }
    }
    Expect(wrong == 0u, what + ": " + std::to_string(written) + " threads wrote their id, expected " + std::to_string(expected[0] * expected[1] * expected[2]) + " (" + Describe(expected) + "); " + std::to_string(wrong) + " slots differ, first " + first);
}

void CpuWrittenCounts() {
    constexpr std::array<Counts, 8> sets{{{5, 3, 3}, {8, 4, 2}, {1, 1, 1}, {17, 9, 5}, {32, 16, 8}, {0, 5, 3}, {6, 7, 0}, {3, 0, 0}}};
    for (std::uint32_t set = 0; set < sets.size(); ++set) {
        const auto what = "CPU-written thread counts " + Describe(sets[set]) + " in groups of " + Describe(GroupSize);
        SetSlot(0, sets[set]);
        Commands commands;
        IndirectIds(commands, set, 0, ThreadDimensions);
        Run(commands, what);
        ExpectIds(set, sets[set], what);
    }
}

void GpuWrittenCounts() {
    constexpr Counts first{7, 5, 3};
    constexpr Counts second{13, 2, 6};
    constexpr Counts empty{0, 4, 4};
    SetSlot(1, {Stale, Stale, Stale});
    SetSlot(2, {Stale, Stale, Stale});
    Labels[GateLabel] = 1u;
    Commands commands;
    commands.Wait(LabelAddress(GateLabel), 3u, 1u, 0xffffffffu);
    WriteCounts(commands, 1, first);
    IndirectIds(commands, 8, 1, ThreadDimensions);
    WriteCounts(commands, 1, second);
    IndirectIds(commands, 9, 1, ThreadDimensions);
    WriteCounts(commands, 2, empty);
    IndirectIds(commands, 10, 2, ThreadDimensions);
    const std::string what = "thread counts an earlier dispatch of the submission writes";
    Run(commands, what);
    ExpectIds(8, first, what + ", first write " + Describe(first));
    ExpectIds(9, second, what + ", rewritten to " + Describe(second) + " after the first indirect dispatch read them");
    ExpectIds(10, empty, what + ", " + Describe(empty));
    AgcDriver::GuestMemory::FlushGpuWrites(SlotAddress(1), SlotWords * 4u);
    Expect(Arguments[SlotWords] == second[0] && Arguments[SlotWords + 1u] == second[1] && Arguments[SlotWords + 2u] == second[2], what + ": the guest's argument slot does not hold the last counts written");
}

Counts BatchCounts(std::uint32_t dispatch) {
    return {(dispatch * 7u) % (Extent[0] + 1u), 1u + (dispatch * 5u) % Extent[1], 1u + (dispatch * 3u) % Extent[2]};
}

void Batch() {
    Commands commands;
    for (std::uint32_t dispatch = 0; dispatch < BatchDispatches; ++dispatch) {
        SetSlot(BatchFirstSlot + dispatch, BatchCounts(dispatch));
        IndirectIds(commands, BatchFirstRegion + dispatch, BatchFirstSlot + dispatch, ThreadDimensions);
    }
    const auto what = std::to_string(BatchDispatches) + " indirect dispatches with thread dimensions in one batch";
    Run(commands, what);
    for (std::uint32_t dispatch = 0; dispatch < BatchDispatches; ++dispatch) {
        ExpectIds(BatchFirstRegion + dispatch, BatchCounts(dispatch), what + ", dispatch " + std::to_string(dispatch));
    }
}

struct KeyedDispatch {
    bool indirect;
    std::uint32_t initiator;
    Counts counts;
};

void Keys() {
    constexpr std::array<KeyedDispatch, 5> forward{{
        {false, GroupDimensions, {3, 2, 2}},
        {false, ThreadDimensions, {13, 7, 3}},
        {true, ThreadDimensions, {6, 11, 5}},
        {true, GroupDimensions, {4, 3, 1}},
        {false, ThreadDimensions, {12, 8, 4}},
    }};
    constexpr std::array<KeyedDispatch, 5> backward{{
        {false, ThreadDimensions, {20, 16, 2}},
        {true, GroupDimensions, {2, 4, 3}},
        {true, ThreadDimensions, {9, 5, 7}},
        {false, ThreadDimensions, {3, 14, 1}},
        {false, GroupDimensions, {1, 1, 4}},
    }};
    constexpr std::uint32_t FirstRegion = BatchFirstRegion + BatchDispatches;
    constexpr std::array<std::uint32_t, 2> ThreadSlot{4, 6};
    constexpr std::array<std::uint32_t, 2> GroupSlot{5, 7};
    const std::array<std::pair<const char*, const std::array<KeyedDispatch, 5>*>, 2> passes{{{"first", &forward}, {"second, in reverse order", &backward}}};
    for (std::uint32_t pass = 0; pass < passes.size(); ++pass) {
        Commands commands;
        const auto& dispatches = *passes[pass].second;
        for (std::uint32_t index = 0; index < dispatches.size(); ++index) {
            const auto& dispatch = dispatches[index];
            const auto region = FirstRegion + pass * 5u + index;
            if (!dispatch.indirect) {
                DirectIds(commands, region, dispatch.counts, dispatch.initiator);
                continue;
            }
            const auto slot = dispatch.initiator == ThreadDimensions ? ThreadSlot[pass] : GroupSlot[pass];
            SetSlot(slot, dispatch.counts);
            IndirectIds(commands, region, slot, dispatch.initiator);
        }
        const auto what = std::string("one program dispatched directly and indirectly, in groups and in threads, ") + passes[pass].first + " pass";
        Run(commands, what);
        for (std::uint32_t index = 0; index < dispatches.size(); ++index) {
            const auto& dispatch = dispatches[index];
            const bool threads = dispatch.initiator == ThreadDimensions;
            const auto expected = threads ? dispatch.counts : Covered(dispatch.counts);
            const auto kind = std::string(dispatch.indirect ? "indirect" : "direct") + (threads ? " in threads " : " in groups ") + Describe(dispatch.counts);
            ExpectIds(FirstRegion + pass * 5u + index, expected, what + ", " + kind);
        }
    }
}

}

int main() {
    {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
    }
    const std::array<std::pair<const char*, std::function<void()>>, 4> checks{{
        {"CPU-written thread counts", CpuWrittenCounts},
        {"GPU-written thread counts", GpuWrittenCounts},
        {"batch of indirect dispatches", Batch},
        {"program keys", Keys},
    }};
    for (const auto& [name, check] : checks) {
        try {
            check();
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
        std::fprintf(stderr, "%d indirect thread dimension checks failed\n", failures);
        return 1;
    }
    std::printf("indirect thread dimension tests passed\n");
    return 0;
}
