#include "prx/libc/include/GuestArena.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {

constexpr std::size_t Page = 0x4000;
#ifdef _WIN32
constexpr std::uint32_t ReadWrite = PAGE_READWRITE;
#else
constexpr std::uint32_t ReadWrite = 0x04;
#endif

GuestArena::GuestArenaUsage Usage() {
    GuestArena::GuestArenaUsage usage{};
    GuestArena::GuestArenaUsage_nid_no_patch(&usage);
    return usage;
}

long long Delta(std::uint64_t now, std::uint64_t start) {
    return static_cast<long long>(now - start);
}

void Expect(const GuestArena::GuestArenaUsage& start, long long privatePages, long long sharedPages, long long directPages, long long reservedPages, const char* step) {
    const auto now = Usage();
    const long long seen[] = {Delta(now.privateBytes, start.privateBytes), Delta(now.sharedBytes, start.sharedBytes), Delta(now.directBytes, start.directBytes), Delta(now.reservedBytes, start.reservedBytes)};
    const long long wanted[] = {privatePages, sharedPages, directPages, reservedPages};
    for (std::size_t kind = 0; kind < 4; ++kind) {
        if (seen[kind] == wanted[kind] * static_cast<long long>(Page)) continue;
        std::fprintf(stderr, "%s: private %+lld, shared %+lld, direct %+lld, reserved %+lld bytes since the start, expected %+lld, %+lld, %+lld, %+lld pages of 0x%zx bytes\n", step, seen[0], seen[1], seen[2], seen[3], wanted[0], wanted[1], wanted[2], wanted[3], Page);
        std::abort();
    }
    std::printf("%s: private %+lld, shared %+lld, direct %+lld, reserved %+lld pages\n", step, wanted[0], wanted[1], wanted[2], wanted[3]);
}

unsigned char* Allocate(std::size_t pages) {
    return static_cast<unsigned char*>(GuestArena::GuestArenaAllocate_nid_postfix(Page * pages, Page));
}

}

int main() {
    if (!GuestArena::GuestArenaAvailable_nid_postfix()) {
        std::printf("skipped, the guest arena is unavailable on this host\n");
        return 77;
    }
    const auto start = Usage();

    auto* block = Allocate(8);
    Expect(start, 0, 0, 0, 8, "allocate 8 pages");
    GuestArena::GuestArenaCommit_nid_postfix(block, Page * 4, ReadWrite, Page);
    Expect(start, 4, 0, 0, 8, "commit 4 of them");
    GuestArena::GuestArenaCommit_nid_postfix(block, Page * 4, ReadWrite, Page);
    Expect(start, 4, 0, 0, 8, "commit the same 4 pages again");
    GuestArena::GuestArenaCommit_nid_postfix(block, Page * 6, ReadWrite, Page);
    Expect(start, 6, 0, 0, 8, "commit 6 pages over them");
    GuestArena::GuestArenaReset_nid_postfix(block + Page * 2, Page * 2);
    Expect(start, 4, 0, 0, 8, "reset 2 pages in the middle");
    GuestArena::GuestArenaReset_nid_postfix(block, Page * 8);
    Expect(start, 0, 0, 0, 8, "reset the whole block");
    GuestArena::GuestArenaRelease_nid_postfix(block, Page * 8);
    Expect(start, 0, 0, 0, 0, "release the block");

    auto* unclaimed = reinterpret_cast<unsigned char*>(GuestArena::GuestArenaHighestFree_nid_no_patch(Page * 2, Page));
    GuestArena::GuestArenaCommit_nid_postfix(unclaimed, Page * 2, ReadWrite, Page);
#ifdef _WIN32
    Expect(start, 2, 0, 0, 0, "commit 2 unclaimed pages");
    GuestArena::GuestArenaReset_nid_postfix(unclaimed, Page * 2);
    Expect(start, 0, 0, 0, 0, "reset them");
#else
    Expect(start, 2, 0, 0, 2, "commit 2 unclaimed pages, which claims them");
    GuestArena::GuestArenaReset_nid_postfix(unclaimed, Page * 2);
    Expect(start, 0, 0, 0, 2, "reset them");
#endif
    GuestArena::GuestArenaRelease_nid_postfix(unclaimed, Page * 2);
    Expect(start, 0, 0, 0, 0, "release them");

#ifdef _WIN32
    const auto section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_EXECUTE_READWRITE | SEC_RESERVE, 0, static_cast<DWORD>(Page * 4), nullptr);
    const auto physical = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_EXECUTE_READWRITE | SEC_RESERVE, 0, static_cast<DWORD>(Page * 2), nullptr);
    if (section == nullptr || physical == nullptr) {
        std::fprintf(stderr, "CreateFileMappingW failed: %lu\n", GetLastError());
        return 1;
    }
    auto* shared = Allocate(2);
    GuestArena::GuestArenaMap_nid_postfix(shared, Page * 2, section, 0, PAGE_READWRITE);
    Expect(start, 0, 2, 0, 2, "map 2 shared pages");
    auto* resident = Allocate(2);
    GuestArena::GuestArenaMapDirect_nid_postfix(resident, Page * 2, physical, 0, PAGE_READWRITE);
    Expect(start, 0, 2, 2, 4, "map 2 pages of direct memory once");
    auto* alias = Allocate(2);
    GuestArena::GuestArenaMapDirect_nid_postfix(alias, Page * 2, physical, 0, PAGE_READWRITE);
    Expect(start, 0, 2, 4, 6, "map the same direct memory a second time");
    auto* remapped = Allocate(2);
    GuestArena::GuestArenaCommit_nid_postfix(remapped, Page * 2, PAGE_READWRITE, Page);
    Expect(start, 2, 2, 4, 8, "commit 2 more pages");
    GuestArena::GuestArenaMap_nid_postfix(remapped, Page * 2, section, Page * 2, PAGE_READWRITE);
    Expect(start, 0, 4, 4, 8, "map shared pages over them");
    GuestArena::GuestArenaReset_nid_postfix(resident, Page * 2);
    Expect(start, 0, 4, 2, 8, "reset the first direct mapping");
    GuestArena::GuestArenaReset_nid_postfix(alias, Page * 2);
    GuestArena::GuestArenaReset_nid_postfix(shared, Page * 2);
    GuestArena::GuestArenaReset_nid_postfix(remapped, Page * 2);
    Expect(start, 0, 0, 0, 8, "reset every mapping");
    for (auto* mapped : {shared, resident, alias, remapped}) GuestArena::GuestArenaRelease_nid_postfix(mapped, Page * 2);
    Expect(start, 0, 0, 0, 0, "release every block");
    GuestArena::GuestArenaForgetSection_nid_postfix(section);
    GuestArena::GuestArenaForgetSection_nid_postfix(physical);
    CloseHandle(section);
    CloseHandle(physical);
#endif

    std::printf("guest arena committed bytes checks passed\n");
    return 0;
}
