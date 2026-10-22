#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Texture.hpp"
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
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;
namespace GuestMemory = AgcDriver::GuestMemory;

constexpr std::size_t BlockBytes = 0x10000;
constexpr std::size_t ElsewhereOffset = 0x8000;
constexpr std::uint32_t TexelWidth = 64;
constexpr std::uint32_t TexelHeight = 4;
constexpr std::size_t ImageBytes = TexelWidth * TexelHeight * 4u;
constexpr std::uint32_t Format32Float = 22;
constexpr std::uint32_t Type2D = 9;
constexpr std::uint32_t Stored = 0x12345678u;

alignas(256) constexpr std::array<std::uint32_t, 7> StoreCode{
    0x7e040300u, 0x7e060280u, 0x7e0202ffu, Stored, 0xf0200108u, 0x00020102u, 0xbf810000u,
};

std::array<std::uint32_t, 4> Image(const void* texels) {
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(texels));
    return {static_cast<std::uint32_t>(address >> 8u), static_cast<std::uint32_t>((address >> 40u) & 0xffu) | (Format32Float << 20u) | (((TexelWidth - 1u) & 3u) << 30u), ((TexelWidth - 1u) >> 2u) | ((TexelHeight - 1u) << 14u) | (1u << 31u), 0xfacu | (Type2D << 28u)};
}

class GuestBlock {
public:
    GuestBlock() {
#ifdef _WIN32
        base = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(BlockBytes, BlockBytes));
        if (base != nullptr) GuestArena::GuestArenaCommit_nid_postfix(base, BlockBytes, PAGE_READWRITE, BlockBytes);
#else
        void* mapped = mmap(nullptr, 2u * BlockBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapped != MAP_FAILED) {
            const auto begin = reinterpret_cast<std::uintptr_t>(mapped);
            const auto aligned = (begin + BlockBytes - 1) & ~(static_cast<std::uintptr_t>(BlockBytes) - 1);
            if (aligned != begin) munmap(mapped, aligned - begin);
            if (aligned + BlockBytes != begin + 2u * BlockBytes) munmap(reinterpret_cast<void*>(aligned + BlockBytes), begin + BlockBytes - aligned);
            base = reinterpret_cast<std::uint8_t*>(aligned);
            GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(base, BlockBytes);
        }
#endif
        Require(base != nullptr, "storage block pieces: cannot allocate guest memory");
        std::memset(base, 0, BlockBytes);
        GuestAllocations::Mutation mutation;
        mutation.Add(base, BlockBytes, true, true, true);
    }

    ~GuestBlock() {
        {
            GuestAllocations::Mutation mutation;
            mutation.Remove(base);
        }
#ifdef _WIN32
        GuestArena::GuestArenaReset_nid_postfix(base, BlockBytes);
        GuestArena::GuestArenaRelease_nid_postfix(base, BlockBytes);
#else
        GuestWriteWatch::GuestWriteWatchUnregister_nid_postfix(base, BlockBytes);
        munmap(base, BlockBytes);
#endif
    }

    GuestBlock(const GuestBlock&) = delete;
    GuestBlock& operator=(const GuestBlock&) = delete;

    std::uint8_t* base = nullptr;
};

bool ElsewhereInTheBlock(AgcDriver::VulkanDevice& device) {
    GuestBlock block;
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(block.base));
    GuestMemory::CollectWritesUncached(address, BlockBytes);
    std::memset(block.base + ElsewhereOffset, 0x5a, 64);
    std::vector<std::uint32_t> userData(16, 0u);
    const auto image = Image(block.base);
    std::copy(image.begin(), image.end(), userData.begin() + 8);
    const std::span<const std::uint32_t> code(StoreCode);
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{8, 1, 1}, 0u, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
        {32, 0, userData, compute, std::nullopt, std::nullopt, memory},
        device.Target(),
        {0, 0, 0, 128}
    };
    request.useCache = false;
    const auto store = ShaderRecompiler::Recompile(request);
    device.Dispatch(store, 1, 1, 1, {}, reinterpret_cast<std::uintptr_t>(code.data()));
    device.WaitIdle();
    Require(AgcDriver::Graphics::StorageTexture::FindPending(address, ImageBytes) != nullptr, "storage block pieces: the store left no pending storage image");
    const bool watched = GuestMemory::CollectWritesUncached(address + ElsewhereOffset, 4096u) != 0u;
#ifdef _WIN32
    if (!watched) {
        std::puts("storage block pieces skipped: host imports of private memory are compared here, not watched");
        return false;
    }
#endif
    Require(watched, "storage block pieces: the block left the write watch");
    std::size_t images = 0;
    std::size_t evicted = 0;
    const bool kept = AgcDriver::Graphics::StorageTexture::AccessKeptByCpu(address, ImageBytes, &images, &evicted);
    Require(images == 1u, "storage block pieces: the read did not find the pending storage image");
    Require(!kept && evicted == 0u, "storage block pieces: a read of a storage image smaller than its block took a CPU write elsewhere in the block for one over the image (kept " + std::to_string(kept) + ", images evicted " + std::to_string(evicted) + ")");
    GuestMemory::FlushGpuWrites(address, ImageBytes);
    std::uint32_t texel = 0;
    std::memcpy(&texel, block.base, sizeof(texel));
    Require(texel == Stored, "storage block pieces: the stored texel did not reach guest memory");
    std::uint8_t elsewhere = 0;
    std::memcpy(&elsewhere, block.base + ElsewhereOffset, sizeof(elsewhere));
    Require(elsewhere == 0x5au, "storage block pieces: the CPU's bytes elsewhere in the block were overwritten");
    return true;
}

}

int main() {
    try {
#ifndef _WIN32
        setenv("APS5_WRITE_WATCH_IMPORTS", "watch", 1);
#endif
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        if (!GuestMemory::WriteWatched()) {
            std::puts("storage block pieces skipped: no write watch");
            return VulkanTestSkipped;
        }
        const bool tested = ElsewhereInTheBlock(*device);
        device->WaitIdle();
        if (!tested) return VulkanTestSkipped;
        std::puts("storage block pieces tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
