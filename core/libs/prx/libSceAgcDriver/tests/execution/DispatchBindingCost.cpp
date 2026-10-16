#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp"
#include "prx/libSceAgcDriver/Submit/include/Acb.hpp"
#include "prx/libSceAgcDriver/Submit/include/Dcb.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libc/include/GuestWriteWatch.hpp"
#include "prx/libc/include/Shutdown.hpp"
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
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::uint32_t Lanes = 64;
constexpr std::uint32_t ComputeQueue = 0x20;
constexpr std::uint32_t MaxTextures = 2048;
constexpr std::uint32_t Stride = 32;
constexpr std::uint32_t SurfaceBytes = 256;
constexpr std::uint32_t Format8888UNorm = 56;
constexpr std::uint32_t Type2D = 9;
constexpr std::size_t PlainOffset = 0;
constexpr std::size_t AddressOffset = 512;
constexpr std::size_t SrtOffset = 4096;
constexpr std::size_t InputOffset = 8192;
constexpr std::size_t OutputOffset = 12288;
constexpr std::size_t GlobalInOffset = 16384;
constexpr std::size_t GlobalOutOffset = 20480;
constexpr std::size_t HeapOffset = 65536;
constexpr std::size_t BlockBytes = (HeapOffset + static_cast<std::size_t>(MaxTextures) * Stride + 65535) & ~static_cast<std::size_t>(65535);
constexpr std::size_t SurfaceBlockBytes = static_cast<std::size_t>(MaxTextures) * SurfaceBytes;
constexpr std::uint32_t Unwritten = 0xcafebabeu;

constexpr std::array<std::uint32_t, 25> PlainCode{
    0xf4080100u, 0xfa000000u, 0xf4080200u, 0xfa000010u, 0xf4080300u, 0xfa000020u, 0xf4080700u, 0xfa000030u,
    0x34080082u, 0x7e200500u, 0x8f108510u, 0xf42c0502u, 0x20000000u, 0xbf8cc07fu,
    0xe0301000u, 0x80030504u, 0xf09c0f08u, 0x00450000u, 0xbf8c3f70u, 0x4a000105u,
    0xe0701000u, 0x80070004u, 0xbf810000u, 0u, 0u};

constexpr std::array<std::uint32_t, 33> AddressCode{
    0xf4080100u, 0xfa000000u, 0xf4080200u, 0xfa000010u, 0xf4080300u, 0xfa000020u, 0xf4080700u, 0xfa000030u,
    0xf4080800u, 0xfa000040u, 0x34080082u, 0x7e200500u, 0x8f108510u, 0xf42c0502u, 0x20000000u, 0xbf8cc07fu,
    0xe0301000u, 0x80030504u, 0xdc308000u, 0x06200004u, 0xf09c0f08u, 0x00450000u, 0xbf8c3f70u, 0x4a000105u,
    0x4a0c0c81u, 0xe0701000u, 0x80070004u, 0xdc708000u, 0x00220604u, 0xbf810000u, 0u, 0u, 0u};

class GuestBlock {
public:
    GuestBlock(std::size_t bytes, bool gpu) : bytes(bytes) {
#ifdef _WIN32
        block = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(bytes, 65536));
        if (block != nullptr) GuestArena::GuestArenaCommit_nid_postfix(block, bytes, PAGE_READWRITE, bytes);
#else
        constexpr std::uintptr_t alignment = 65536;
        void* mapped = mmap(nullptr, bytes + alignment, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapped != MAP_FAILED) {
            const auto begin = reinterpret_cast<std::uintptr_t>(mapped);
            const auto aligned = (begin + alignment - 1) & ~(alignment - 1);
            if (aligned != begin) munmap(mapped, aligned - begin);
            if (aligned + bytes != begin + bytes + alignment) munmap(reinterpret_cast<void*>(aligned + bytes), begin + alignment - aligned);
            block = reinterpret_cast<std::uint8_t*>(aligned);
            GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(block, bytes);
        }
#endif
        if (block == nullptr) throw std::runtime_error("dispatch binding cost: cannot allocate the guest block");
        std::memset(block, 0, bytes);
        GuestAllocations::Mutation().Add(block, bytes, true, true, gpu);
    }
    GuestBlock(const GuestBlock&) = delete;
    GuestBlock& operator=(const GuestBlock&) = delete;
    std::uint64_t At(std::size_t offset) const { return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(block + offset)); }
    std::uint32_t* Words(std::size_t offset) const { return reinterpret_cast<std::uint32_t*>(block + offset); }

private:
    std::size_t bytes;
    std::uint8_t* block = nullptr;
};

void check(bool condition, const std::string& what) {
    if (!condition) throw std::runtime_error("dispatch binding cost: " + what);
}

std::array<std::uint32_t, 4> V(std::uint64_t address, std::uint32_t stride, std::uint32_t records) {
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (stride << 16u), records, stride == 0u ? 0x31016facu : 0x01016facu};
}

void Prepare(const GuestBlock& block, const GuestBlock& surfaces, std::uint32_t textures) {
    std::memcpy(block.Words(PlainOffset), PlainCode.data(), sizeof(PlainCode));
    std::memcpy(block.Words(AddressOffset), AddressCode.data(), sizeof(AddressCode));
    for (std::uint32_t texture = 0; texture < MaxTextures; ++texture) {
        const auto surface = surfaces.At(static_cast<std::size_t>(texture) * SurfaceBytes);
        for (std::uint32_t word = 0; word < SurfaceBytes / 4u; ++word) surfaces.Words(static_cast<std::size_t>(texture) * SurfaceBytes)[word] = texture * 0x01010101u;
        const std::array<std::uint32_t, 8> words{static_cast<std::uint32_t>(surface >> 8u), static_cast<std::uint32_t>((surface >> 40u) & 0xffu) | (Format8888UNorm << 20u) | (3u << 30u), 7u << 14u, 0xfacu | (Type2D << 28u), 0u, 0u, 0u, 0u};
        std::memcpy(block.Words(HeapOffset + static_cast<std::size_t>(texture) * Stride), words.data(), sizeof(words));
    }
    auto* srt = block.Words(SrtOffset);
    std::fill(srt, srt + 64, 0u);
    const auto heap = V(block.At(HeapOffset), Stride, textures);
    const auto input = V(block.At(InputOffset), 0u, Lanes * 4u);
    const auto output = V(block.At(OutputOffset), 0u, Lanes * 4u);
    std::copy(heap.begin(), heap.end(), srt);
    std::copy(input.begin(), input.end(), srt + 8);
    std::copy(output.begin(), output.end(), srt + 12);
    const auto globalIn = block.At(GlobalInOffset);
    const auto globalOut = block.At(GlobalOutOffset);
    srt[16] = static_cast<std::uint32_t>(globalIn);
    srt[17] = static_cast<std::uint32_t>(globalIn >> 32u);
    srt[18] = static_cast<std::uint32_t>(globalOut);
    srt[19] = static_cast<std::uint32_t>(globalOut >> 32u);
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
        block.Words(InputOffset)[lane] = lane;
        block.Words(GlobalInOffset)[lane] = 0x1000u + lane;
    }
}

std::vector<std::uint32_t> Pm4(std::uint32_t opcode, std::initializer_list<std::uint32_t> body) {
    std::vector<std::uint32_t> words{0xc0000000u | (static_cast<std::uint32_t>(body.size() - 1) << 16u) | (opcode << 8u)};
    words.insert(words.end(), body);
    return words;
}

std::vector<std::uint32_t> Commands(const GuestBlock& block, bool address) {
    const auto kernel = block.At(address ? AddressOffset : PlainOffset);
    const auto srt = block.At(SrtOffset);
    std::vector<std::uint32_t> words;
    for (const auto& packet : {Pm4(0x76, {0x20c, static_cast<std::uint32_t>(kernel >> 8u)}), Pm4(0x76, {0x20d, static_cast<std::uint32_t>(kernel >> 40u) & 0xffu}), Pm4(0x76, {0x207, Lanes}), Pm4(0x76, {0x208, 1}), Pm4(0x76, {0x209, 1}), Pm4(0x76, {0x213, 2u << 1u}), Pm4(0x76, {0x240, static_cast<std::uint32_t>(srt)}), Pm4(0x76, {0x241, static_cast<std::uint32_t>(srt >> 32u)}), Pm4(0x15, {1, 1, 1, 0x41})}) {
        words.insert(words.end(), packet.begin(), packet.end());
    }
    return words;
}

void Submit(const std::vector<std::uint32_t>& words) {
    Packet packet{const_cast<std::uint32_t*>(words.data()), static_cast<std::uint32_t>(words.size()), 0, {}};
    check(sceAgcDriverSubmitAcb(ComputeQueue, &packet) == 0, "submission failed");
    AgcDriverWaitIdle_nid_postfix();
}

template <typename Ready>
void WaitUntil(Ready ready, const std::string& what) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!ready()) {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("dispatch binding cost: timed out waiting for " + what);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void CheckAddressOutput(const GuestBlock& block, std::uint32_t round) {
    auto* out = block.Words(GlobalOutOffset);
    WaitUntil([&] {
        AgcDriver::GuestMemory::FlushGpuWrites(block.At(GlobalOutOffset), Lanes * 4u);
        for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
            if (std::atomic_ref<std::uint32_t>(out[lane]).load(std::memory_order_acquire) == Unwritten) return false;
        }
        return true;
    }, "the address-based store of round " + std::to_string(round));
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
        const auto expected = block.Words(GlobalInOffset)[lane] + 1u;
        check(out[lane] == expected, "round " + std::to_string(round) + ": global output lane " + std::to_string(lane) + " is " + std::to_string(out[lane]) + ", expected " + std::to_string(expected));
    }
}

void Rounds(const GuestBlock& block, bool address, std::uint32_t rounds) {
    const auto words = Commands(block, address);
    for (std::uint32_t round = 0; round < rounds; ++round) {
        auto* out = block.Words(GlobalOutOffset);
        for (std::uint32_t lane = 0; lane < Lanes; ++lane) out[lane] = Unwritten;
        block.Words(GlobalInOffset)[round % Lanes] += 7u;
        Submit(words);
        if (address) CheckAddressOutput(block, round);
    }
}

int Cost(int argc, char** argv) {
    if (argc < 6) {
        std::cerr << "usage: --cost <plain|address> <textures> <unchanged|changed> <seconds> [gpu-surfaces]\n";
        return 2;
    }
    const bool address = std::string(argv[2]) == "address";
    const auto textures = static_cast<std::uint32_t>(std::strtoul(argv[3], nullptr, 10));
    const bool changed = std::string(argv[4]) == "changed";
    const auto seconds = std::strtod(argv[5], nullptr);
    check(textures >= 1 && textures <= MaxTextures, "the texture count must be 1.." + std::to_string(MaxTextures));
    const bool gpuSurfaces = argc > 6 && std::string(argv[6]) == "gpu-surfaces";
    GuestBlock block(BlockBytes, true);
    GuestBlock surfaces(SurfaceBlockBytes, gpuSurfaces);
    Prepare(block, surfaces, textures);
    const auto words = Commands(block, address);
    for (std::uint32_t warm = 0; warm < 50; ++warm) Submit(words);
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    constexpr std::uint32_t Window = 2000;
    std::uint64_t total = 0;
    auto windowStart = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() < end) {
        if (changed) block.Words(InputOffset)[total % Lanes] ^= 1u;
        Submit(words);
        if (total % 64 == 63) AgcDriver::GuestMemory::FlushGpuWrites(block.At(OutputOffset), Lanes * 4u);
        if (++total % Window == 0) {
            const auto now = std::chrono::steady_clock::now();
            std::printf("dispatch binding cost: %s %u textures %s: %.1f us per submitted dispatch (round trip, %u dispatches)\n", address ? "address" : "plain", textures, changed ? "changed" : "unchanged", std::chrono::duration<double, std::micro>(now - windowStart).count() / Window, Window);
            std::fflush(stdout);
            windowStart = now;
        }
    }
    std::printf("dispatch binding cost: %llu dispatches, resource cache %zu entries\n", static_cast<unsigned long long>(total), AgcDriver::Graphics::SharedResourceCache().Size());
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(0);
}

}

int main(int argc, char** argv) {
    try {
        {
            const auto probe = OpenVulkanTestDevice();
            if (!probe) return VulkanTestSkipped;
        }
        if (argc > 1 && std::string(argv[1]) == "--cost") return Cost(argc, argv);
        GuestBlock block(BlockBytes, true);
        GuestBlock surfaces(SurfaceBlockBytes, false);
        Prepare(block, surfaces, 16);
        Rounds(block, false, 4);
        Rounds(block, true, 6);
        std::puts("dispatch binding cost: plain and address-based rounds dispatched, the address-based stores read their inputs");
        LibcRunShutdown_nid_postfix();
        std::puts("dispatch binding cost tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        try { LibcRunShutdown_nid_postfix(); } catch (...) {}
        return 1;
    }
}
