#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/BdaPageTable.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "VulkanTestDevice.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace AgcDriver {

struct VulkanDeviceTestAccess {
    static Graphics::Context ContextOf(const VulkanDevice& device) { return device.graphicsContext(); }
};

}

#ifdef _WIN32

namespace {

namespace Graphics = AgcDriver::Graphics;
namespace Abi = ShaderRecompiler::BdaAbi;
using Graphics::Require;

constexpr std::uint64_t Unit = 65536;
constexpr std::uint64_t ProbeBytes = 4096;
constexpr std::uint64_t ImportWindowBytes = std::uint64_t{8} << 20u;
constexpr std::uint32_t HostUnits = static_cast<std::uint32_t>(3u * ImportWindowBytes / Unit);

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

class Block {
public:
    enum class Backing { Arena, Host };

    explicit Block(std::uint32_t units, Backing backing = Backing::Arena, std::uintptr_t hint = 0) : bytes(units * Unit), backing(backing), lengths(units, 0u) {
        if (backing == Backing::Host) {
            base = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        } else {
            base = static_cast<std::uint8_t*>(hint != 0 ? GuestArena::GuestArenaAllocateAtOrAbove_nid_postfix(hint, bytes, Unit) : GuestArena::GuestArenaAllocate_nid_postfix(bytes, Unit));
            if (base != nullptr) GuestArena::GuestArenaCommit_nid_postfix(base, bytes, PAGE_READWRITE, 16384);
        }
        Require(base != nullptr, "address space reuse: cannot reserve guest units");
        std::memset(base, 0x5a, bytes);
    }

    ~Block() {
        for (std::uint32_t unit = 0; unit < lengths.size(); ++unit) {
            if (lengths[unit] == 0u) continue;
            try {
                GuestAllocations::Mutation().Remove(base + unit * Unit);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: address space reuse: a unit could not be removed: %s\n", error.what());
            }
        }
        if (backing == Backing::Host) {
            VirtualFree(base, 0, MEM_RELEASE);
            return;
        }
        GuestArena::GuestArenaReset_nid_postfix(base, bytes);
        GuestArena::GuestArenaRelease_nid_postfix(base, bytes);
    }

    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;

    void Map(std::uint32_t unit, std::uint32_t units) {
        GuestAllocations::Mutation().Add(base + unit * Unit, units * Unit, true, true, true);
        lengths[unit] = units;
    }

    void Unmap(std::uint32_t unit) {
        GuestAllocations::Mutation().Remove(base + unit * Unit);
        lengths[unit] = 0u;
    }

    std::uint64_t Address(std::uint32_t unit, std::uint64_t offset = 0) const { return reinterpret_cast<std::uint64_t>(base) + unit * Unit + offset; }
    std::uint32_t Window(std::uint32_t unit = 0) const { return static_cast<std::uint32_t>(Address(unit) >> Abi::WindowShift); }

private:
    std::uint8_t* base = nullptr;
    std::uint64_t bytes = 0;
    Backing backing = Backing::Arena;
    std::vector<std::uint32_t> lengths;
};

std::vector<VkDeviceAddress> Leaves(const Graphics::BdaPageTable& table) {
    std::vector<VkDeviceAddress> leaves(Abi::WindowCount);
    for (std::uint32_t window = 0; window < Abi::WindowCount; ++window) leaves[window] = table.LeafAddress(window);
    return leaves;
}

struct Built {
    std::shared_ptr<const Graphics::BdaPageTable> table;
    std::vector<VkDeviceAddress> leaves;
    Graphics::BdaPageTable::Counts before;
    Graphics::BdaPageTable::Counts after;
};

Built Build(const Graphics::Context& context) {
    Built built;
    std::unique_lock gpu(AgcDriver::GuestMemory::GpuMutex());
    built.before = Graphics::BdaPageTable::Totals();
    Graphics::GuestBufferMemory memory(context);
    memory.AcquireRegistered();
    memory.Upload();
    built.table = memory.PageTable(context);
    built.after = Graphics::BdaPageTable::Totals();
    if (built.table != nullptr) built.leaves = Leaves(*built.table);
    memory.Release();
    return built;
}

std::uint64_t Serial(const Graphics::Context& context, std::uint64_t address) {
    return Graphics::HostImportSerial(context, address, ProbeBytes, false);
}

bool ImportsSurviveRebuilds(const Graphics::Context& context) {
    Graphics::SetHostImportWindow(ImportWindowBytes);
    Block units(HostUnits, Block::Backing::Host);
    const auto windowStart = (units.Address(8) + ImportWindowBytes - 1u) & ~(ImportWindowBytes - 1u);
    const auto run = static_cast<std::uint32_t>((windowStart - units.Address(0)) / Unit);
    constexpr std::uint32_t Kept = 0;
    constexpr std::uint32_t Remapped = 3;
    constexpr std::uint32_t Elsewhere = 6;
    constexpr auto Half = static_cast<std::uint32_t>(ImportWindowBytes / 2u / Unit);
    const auto grownUnit = run;
    const auto adjacentUnit = run + Half;
    units.Map(Kept, 1);
    units.Map(Remapped, 1);
    units.Map(grownUnit, Half);
    static_cast<void>(Build(context));
    const auto kept = Serial(context, units.Address(Kept));
    const auto remapped = Serial(context, units.Address(Remapped));
    const auto grown = Serial(context, units.Address(grownUnit));
    if (kept == 0u || remapped == 0u || grown == 0u) return false;
    Expect(kept != remapped && remapped != grown && kept != grown, "address space reuse: three separate ranges share an import serial");

    units.Map(Elsewhere, 1);
    static_cast<void>(Build(context));
    const std::string elsewhere = "a rebuild after a new range was mapped elsewhere";
    Expect(Serial(context, units.Address(Kept)) == kept, elsewhere + ": the unchanged range at " + Hex(units.Address(Kept)) + " lost its import (serial " + Hex(kept) + " became " + Hex(Serial(context, units.Address(Kept))) + ")");
    Expect(Serial(context, units.Address(Remapped)) == remapped, elsewhere + ": the unchanged range at " + Hex(units.Address(Remapped)) + " lost its import");
    Expect(Serial(context, units.Address(grownUnit)) == grown, elsewhere + ": the unchanged range at " + Hex(units.Address(grownUnit)) + " lost its import");
    Expect(Serial(context, units.Address(Elsewhere)) != 0u, elsewhere + ": the new range at " + Hex(units.Address(Elsewhere)) + " was not imported");

    units.Unmap(Remapped);
    units.Map(Remapped, 1);
    static_cast<void>(Build(context));
    const std::string again = "a rebuild after one range was unmapped and mapped again";
    const auto replaced = Serial(context, units.Address(Remapped));
    Expect(replaced != 0u && replaced != remapped, again + ": the range mapped again kept its old import (serial " + Hex(remapped) + ", now " + Hex(replaced) + ")");
    Expect(Serial(context, units.Address(Kept)) == kept, again + ": the unchanged range at " + Hex(units.Address(Kept)) + " lost its import");
    Expect(Serial(context, units.Address(grownUnit)) == grown, again + ": the unchanged range at " + Hex(units.Address(grownUnit)) + " lost its import");

    units.Map(adjacentUnit, Half);
    static_cast<void>(Build(context));
    const std::string adjacent = "a rebuild after a new range was mapped right after an imported one, the two filling one import window";
    const auto regrown = Serial(context, units.Address(grownUnit));
    Expect(regrown != 0u && regrown != grown, adjacent + ": the import of the range whose run grew was kept (serial " + Hex(grown) + ", now " + Hex(regrown) + ")");
    Expect(Serial(context, units.Address(adjacentUnit)) != 0u, adjacent + ": the new adjacent range was not imported");
    Expect(Serial(context, units.Address(Kept)) == kept, adjacent + ": the unchanged range at " + Hex(units.Address(Kept)) + " lost its import");
    Expect(Serial(context, units.Address(Remapped)) == replaced, adjacent + ": the unchanged range at " + Hex(units.Address(Remapped)) + " lost its import");
    return true;
}

void WindowsShared(const Graphics::Context& context) {
    Block home(2);
    home.Map(0, 1);
    std::uintptr_t arenaBase = 0;
    std::size_t arenaBytes = 0;
    GuestArena::GuestArenaRange_nid_postfix(&arenaBase, &arenaBytes);
    const auto nextWindow = (static_cast<std::uint64_t>(home.Window()) + 1u) << Abi::WindowShift;
    if (nextWindow + 4u * Unit > arenaBase + arenaBytes) {
        std::printf("address space reuse: the guest arena ends in the window of its allocations, page table window sharing skipped\n");
        return;
    }
    Block remote(4, Block::Backing::Arena, static_cast<std::uintptr_t>(nextWindow));
    if (remote.Window() == home.Window() || remote.Window() != remote.Window(3)) {
        std::printf("address space reuse: no allocation in another page table window, window sharing skipped\n");
        return;
    }
    remote.Map(0, 1);
    const auto first = Build(context);
    Require(first.table != nullptr, "address space reuse: the address space built no page table");
    Expect(first.leaves[home.Window()] != 0u && first.leaves[remote.Window()] != 0u, "address space reuse: the page table has no leaf for the window " + std::to_string(home.Window()) + " or " + std::to_string(remote.Window()) + " of the mapped ranges");

    remote.Map(2, 1);
    const auto second = Build(context);
    Require(second.table != nullptr, "address space reuse: the rebuilt address space built no page table");
    const std::string what = "a rebuild after a new range was mapped in window " + std::to_string(remote.Window()) + " only";
    std::uint32_t windows = 0;
    std::uint32_t changed = 0;
    std::string firstChanged;
    for (std::uint32_t window = 0; window < Abi::WindowCount; ++window) {
        if (second.leaves[window] != 0u) ++windows;
        if (window == remote.Window()) continue;
        if (second.leaves[window] == first.leaves[window]) continue;
        if (changed++ == 0u) firstChanged = "window " + std::to_string(window) + " (" + Hex(first.leaves[window]) + " became " + Hex(second.leaves[window]) + ")";
    }
    Expect(changed == 0u, what + ": " + std::to_string(changed) + " other windows got new leaves, first " + firstChanged);
    Expect(second.leaves[remote.Window()] != 0u && second.leaves[remote.Window()] != first.leaves[remote.Window()], what + ": the changed window kept its old leaf");
    const auto encoded = second.after.encodedWindows - second.before.encodedWindows;
    const auto shared = second.after.sharedWindows - second.before.sharedWindows;
    Expect(encoded == 1u, what + ": the rebuild encoded " + std::to_string(encoded) + " windows, expected 1");
    Expect(shared + 1u == windows, what + ": the rebuild shared " + std::to_string(shared) + " windows of the table's " + std::to_string(windows) + ", expected " + std::to_string(windows - 1u));
    Expect(Build(context).leaves == second.leaves, "address space reuse: a build without a mapping change got other leaves than the one before it");
}

}

#endif

int main() {
#ifdef _WIN32
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        const auto context = AgcDriver::VulkanDeviceTestAccess::ContextOf(*device);
        if (context.hostImportAlignment == 0 || !context.bufferDeviceAddress) {
            std::puts("address space reuse: host imports or buffer device addresses unavailable");
            return VulkanTestSkipped;
        }
        if (!ImportsSurviveRebuilds(context)) {
            std::puts("skipped, the device does not import the guest units");
            return VulkanTestSkipped;
        }
        WindowsShared(context);
    } catch (const std::exception& error) {
        Expect(false, std::string("exception: ") + error.what());
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d address space reuse checks failed\n", failures);
        return 1;
    }
    std::puts("address space reuse tests passed");
    return 0;
#else
    return VulkanTestSkipped;
#endif
}
