#include "MemoryBufferFixture.hpp"

namespace {

using namespace MemoryBuffers;

TestGuest::Array<std::uint32_t, TableWords> Table;

constexpr std::uint32_t DataWords = 8;

std::uint32_t DataWord(std::uint32_t word) {
    return 0x3a000000u + word * 0x00010203u;
}

const Program& WideProgram() {
    static const Program program = [] {
        using namespace Ops;
        Program built;
        built << Smem(SLoadDwordX4, 28, 0, OutputSlot * 4u)
              << Smem(SLoadDwordX8, 36, 0, DataSlot * 4u)
              << WaitAll;
        for (std::uint32_t component = 0; component < LaneWords; ++component) built << Vop1(VMovB32, 4 + component, 36 + component) << Vop2(VAddNcU32, 4 + component, 40 + component, 4 + component);
        StoreLane(built, 2, 28);
        return built;
    }();
    return program;
}

const Program& ChainProgram() {
    static const Program program = [] {
        using namespace Ops;
        Program built;
        built << Smem(SLoadDwordX2, 20, 0, TableSlot * 4u)
              << WaitAll
              << Smem(SLoadDwordX4, 28, 20, TableOutputSlot * 4u)
              << WaitAll;
        LaneValues(built, 2);
        StoreLane(built, 2, 28);
        return built;
    }();
    return program;
}

const Program& DynamicProgram() {
    static const Program program = [] {
        using namespace Ops;
        Program built;
        built << SmemRegisterOffset(SLoadDwordX4, 28, 0, 2, OutputSlot * 4u) << WaitAll;
        LaneValues(built, 2);
        StoreLane(built, 2, 28);
        return built;
    }();
    return program;
}

const Program& UserDataOffsetProgram() {
    static const Program program = [] {
        using namespace Ops;
        Program built;
        built << SmemRegisterOffset(SLoadDwordX4, 28, 0, 2, 0u) << WaitAll;
        LaneValues(built, 3);
        StoreLane(built, 3, 28);
        return built;
    }();
    return program;
}

std::uint64_t SrtAt(std::uint32_t slot) {
    return AddressOf(Srt.data()) + slot * 4u;
}

std::uint64_t TableAt(std::uint32_t slot) {
    return AddressOf(Table.data()) + slot * 4u;
}

bool Inside(const Ranges& ranges, std::initializer_list<std::pair<std::uint64_t, std::uint64_t>> objects) {
    return std::ranges::all_of(ranges, [&](const std::pair<std::uint64_t, std::uint64_t>& range) {
        return std::ranges::any_of(objects, [&](const std::pair<std::uint64_t, std::uint64_t>& object) { return range.first >= object.first && range.second <= object.second; });
    });
}

void ExpectRead(const ShaderRecompiler::RecompileResult& result, std::uint64_t begin, std::uint64_t end, const std::string& what) {
    Expect(Covers(result.readRanges, begin, end), what + ": the " + Reads(result) + " do not cover " + Span(begin, end));
}

void WideReads(AgcDriver::VulkanDevice& device) {
    Srt.fill(0u);
    const auto userData = SrtUserData();
    for (std::uint32_t word = 0; word < DataWords; ++word) Srt[DataSlot + word] = DataWord(word);
    const auto compiled = Compile(device, WideProgram(), userData);
    ExpectNothingCaptured(compiled, "SRT reads");
    const auto& result = *compiled.result;
    Expect(!result.unboundedReads, "SRT reads: an image-free program's s_load_dwordx4 and s_load_dwordx8 at immediate offsets from a user-data pointer make its reads unbounded (" + Reads(result) + ")");
    ExpectRead(result, SrtAt(OutputSlot), SrtAt(OutputSlot + 4u), "SRT reads: the s_load_dwordx4");
    ExpectRead(result, SrtAt(DataSlot), SrtAt(DataSlot + DataWords), "SRT reads: the s_load_dwordx8");
    Expect(Inside(result.readRanges, {{AddressOf(Srt.data()), AddressOf(Srt.data()) + decltype(Srt)::ByteSize}}), "SRT reads: the " + Reads(result) + " reach outside the SRT the program reads");
    Expect(!result.unboundedWrites, "SRT reads: the store through the V# from the SRT has the " + Writes(result));
    const auto outcome = Run(device, compiled, Height);
    ExpectClean(outcome, "SRT reads");
    ExpectRows(outcome.words, Height, [](std::uint32_t, std::uint32_t, std::uint32_t component) { return DataWord(component) + DataWord(component + LaneWords); }, "SRT reads: the sums of the s_load_dwordx8's words");
}

void PointerChain(AgcDriver::VulkanDevice& device) {
    Srt.fill(0u);
    Table.fill(0u);
    const auto userData = SrtUserData();
    Place(OutputSlot, {});
    const auto table = Pointer(Table.data());
    std::copy(table.begin(), table.end(), Srt.begin() + TableSlot);
    const auto output = Buffer(Output.data(), static_cast<std::uint32_t>(decltype(Output)::ByteSize));
    std::copy(output.begin(), output.end(), Table.begin() + TableOutputSlot);
    const auto compiled = Compile(device, ChainProgram(), userData);
    ExpectNothingCaptured(compiled, "SRT pointer chain");
    const auto& result = *compiled.result;
    Expect(!result.unboundedReads, "SRT pointer chain: a table read through a pointer the program loads from its SRT makes its reads unbounded (" + Reads(result) + ")");
    ExpectRead(result, SrtAt(TableSlot), SrtAt(TableSlot + 2u), "SRT pointer chain: the pointer in the SRT");
    ExpectRead(result, TableAt(TableOutputSlot), TableAt(TableOutputSlot + 4u), "SRT pointer chain: the V# in the second table");
    Expect(Inside(result.readRanges, {{AddressOf(Srt.data()), AddressOf(Srt.data()) + decltype(Srt)::ByteSize}, {AddressOf(Table.data()), AddressOf(Table.data()) + decltype(Table)::ByteSize}}), "SRT pointer chain: the " + Reads(result) + " reach outside the two tables the program reads");
    const auto base = AddressOf(Output.data());
    Expect(!result.unboundedWrites && Contains(result.writeRanges, base, base + decltype(Output)::ByteSize), "SRT pointer chain: the store through the V# in the second table has the " + Writes(result));
    const auto outcome = Run(device, compiled, Height);
    ExpectClean(outcome, "SRT pointer chain");
    ExpectStored(outcome.words, Height, "SRT pointer chain");
}

void DynamicOffset(AgcDriver::VulkanDevice& device) {
    Srt.fill(0u);
    const auto userData = SrtUserData();
    const auto compiled = Compile(device, DynamicProgram(), userData);
    const auto& result = *compiled.result;
    Expect(result.unboundedReads, "SRT dynamic offset: a scalar read at a register offset from the SRT pointer was bounded (" + Reads(result) + ")");
    const auto outcome = Run(device, compiled, 1);
    ExpectClean(outcome, "SRT dynamic offset");
    ExpectStored(outcome.words, 1, "SRT dynamic offset");
}

void UserDataOffset(AgcDriver::VulkanDevice& device) {
    Srt.fill(0u);
    const auto pointer = SrtUserData();
    const std::array<std::uint32_t, 3> userData{pointer[0], pointer[1], OutputSlot * 4u};
    const auto compiled = Compile(device, UserDataOffsetProgram(), userData);
    const auto& result = *compiled.result;
    Expect(!result.unboundedReads, "SRT user-data offset: a scalar read at a register offset the user data sets makes its reads unbounded (" + Reads(result) + ")");
    ExpectRead(result, SrtAt(OutputSlot), SrtAt(OutputSlot + 4u), "SRT user-data offset: the s_load_dwordx4");
    Expect(Inside(result.readRanges, {{AddressOf(Srt.data()), AddressOf(Srt.data()) + decltype(Srt)::ByteSize}}), "SRT user-data offset: the " + Reads(result) + " reach outside the SRT the program reads");
    const auto outcome = Run(device, compiled, 1);
    ExpectClean(outcome, "SRT user-data offset");
    ExpectStored(outcome.words, 1, "SRT user-data offset");
}

void Run(AgcDriver::VulkanDevice& device) {
    WideReads(device);
    PointerChain(device);
    DynamicOffset(device);
    UserDataOffset(device);
}

}

int main() {
    return Main("SRT read ranges", Run);
}
