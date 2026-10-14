#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <windows.h>
#include <psapi.h>

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message.c_str());
        std::exit(1);
    }
}

long long PrivateBytes() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    Require(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)) != 0, "cannot read the process memory counters");
    return static_cast<long long>(counters.PrivateUsage);
}

void Register(void* block, std::size_t bytes) {
    auto* mutation = GuestAllocations::GuestAllocationsBegin_nid_postfix();
    GuestAllocations::GuestAllocationsAdd_nid_postfix(mutation, block, bytes, true, true, true);
    GuestAllocations::GuestAllocationsEnd_nid_postfix(mutation);
}

void Unregister(void* block) {
    auto* mutation = GuestAllocations::GuestAllocationsBegin_nid_postfix();
    GuestAllocations::GuestAllocationsRemove_nid_postfix(mutation, block);
    GuestAllocations::GuestAllocationsEnd_nid_postfix(mutation);
}

}

int main() {
    namespace GuestMemory = AgcDriver::GuestMemory;
    Require(GuestArena::GuestArenaAvailable_nid_postfix(), "the guest arena is unavailable");
    std::uintptr_t arenaBase = 0;
    std::size_t arenaBytes = 0;
    GuestArena::GuestArenaRange_nid_postfix(&arenaBase, &arenaBytes);
    constexpr std::size_t bytes = 1u << 16u;
    constexpr std::size_t page = 4096;
    auto* low = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(bytes, bytes));
    auto* high = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocateAtOrAbove_nid_postfix(arenaBase + arenaBytes / 2, bytes, bytes));
    auto* reserved = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocateAtOrAbove_nid_postfix(arenaBase + arenaBytes / 4 * 3, bytes, bytes));
    for (auto* block : {low, high}) {
        GuestArena::GuestArenaCommit_nid_postfix(block, bytes, PAGE_READWRITE, bytes);
        Register(block, bytes);
    }
    const auto before = PrivateBytes();
    Require(GuestMemory::Accessible(low, bytes, true) && GuestMemory::Accessible(high, bytes, true), "committed arena pages are not writable");
    const auto grown = PrivateBytes() - before;
    Require(grown < (4ll << 20), "the arena page-state table committed " + std::to_string(grown >> 10) + " KiB to record two 64 KiB ranges of a " + std::to_string(arenaBytes >> 30) + " GiB arena");
    Require(!GuestMemory::Accessible(reserved, page), "a reserved arena page whose table chunk was never recorded counts as accessible");
    DWORD previous = 0;
    Require(VirtualProtect(high, page, PAGE_NOACCESS, &previous) != 0, "cannot revoke the test page behind the registry");
    Require(GuestMemory::Accessible(high, page, true), "a recorded page was queried again instead of read from the page-state table");
    GuestAllocations::GuestAllocationsInvalidate_nid_postfix(reinterpret_cast<std::uintptr_t>(high), bytes);
    Require(!GuestMemory::Accessible(high, page), "an invalidated page kept its recorded access");
    Require(VirtualProtect(high, page, PAGE_READWRITE, &previous) != 0, "cannot restore the test page");
    GuestAllocations::GuestAllocationsInvalidate_nid_postfix(reinterpret_cast<std::uintptr_t>(high), bytes);
    Require(GuestMemory::Accessible(high, bytes, true), "a restored page is not writable");
    for (auto* block : {low, high}) Unregister(block);
    std::printf("guest page table: two recorded ranges of a %zu GiB arena committed %lld KiB of table\n", arenaBytes >> 30, grown >> 10);
    return 0;
}
