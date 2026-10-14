#include "DepthFastClearHarness.hpp"
#include "VulkanTestDevice.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

extern "C" {
int APS5_VABI sceKernelAllocateDirectMemory(std::int64_t, std::int64_t, std::size_t, std::size_t, int, std::int64_t*);
int APS5_VABI sceKernelMapDirectMemory(void**, std::size_t, int, int, std::int64_t, std::size_t);
int APS5_VABI sceKernelReleaseDirectMemory(std::int64_t, std::size_t);
int APS5_VABI sceKernelMunmap(void*, std::size_t);
}

namespace {

using AgcDriver::Graphics::Require;
using DepthFastClearHarness::Covered;
using DepthFastClearHarness::Draw;
using DepthFastClearHarness::Height;
using DepthFastClearHarness::Surface;
using DepthFastClearHarness::Width;

constexpr std::uint32_t Threads = 32;
constexpr std::uint32_t Format32Float = 22;
constexpr std::uint32_t Type2D = 9;
constexpr std::size_t DirectBytes = 2u << 20u;
constexpr std::size_t DirectAlignment = 1u << 16u;
constexpr std::size_t ProbeOffset = DirectBytes - 4096u;
constexpr int CpuGpuReadWrite = 0x33;
constexpr std::size_t OutputBytes = 1u << 16u;
constexpr float DrawnDepth = 0.5f;

alignas(256) constexpr std::array<std::uint32_t, 7> CopyBufferCode{
    0x34060082u, 0xe0301000u, 0x80010a03u, 0xbf8c3f70u, 0xe0701000u, 0x80000a03u, 0xbf810000u,
};

alignas(256) constexpr std::array<std::uint32_t, 10> LoadPlaneCode{
    0x7e3c0300u, 0x7e3e0280u, 0x7e140280u, 0xf0001108u, 0x00010a1eu, 0xbf8c3f70u, 0x34060082u, 0xe0701000u, 0x80000a03u, 0xbf810000u,
};

class DirectMemory {
public:
    explicit DirectMemory(bool aliased) {
        Require(sceKernelAllocateDirectMemory(0, 0x7fffffffffll, DirectBytes, DirectAlignment, 3, &physical) == 0, "depth alias read: cannot allocate direct memory");
        try {
            first = map("first");
            second = aliased ? map("second") : first;
        } catch (...) {
            release();
            throw;
        }
    }

    ~DirectMemory() { release(); }

    DirectMemory(const DirectMemory&) = delete;
    DirectMemory& operator=(const DirectMemory&) = delete;

    std::uint8_t* First() const { return static_cast<std::uint8_t*>(first); }
    std::uint8_t* Second() const { return static_cast<std::uint8_t*>(second); }

private:
    void* map(const char* which) {
        void* address = nullptr;
        Require(sceKernelMapDirectMemory(&address, DirectBytes, CpuGpuReadWrite, 0, physical, DirectAlignment) == 0, std::string("depth alias read: cannot map the ") + which + " view of the direct memory");
        return address;
    }

    void release() noexcept {
        if (second != nullptr && second != first) sceKernelMunmap(second, DirectBytes);
        if (first != nullptr) sceKernelMunmap(first, DirectBytes);
        if (physical >= 0) sceKernelReleaseDirectMemory(physical, DirectBytes);
        first = second = nullptr;
        physical = -1;
    }

    std::int64_t physical = -1;
    void* first = nullptr;
    void* second = nullptr;
};

class GuestBlock {
public:
    GuestBlock() {
#ifdef _WIN32
        block = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, OutputBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
        block = static_cast<std::uint8_t*>(std::aligned_alloc(65536, OutputBytes));
#endif
        Require(block != nullptr, "depth alias read: cannot allocate the output block");
        std::memset(block, 0, OutputBytes);
        GuestAllocations::Mutation().Add(block, OutputBytes, true, true, true);
    }

    ~GuestBlock() {
        GuestAllocations::Mutation().Remove(block);
#ifdef _WIN32
        VirtualFree(block, 0, MEM_RELEASE);
#else
        std::free(block);
#endif
    }

    GuestBlock(const GuestBlock&) = delete;
    GuestBlock& operator=(const GuestBlock&) = delete;

    std::span<std::uint32_t> Words() { return {reinterpret_cast<std::uint32_t*>(block), Threads}; }

private:
    std::uint8_t* block = nullptr;
};

std::array<std::uint32_t, 4> BufferDescriptor(const void* data, std::uint32_t bytes) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, 0x31016facu};
}

std::array<std::uint32_t, 8> TextureDescriptor(const void* data) {
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(data));
    return {
        static_cast<std::uint32_t>(address >> 8u),
        static_cast<std::uint32_t>((address >> 40u) & 0xffu) | (Format32Float << 20u) | (((Width - 1u) & 3u) << 30u),
        ((Width - 1u) >> 2u) | ((Height - 1u) << 14u),
        0xfacu | (Type2D << 28u),
        0u, 0u, 0u, 0u,
    };
}

ShaderRecompiler::RecompileResult Compile(AgcDriver::VulkanDevice& device, std::span<const std::uint32_t> code, std::span<const std::uint32_t> userData) {
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{Threads, 1, 1}, 0u, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderRecompiler::ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
        {32, 0, userData, compute, std::nullopt, std::nullopt, memory},
        device.Target(),
        {0, 0, 0, 128}
    };
    request.useCache = false;
    return ShaderRecompiler::Recompile(request);
}

std::vector<std::uint32_t> Run(AgcDriver::VulkanDevice& device, std::span<const std::uint32_t> code, std::span<const std::uint32_t> userData, std::span<std::uint32_t> output) {
    std::fill(output.begin(), output.end(), 0xdeadbeefu);
    const auto program = Compile(device, code, userData);
    device.Dispatch(program, 1, 1, 1, {}, reinterpret_cast<std::uintptr_t>(code.data()));
    device.WaitIdle();
    return {output.begin(), output.end()};
}

std::vector<std::uint32_t> ReadBuffer(AgcDriver::VulkanDevice& device, const std::uint8_t* source, std::span<std::uint32_t> output) {
    std::vector<std::uint32_t> userData(16, 0u);
    const auto destination = BufferDescriptor(output.data(), static_cast<std::uint32_t>(output.size_bytes()));
    const auto plane = BufferDescriptor(source, Threads * sizeof(std::uint32_t));
    std::copy(destination.begin(), destination.end(), userData.begin());
    std::copy(plane.begin(), plane.end(), userData.begin() + 4);
    return Run(device, CopyBufferCode, userData, output);
}

std::vector<std::uint32_t> ReadImage(AgcDriver::VulkanDevice& device, const std::uint8_t* plane, std::span<std::uint32_t> output) {
    std::vector<std::uint32_t> userData(16, 0u);
    const auto destination = BufferDescriptor(output.data(), static_cast<std::uint32_t>(output.size_bytes()));
    const auto texture = TextureDescriptor(plane);
    std::copy(destination.begin(), destination.end(), userData.begin());
    std::copy(texture.begin(), texture.end(), userData.begin() + 4);
    return Run(device, LoadPlaneCode, userData, output);
}

std::uint32_t Matching(const std::vector<std::uint32_t>& values, float expected) {
    return static_cast<std::uint32_t>(std::count(values.begin(), values.end(), std::bit_cast<std::uint32_t>(expected)));
}

std::string Describe(const std::vector<std::uint32_t>& values) {
    char text[96];
    std::snprintf(text, sizeof(text), "%u of %u lanes read %g; lane 0 read 0x%08x (%g)", Matching(values, DrawnDepth), Threads, static_cast<double>(DrawnDepth), values.front(), static_cast<double>(std::bit_cast<float>(values.front())));
    return text;
}

}

int main(int argc, char** argv) {
    const bool aliased = !(argc > 1 && std::string_view(argv[1]) == "--single");
    try {
        DirectMemory memory(aliased);
        GuestBlock output;
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        auto* depth = memory.First();
        auto* read = memory.Second();
        Require(aliased == (read != depth), "the second view is not where the mode puts it");
        const std::uint32_t probe = 0x5eed5eedu;
        std::memcpy(depth + ProbeOffset, &probe, sizeof(probe));
        Require(std::memcmp(read + ProbeOffset, &probe, sizeof(probe)) == 0, "a CPU store through the depth view is not seen through the read view");
        const auto shaders = DepthFastClearHarness::Compile(*device);
        const Surface surface{reinterpret_cast<std::uintptr_t>(depth), 0, 0, false, VK_FORMAT_D32_SFLOAT};
        Require(Draw(*device, shaders, surface, {.depthWrite = true, .depthCompare = VK_COMPARE_OP_ALWAYS}) == Covered, "the depth producer did not cover the surface");
        const auto buffer = ReadBuffer(*device, read, output.Words());
        const auto image = ReadImage(*device, depth, output.Words());
        const auto stored = ReadBuffer(*device, read + ProbeOffset, output.Words());
        float cpu = 0.0f;
        std::memcpy(&cpu, read, sizeof(cpu));
        std::printf("%s mapping: depth drawn at %p, buffer read at %p: %s\n", aliased ? "aliased" : "single", static_cast<void*>(depth), static_cast<void*>(read), Describe(buffer).c_str());
        std::printf("control, a storage image read of the depth plane at %p: %s\n", static_cast<void*>(depth), Describe(image).c_str());
        std::printf("control, a buffer read at %p of the CPU's store through %p: 0x%08x\n", static_cast<void*>(read + ProbeOffset), static_cast<void*>(depth + ProbeOffset), stored.front());
        std::printf("the CPU reads %g at %p after the draw and the dispatches\n", static_cast<double>(cpu), static_cast<void*>(read));
        Require(Matching(image, DrawnDepth) == Threads, "the storage image read of the depth plane does not see the drawn depth, so the depth draw did not happen");
        Require(stored.front() == probe, "a buffer read through the read view does not see a CPU store made through the depth view, so the buffer read itself fails");
        Require(Matching(buffer, DrawnDepth) == Threads, std::string("a buffer read ") + (aliased ? "through a second mapping of the depth plane's memory" : "of the depth plane's own memory") + " does not see the drawn depth: " + Describe(buffer));
        std::puts("depth alias read test passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
