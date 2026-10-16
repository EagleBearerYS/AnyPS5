#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "VulkanTestDevice.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
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
using Graphics::Require;

constexpr std::size_t Cell = 0x100000;
constexpr std::size_t RangeBytes = 4u * Cell;
constexpr std::size_t PieceOffset = Cell / 2u;
constexpr std::size_t PieceBytes = 0x4000;
constexpr std::size_t ProbeBytes = 0x4000;
constexpr auto PromptChange = std::chrono::milliseconds(1000);

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

class HostRange {
public:
    HostRange() {
        memory = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, RangeBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        Require(memory != nullptr, "pin wait cell imports: cannot allocate a host range");
        std::memset(memory, 0x3c, RangeBytes);
        GuestAllocations::Mutation().Add(memory, RangeBytes, true, true, true);
    }

    ~HostRange() {
        for (std::size_t offset = 0; offset < RangeBytes; offset += PieceBytes) {
            try {
                GuestAllocations::Mutation mutation;
                if (mutation.Covers(memory + offset, 1u)) mutation.Remove(memory + offset);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: pin wait cell imports: a range could not be removed at the end: %s\n", error.what());
                break;
            }
        }
        VirtualFree(memory, 0, MEM_RELEASE);
    }

    HostRange(const HostRange&) = delete;
    HostRange& operator=(const HostRange&) = delete;

    std::uint8_t* Data() const { return memory; }
    std::uint64_t Address(std::size_t offset = 0) const { return reinterpret_cast<std::uint64_t>(memory) + offset; }

private:
    std::uint8_t* memory = nullptr;
};

void ImportCells(const Graphics::Context& context, const HostRange& range, const std::string& what) {
    const auto first = Graphics::HostImportFor(context, range.Address(PieceOffset), ProbeBytes);
    const auto third = Graphics::HostImportFor(context, range.Address(2u * Cell + PieceOffset), ProbeBytes);
    Require(first != nullptr && third != nullptr, what + ": accesses in the first and third cells of the range were not imported");
    const auto firstSerial = Graphics::HostImportSerial(context, range.Address(PieceOffset), ProbeBytes, false);
    const auto thirdSerial = Graphics::HostImportSerial(context, range.Address(2u * Cell + PieceOffset), ProbeBytes, false);
    const auto whole = Graphics::HostImportSerial(context, range.Address(), RangeBytes, false);
    Require(firstSerial != 0u && thirdSerial != 0u && firstSerial != thirdSerial && whole == 0u, what + ": the accesses in two cells did not get two imports each covering less than the range (serials " + Hex(firstSerial) + " and " + Hex(thirdSerial) + ", whole range " + Hex(whole) + ")");
}

struct Piece {
    std::uint64_t address;
    std::size_t bytes;
    bool readable;
};

void ExpectLeased(const std::vector<Piece>& pieces, const std::string& what) {
    const auto lease = GuestAllocations::GuestAllocationsAcquire_nid_postfix();
    for (const auto& piece : pieces) {
        const bool found = std::any_of(lease.begin(), lease.end(), [&](const auto& range) { return range->address == piece.address && range->bytes == piece.bytes && range->readable == piece.readable; });
        Expect(found, what + ": a later lease does not hold the " + (piece.readable ? "readable" : "unreadable") + " piece " + Hex(piece.address) + "+" + Hex(piece.bytes));
    }
}

void ExpectNotLeased(std::uint64_t address, std::size_t bytes, const std::string& what) {
    const auto lease = GuestAllocations::GuestAllocationsAcquire_nid_postfix();
    const bool held = std::any_of(lease.begin(), lease.end(), [&](const auto& entry) { return entry->address < address + bytes && address < entry->address + entry->bytes; });
    Expect(!held, what);
}

std::chrono::milliseconds Timed(const std::function<void()>& change, std::string& failure) {
    const auto start = std::chrono::steady_clock::now();
    try {
        change();
    } catch (const std::exception& error) {
        failure = error.what();
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
}

void ProtectInsideCell(const Graphics::Context& context) {
    HostRange range;
    const std::string what = "a guest protection of 16 KiB inside the first 1 MiB cell of a 4 MiB registered range to no access, with imports of its first and third cells and no build running";
    ImportCells(context, range, what);
    std::string failure;
    const auto elapsed = Timed([&] { GuestAllocations::Mutation().Protect(range.Data() + PieceOffset, PieceBytes, false, false, false, [] {}); }, failure);
    Expect(failure.empty(), what + ": the protection failed after " + std::to_string(elapsed.count()) + " ms: " + failure);
    Expect(elapsed < PromptChange, what + ": the protection took " + std::to_string(elapsed.count()) + " ms, expected under " + std::to_string(PromptChange.count()) + " ms");
    if (!failure.empty()) return;
    ExpectLeased({{range.Address(), PieceOffset, true}, {range.Address(PieceOffset + PieceBytes), RangeBytes - PieceOffset - PieceBytes, true}}, what);
    ExpectNotLeased(range.Address(PieceOffset), PieceBytes, what + ": a later lease holds the protected piece");
    const auto piece = GuestAllocations::GuestAllocationsLookup_nid_postfix(static_cast<std::uintptr_t>(range.Address(PieceOffset)), false);
    Expect(piece != nullptr && piece->address == range.Address(PieceOffset) && piece->bytes == PieceBytes && !piece->readable && !piece->writable && !piece->gpu, what + ": the registry does not hold the protected piece alone, without access and GPU mapping");
}

void UnmapInsideCell(const Graphics::Context& context) {
    HostRange range;
    const std::string what = "a guest unmap of 16 KiB inside the first 1 MiB cell of a 4 MiB registered range, with imports of its first and third cells and no build running";
    ImportCells(context, range, what);
    std::string failure;
    const auto elapsed = Timed([&] { GuestAllocations::Mutation().Unmap(range.Data() + PieceOffset, PieceBytes, [](const void*, std::size_t, const void*, bool) {}); }, failure);
    Expect(failure.empty(), what + ": the unmap failed after " + std::to_string(elapsed.count()) + " ms: " + failure);
    Expect(elapsed < PromptChange, what + ": the unmap took " + std::to_string(elapsed.count()) + " ms, expected under " + std::to_string(PromptChange.count()) + " ms");
    if (!failure.empty()) return;
    ExpectLeased({{range.Address(), PieceOffset, true}, {range.Address(PieceOffset + PieceBytes), RangeBytes - PieceOffset - PieceBytes, true}}, what);
    ExpectNotLeased(range.Address(PieceOffset), PieceBytes, what + ": a later lease still holds the unmapped piece");
}

}

#endif

int main() {
#ifdef _WIN32
    _putenv_s("APS5_PIN_WAIT_MS", "2000");
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        const auto context = AgcDriver::VulkanDeviceTestAccess::ContextOf(*device);
        if (context.hostImportAlignment == 0) {
            std::puts("pin wait cell imports: host imports unavailable");
            return VulkanTestSkipped;
        }
        Graphics::SetHostImportRangeWindow(Cell);
        ProtectInsideCell(context);
        UnmapInsideCell(context);
    } catch (const std::exception& error) {
        Expect(false, std::string("exception: ") + error.what());
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d pin wait cell import checks failed\n", failures);
        return 1;
    }
    std::puts("pin wait cell import tests passed");
    return 0;
#else
    return VulkanTestSkipped;
#endif
}
