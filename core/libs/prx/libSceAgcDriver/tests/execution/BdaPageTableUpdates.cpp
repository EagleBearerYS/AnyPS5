#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/BdaPageTable.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libc/include/GuestWriteWatch.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::BdaPageTable;
using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Threads = 64;
constexpr std::size_t BlockBytes = 2 * 65536;
constexpr std::uint32_t Fill = 0xcdcdcdcdu;
constexpr std::uint32_t LoadBase = 0xfe00;
constexpr std::uint32_t OutputBase = 0x18000;

alignas(256) std::array<std::uint32_t, 20> Program{};

std::span<const std::uint32_t> Code() {
    const std::vector<std::uint32_t> code{0x34040084, 0x4a0404ff, LoadBase, 0x34080081, 0x36080882, 0x4a040504, 0x34060083, 0x4a0606ff, OutputBase, 0xdc388ff8, 0x04000002, 0xbf8c3f70, 0xdc708000, 0x00000503, 0xdc708004, 0x00000703, 0xbf810000};
    std::copy(code.begin(), code.end(), Program.begin());
    return std::span<const std::uint32_t>(Program.data(), code.size());
}

class GuestBlock {
public:
    GuestBlock() {
#ifdef _WIN32
        block = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(BlockBytes, 65536));
        if (block != nullptr) GuestArena::GuestArenaCommit_nid_postfix(block, BlockBytes, PAGE_READWRITE, BlockBytes);
#else
        constexpr std::uintptr_t alignment = 65536;
        void* mapped = mmap(nullptr, BlockBytes + alignment, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapped != MAP_FAILED) {
            const auto begin = reinterpret_cast<std::uintptr_t>(mapped);
            const auto aligned = (begin + alignment - 1) & ~(alignment - 1);
            if (aligned != begin) munmap(mapped, aligned - begin);
            if (aligned + BlockBytes != begin + BlockBytes + alignment) munmap(reinterpret_cast<void*>(aligned + BlockBytes), begin + alignment - aligned);
            block = reinterpret_cast<std::uint8_t*>(aligned);
            GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(block, BlockBytes);
        }
#endif
        Require(block != nullptr, "BDA page table updates: cannot allocate a guest block");
        GuestAllocations::Mutation().Add(block, BlockBytes, true, true, true);
    }

    ~GuestBlock() {
        GuestAllocations::Mutation().Remove(block);
#ifdef _WIN32
        GuestArena::GuestArenaReset_nid_postfix(block, BlockBytes);
        GuestArena::GuestArenaRelease_nid_postfix(block, BlockBytes);
#else
        munmap(block, BlockBytes);
        GuestWriteWatch::GuestWriteWatchUnregister_nid_postfix(block, BlockBytes);
#endif
    }

    GuestBlock(const GuestBlock&) = delete;
    GuestBlock& operator=(const GuestBlock&) = delete;

    std::uint8_t* Data() const { return block; }

private:
    std::uint8_t* block = nullptr;
};

std::string Hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08x", value);
    return text;
}

std::vector<std::uint32_t> Initial(std::uint32_t seed) {
    std::vector<std::uint32_t> memory(BlockBytes / 4u, Fill);
    for (std::uint32_t dword = (LoadBase - 0x100u) / 4u; dword < (LoadBase + 0x500u) / 4u; ++dword) memory[dword] = seed + dword * 0x00010203u;
    return memory;
}

std::uint32_t Bytes(const std::vector<std::uint32_t>& memory, std::uint32_t address) {
    std::uint32_t value = 0;
    for (std::uint32_t byte = 0; byte < 4u; ++byte) {
        const std::uint32_t at = address + byte;
        value |= ((memory[at / 4u] >> (at % 4u * 8u)) & 0xffu) << (byte * 8u);
    }
    return value;
}

void Run(AgcDriver::VulkanDevice& device, const GuestBlock& guest, std::uint32_t seed, const std::string& name) {
    const auto code = Code();
    const auto initial = Initial(seed);
    std::memcpy(guest.Data(), initial.data(), BlockBytes);
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(guest.Data()));
    const std::vector<std::uint32_t> userData{static_cast<std::uint32_t>(address), static_cast<std::uint32_t>(address >> 32u)};
    const std::array<ShaderRecompiler::MemoryRegion, 1> regions{{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{Threads, 1, 1}, 0, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
        {32, 0, userData, compute, std::nullopt, std::nullopt, regions},
        device.Target(),
        {0, 0, 0, 128}
    };
    request.useCache = false;
    const auto result = ShaderRecompiler::Recompile(request);
    device.Dispatch(result, 1, 1, 1, reinterpret_cast<std::uintptr_t>(code.data()));
    device.WaitIdle();
    std::vector<std::uint32_t> memory(BlockBytes / 4u);
    std::memcpy(memory.data(), guest.Data(), BlockBytes);
    auto expected = initial;
    for (std::uint32_t tid = 0; tid < Threads; ++tid) {
        const std::uint32_t load = LoadBase + tid * 16u + (tid & 1u) * 2u - 8u;
        expected[OutputBase / 4u + tid * 2u] = Bytes(initial, load + 4u);
        expected[OutputBase / 4u + tid * 2u + 1u] = Bytes(initial, load + 12u);
    }
    for (std::size_t dword = 0; dword < memory.size(); ++dword) {
        Require(memory[dword] == expected[dword], "BDA page table updates: " + name + ": byte " + std::to_string(dword * 4u) + " is " + Hex(memory[dword]) + ", expected " + Hex(expected[dword]));
    }
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        const auto start = BdaPageTable::Totals();
        auto first = std::make_unique<GuestBlock>();
        Run(*device, *first, 0x51000000u, "first block");
        const auto built = BdaPageTable::Totals();
        Require(built.tables > start.tables && built.encodedWindows > start.encodedWindows, "BDA page table updates: a dispatch over registered memory built no page table");
        Run(*device, *first, 0x52000000u, "first block again");
        Require(BdaPageTable::Totals().tables == built.tables, "BDA page table updates: an unchanged address space built its page table again");
        auto second = std::make_unique<GuestBlock>();
        Run(*device, *second, 0x53000000u, "second block");
        const auto added = BdaPageTable::Totals();
        Require(added.tables > built.tables, "BDA page table updates: a registered block did not update the page table");
        first.reset();
        Run(*device, *second, 0x54000000u, "second block after the first was replaced");
        Require(BdaPageTable::Totals().tables > added.tables, "BDA page table updates: a removed block did not update the page table");
        std::puts("BDA page table update tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
