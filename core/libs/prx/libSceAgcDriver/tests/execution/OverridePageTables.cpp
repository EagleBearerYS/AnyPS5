#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/BdaPageTable.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Resources.hpp"
#include "prx/libSceAgcDriver/Graphics/include/UnitShadow.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include "GuestTestMemory.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace AgcDriver {

struct VulkanDeviceTestAccess {
    static Graphics::Context ContextOf(const VulkanDevice& device) { return device.graphicsContext(); }
};

}

#ifdef _WIN32

namespace {

using AgcDriver::Graphics::BdaPageTable;
using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;
namespace Abi = ShaderRecompiler::BdaAbi;
namespace Graphics = AgcDriver::Graphics;

constexpr std::uint64_t Unit = 65536;
constexpr std::uint32_t Units = 10;
constexpr std::uint64_t FakeDevice = 0x6000000000ull;
constexpr std::uint64_t FakeStride = 0x1000000ull;
constexpr std::uint32_t Threads = 32;
constexpr std::uint8_t ImportByte = 0x11;

int failures = 0;

void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

std::string Hex(std::uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(value));
    return text;
}

struct Translation {
    bool mapped = false;
    std::uint64_t device = 0;
    std::uint32_t permissions = 0;
    bool operator==(const Translation&) const = default;
};

Translation Through(const std::optional<Abi::Range>& range, std::uint64_t address) {
    if (!range) return {};
    return {true, range->deviceAddress + (address - range->begin), range->permissions};
}

Translation Spliced(const BdaPageTable& base, std::span<const Abi::Range> overrides, std::uint64_t address) {
    const auto found = std::upper_bound(overrides.begin(), overrides.end(), address, [](std::uint64_t value, const Abi::Range& range) { return value < range.begin; });
    if (found != overrides.begin() && address < std::prev(found)->end) return Through(*std::prev(found), address);
    return Through(base.Find(address), address);
}

std::string Describe(const Translation& translation) {
    return translation.mapped ? Hex(translation.device) + " permissions " + std::to_string(translation.permissions) : "unmapped";
}

void ExpectTranslations(const BdaPageTable& table, const BdaPageTable& base, std::span<const Abi::Range> overrides, std::span<const std::uint64_t> probes, const std::string& name) {
    std::size_t wrong = 0;
    std::string first;
    for (const auto address : probes) {
        const auto expected = Spliced(base, overrides, address);
        const auto actual = Through(table.Find(address), address);
        if (actual == expected) continue;
        if (wrong++ == 0u) first = Hex(address) + " translated to " + Describe(actual) + ", expected " + Describe(expected);
    }
    Expect(wrong == 0u, name + ": " + std::to_string(wrong) + " of " + std::to_string(probes.size()) + " probed addresses translated wrongly, first " + first);
}

void AddBoundaries(std::vector<std::uint64_t>& probes, std::span<const Abi::Range> ranges) {
    for (const auto& range : ranges) {
        for (const auto address : {range.begin - 1u, range.begin, range.end - 1u, range.end}) probes.push_back(address);
    }
}

Abi::Range Override(std::uint64_t begin, std::uint64_t end, std::uint32_t index, std::uint32_t permissions) {
    return {begin, end, FakeDevice + index * FakeStride, permissions, 0u};
}

std::vector<Translation> Translations(const BdaPageTable& table, std::span<const std::uint64_t> probes) {
    std::vector<Translation> translations;
    translations.reserve(probes.size());
    for (const auto address : probes) translations.push_back(Through(table.Find(address), address));
    return translations;
}

std::vector<VkDeviceAddress> Leaves(const BdaPageTable& table) {
    std::vector<VkDeviceAddress> leaves(Abi::WindowCount);
    for (std::uint32_t window = 0; window < Abi::WindowCount; ++window) leaves[window] = table.LeafAddress(window);
    return leaves;
}

class Reservation {
public:
    Reservation() {
        base = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(Units * Unit, Unit));
        Require(base != nullptr, "override page tables: cannot reserve guest units");
        GuestArena::GuestArenaCommit_nid_postfix(base, Units * Unit, PAGE_READWRITE, 16384);
        std::memset(base, ImportByte, Units * Unit);
    }

    ~Reservation() {
        for (const auto unit : registered) GuestAllocations::Mutation().Remove(base + unit * Unit);
        GuestArena::GuestArenaReset_nid_postfix(base, Units * Unit);
        GuestArena::GuestArenaRelease_nid_postfix(base, Units * Unit);
    }

    Reservation(const Reservation&) = delete;
    Reservation& operator=(const Reservation&) = delete;

    void Register(std::uint32_t unit, std::uint32_t units, bool writable) {
        GuestAllocations::Mutation().Add(base + unit * Unit, units * Unit, true, writable, true);
        registered.push_back(unit);
    }

    std::uint64_t Address(std::uint32_t unit, std::uint64_t offset = 0) const { return reinterpret_cast<std::uint64_t>(base) + unit * Unit + offset; }

private:
    std::uint8_t* base = nullptr;
    std::vector<std::uint32_t> registered;
};

std::optional<Abi::Range> Resident(const BdaPageTable& base, std::uint64_t address, std::uint64_t end, const std::string& name) {
    const auto found = base.Find(address);
    Expect(found.has_value() && found->begin == address && found->end == end, "override page tables: the base table does not hold " + name + " as one range from " + Hex(address) + " to " + Hex(end) + (found ? " (it holds " + Hex(found->begin) + " to " + Hex(found->end) + ")" : " (unmapped)"));
    return found;
}

void SplicesAndSharing(const Graphics::Context& context) {
    Reservation units;
    units.Register(0, 2, true);
    units.Register(3, 1, true);
    units.Register(4, 1, false);
    units.Register(6, 2, true);
    units.Register(9, 1, true);
    std::unique_lock gpu(AgcDriver::GuestMemory::GpuMutex());
    Graphics::GuestBufferMemory memory(context);
    memory.AcquireRegistered();
    memory.Upload();
    Expect(memory.HoldsLease(), "override page tables: the build holds no address space");
    const auto base = memory.PageTable(context);
    Require(base != nullptr, "override page tables: the address space built no base table");
    const auto r1 = Resident(*base, units.Address(0), units.Address(2), "the first two registered units");
    const auto r2 = Resident(*base, units.Address(3), units.Address(4), "the writable unit 3");
    const auto r3 = Resident(*base, units.Address(4), units.Address(5), "the read-only unit 4 beside it");
    const auto r4 = Resident(*base, units.Address(6), units.Address(8), "units 6 and 7");
    const auto r5 = Resident(*base, units.Address(9), units.Address(10), "unit 9");
    Require(r1 && r2 && r3 && r4 && r5, "override page tables: the registered units are not the expected resident ranges");
    Expect(r3->permissions == Abi::Read && r2->permissions == (Abi::Read | Abi::Write), "override page tables: the read-only unit's range is not read-only beside a writable one");
    for (const auto address : {units.Address(2), units.Address(5), units.Address(8, Unit - 1u)}) Expect(!base->Find(address).has_value(), "override page tables: an unregistered unit at " + Hex(address) + " is in the base table");

    const std::vector<Abi::Range> cuts{
        Override(r1->begin, r1->begin + 0x1234u, 0, Abi::Read),
        Override(r1->begin + 0x8010u, r1->begin + 0x18007u, 1, Abi::Read | Abi::Write),
        Override(r1->end - 0x321u, r1->end, 2, Abi::Read),
        Override(r2->begin + 0x4000u, r3->begin + 0x2000u, 3, Abi::Read | Abi::Write),
        Override(r4->begin + 0x100u, r4->begin + 0x900u, 4, Abi::Read),
        Override(r4->begin + 0x900u, r4->begin + 0x1100u, 5, Abi::Read | Abi::Write),
        Override(r5->begin, r5->end, 6, Abi::Read),
    };
    std::vector<std::uint64_t> every;
    for (auto address = units.Address(0) - 0x100u; address < units.Address(Units) + 0x100u; ++address) every.push_back(address);
    const auto baseEvery = Translations(*base, every);
    const auto spliced = memory.PageTableWith(context, cuts);
    Require(spliced != nullptr, "override page tables: no table for the override cuts");
    ExpectTranslations(*spliced, *base, cuts, every, "override page tables: cuts at a range's start, middle and end, over two adjacent ranges, two adjacent overrides and a whole range");
    Expect(Translations(*base, every) == baseEvery, "override page tables: the base table translates differently after the override table was built");

    const auto window = static_cast<std::uint32_t>(units.Address(0) >> Abi::WindowShift);
    Require(window == static_cast<std::uint32_t>((units.Address(Units) - 1u) >> Abi::WindowShift), "override page tables: the registered units straddle two page table windows");
    const auto other = (window + 1u) % Abi::WindowCount;
    const auto third = (window + 2u) % Abi::WindowCount;
    const auto start = [](std::uint32_t at) { return static_cast<std::uint64_t>(at) << Abi::WindowShift; };
    const std::vector<Abi::Range> overridesA{Override(r4->begin + 0x1000u, r4->begin + 0x3000u, 10, Abi::Read), Override(start(other) + 4u * Unit, start(other) + 6u * Unit, 11, Abi::Read | Abi::Write)};
    std::vector<Abi::Range> overridesB{Override(r4->begin + 0x5000u, r4->begin + 0x7000u, 10, Abi::Read), overridesA[1]};
    if (other < window) std::swap(overridesB[0], overridesB[1]);
    std::vector<Abi::Range> sortedA = overridesA;
    if (other < window) std::swap(sortedA[0], sortedA[1]);
    const std::vector<Abi::Range> overridesC{Override(start(third) + Unit, start(third) + 2u * Unit, 12, Abi::Read)};

    std::vector<std::uint64_t> pages;
    for (const auto at : {window, other, third}) {
        for (std::uint64_t page = 0; page < Abi::WindowPages; ++page) pages.push_back(start(at) + (page << Abi::PageShift));
    }
    for (auto address = units.Address(0); address < units.Address(Units); address += 16u) pages.push_back(address);
    AddBoundaries(pages, sortedA);
    AddBoundaries(pages, overridesB);
    AddBoundaries(pages, overridesC);
    AddBoundaries(pages, std::array{*r1, *r2, *r3, *r4, *r5});
    const auto baseLeaves = Leaves(*base);
    const auto basePages = Translations(*base, pages);
    const auto a = memory.PageTableWith(context, sortedA);
    const auto b = memory.PageTableWith(context, overridesB);
    const auto c = memory.PageTableWith(context, overridesC);
    Require(a != nullptr && b != nullptr && c != nullptr, "override page tables: an override table was not built");
    ExpectTranslations(*a, *base, sortedA, pages, "override page tables: table A");
    ExpectTranslations(*b, *base, overridesB, pages, "override page tables: table B, with A's override moved within its window");
    ExpectTranslations(*c, *base, overridesC, pages, "override page tables: table C, with one override in a third window");

    const auto leavesA = Leaves(*a);
    const auto leavesB = Leaves(*b);
    const auto leavesC = Leaves(*c);
    std::size_t aWrong = 0;
    std::size_t bWrong = 0;
    std::size_t cWrong = 0;
    for (std::uint32_t at = 0; at < Abi::WindowCount; ++at) {
        if (at == window || at == other) {
            aWrong += leavesA[at] != 0u && leavesA[at] != baseLeaves[at] ? 0u : 1u;
        } else {
            aWrong += leavesA[at] == baseLeaves[at] ? 0u : 1u;
        }
        if (at == window) {
            bWrong += leavesB[at] != 0u && leavesB[at] != leavesA[at] && leavesB[at] != baseLeaves[at] ? 0u : 1u;
        } else {
            bWrong += leavesB[at] == leavesA[at] ? 0u : 1u;
        }
        if (at == third) {
            cWrong += leavesC[at] != 0u && leavesC[at] != baseLeaves[at] ? 0u : 1u;
        } else {
            cWrong += leavesC[at] == baseLeaves[at] ? 0u : 1u;
        }
    }
    Expect(aWrong == 0u, "override page tables: table A has " + std::to_string(aWrong) + " windows that are not the base's leaves outside its two override windows, or not new leaves inside them");
    Expect(bWrong == 0u, "override page tables: table B has " + std::to_string(bWrong) + " windows that are not A's leaves outside the window whose override moved, or not a new leaf inside it");
    Expect(cWrong == 0u, "override page tables: table C has " + std::to_string(cWrong) + " windows that are not the base's leaves outside its override window, or not a new leaf inside it");
    Expect(memory.PageTable(context) == base && Leaves(*base) == baseLeaves, "override page tables: the base table or one of its leaves changed after the override tables were built");
    Expect(Translations(*base, pages) == basePages, "override page tables: the base table translates differently after tables A, B and C were built");
    memory.Release();
}

TestGuest::Array<std::uint32_t, 8> PointerReader{0x7e040200u, 0x7e060201u, 0xdc308000u, 0x017d0002u, 0xbf8c3f70u, 0xe0700000u, 0x80010100u, 0xbf810000u};
TestGuest::Array<std::uint32_t, 4> Output;

std::array<std::uint32_t, 4> BufferDescriptor(std::uint64_t address, std::uint32_t bytes) {
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, 0x31016facu};
}

std::uint32_t ReadThrough(AgcDriver::VulkanDevice& device, std::uint64_t pointer) {
    std::vector<std::uint32_t> userData{static_cast<std::uint32_t>(pointer), static_cast<std::uint32_t>(pointer >> 32u), 0u, 0u};
    const auto output = BufferDescriptor(reinterpret_cast<std::uintptr_t>(Output.data()), static_cast<std::uint32_t>(decltype(Output)::ByteSize));
    userData.insert(userData.end(), output.begin(), output.end());
    const std::span<const std::uint32_t> program(PointerReader.data(), PointerReader.size());
    const auto address = reinterpret_cast<std::uintptr_t>(program.data());
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{address, std::as_bytes(program)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{Threads, 1, 1}, 0u, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, address, program, 0, {}},
        {32, 0, userData, compute, std::nullopt, std::nullopt, memory},
        device.Target(),
        {0, 0, 0, 128}
    };
    request.useCache = false;
    const auto result = ShaderRecompiler::Recompile(request);
    Require(result.unboundedReads && !result.unboundedWrites, "override page tables: the pointer reader does not read through the page table");
    Output.fill(0u);
    device.Dispatch(result, 1, 1, 1, address);
    device.WaitIdle();
    return Output[0];
}

void Shadow(const Graphics::Context& context, const Graphics::HostImport& import, std::uint64_t unit, std::uint8_t value) {
    std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
    auto* recorder = Graphics::Recorder::Active();
    Require(recorder != nullptr, "override page tables: the driver opened no recorder");
    auto destination = Graphics::ShadowDestinationFor(context, import, unit, unit + Unit);
    Require(destination.has_value() && destination->seedUnits.empty(), "override page tables: no slab destination for a whole unit");
    auto pattern = std::make_shared<Graphics::Buffer>(context, static_cast<std::size_t>(Unit), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    std::memset(pattern->Bytes().data(), value, pattern->Bytes().size());
    const auto commands = recorder->Commands();
    recorder->Keep(pattern);
    recorder->Keep(destination->slab);
    Graphics::RecordMemoryBarrier(context, commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
    const VkBufferCopy piece{0, Graphics::SlabOffset(import, *destination->slab, unit), Unit};
    context.Function<PFN_vkCmdCopyBuffer>("vkCmdCopyBuffer")(commands, pattern->Handle(), destination->buffer, 1, &piece);
    Graphics::RecordMemoryBarrier(context, commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
    recorder->MarkCovered(VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
    const Graphics::ShadowedRange range{unit, unit + Unit, destination->slab};
    Graphics::MarkShadowed(import, std::span(&range, 1), AgcDriver::GuestMemory::TrackerGeneration());
}

void ExpectRead(AgcDriver::VulkanDevice& device, std::uint64_t pointer, std::uint8_t byte, const std::string& what) {
    const auto read = ReadThrough(device, pointer);
    const auto expected = 0x01010101u * byte;
    Expect(read == expected, "override page tables: " + what + " read " + Hex(read) + ", expected " + Hex(expected));
}

void ShadowSetChanges(AgcDriver::VulkanDevice& device, const Graphics::Context& context) {
    if (!Graphics::UnitShadowEnabled()) {
        std::puts("override page tables: unit shadows are off, the dispatch case is skipped");
        return;
    }
    Reservation units;
    units.Register(0, 4, true);
    const auto address = units.Address(0);
    const auto* import = Graphics::HostImportFor(context, address, 4u * Unit);
    Require(import != nullptr && AgcDriver::GuestMemory::Watched(address, 4u * Unit), "override page tables: the shadowed block is not imported and watched");
    AgcDriver::GuestMemory::CollectWritesUncached(address, 4u * Unit);
    const auto first = units.Address(1);
    const auto second = units.Address(2);
    ExpectRead(device, first + 64u, ImportByte, "the first unit before any shadow");
    Shadow(context, *import, first, 0x22);
    ExpectRead(device, first + 64u, 0x22, "the first unit through its shadow");
    ExpectRead(device, second + 64u, ImportByte, "the second unit beside the shadowed one");
    Shadow(context, *import, second, 0x33);
    ExpectRead(device, second + 64u, 0x33, "the second unit after it joined the shadow set");
    ExpectRead(device, first + 64u, 0x22, "the first unit after the shadow set grew");
    std::memset(reinterpret_cast<void*>(first), 0x55, static_cast<std::size_t>(Unit));
    ExpectRead(device, first + 64u, 0x55, "the first unit after a CPU store left the shadow set");
    ExpectRead(device, second + 64u, 0x33, "the second unit after the first left the shadow set");
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        const auto context = AgcDriver::VulkanDeviceTestAccess::ContextOf(*device);
        if (context.hostImportAlignment == 0 || !context.bufferDeviceAddress) {
            std::puts("override page tables: host imports or buffer device addresses unavailable");
            return VulkanTestSkipped;
        }
        SplicesAndSharing(context);
        ShadowSetChanges(*device, context);
    } catch (const std::exception& error) {
        Expect(false, std::string("exception: ") + error.what());
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d override page table checks failed\n", failures);
        return 1;
    }
    std::puts("override page table tests passed");
    return 0;
}

#else

int main() {
    return VulkanTestSkipped;
}

#endif
