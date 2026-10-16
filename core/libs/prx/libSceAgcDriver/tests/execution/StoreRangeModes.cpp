#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include "GuestTestMemory.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Threads = 32;
constexpr std::uint32_t Stored = 0x00005678u;
constexpr std::uint32_t Offset = 8;
constexpr std::uint32_t IndexedStore = 0xe0702000u;
constexpr std::uint32_t UnindexedStore = 0xe0700000u;
constexpr std::uint32_t LaneIndex = 1u << 23u;

TestGuest::Array<std::uint32_t, 8> Code;
constexpr std::uint32_t WideStride = 8192;

TestGuest::Array<std::uint32_t, Threads * WideStride / 4u> Output;

std::array<std::uint32_t, 4> Descriptor(std::uint32_t stride, std::uint32_t records, std::uint32_t mode, std::uint32_t extra = 0u) {
    const auto address = reinterpret_cast<std::uintptr_t>(Output.data());
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (stride << 16u), records, 0x01016facu | mode << 28u | extra};
}

std::string Hex(std::uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(value));
    return text;
}

struct Compiled {
    ShaderRecompiler::RecompileResult result;
    std::uintptr_t address = 0;
};

Compiled Compile(AgcDriver::VulkanDevice& device, std::uint32_t store, const std::array<std::uint32_t, 4>& descriptor) {
    const std::array<std::uint32_t, 8> words{0x7e020300u, 0x7e0002ffu, Stored, store | Offset, 0x80000001u, 0xbf810000u, 0u, 0u};
    std::copy(words.begin(), words.end(), Code.begin());
    const std::vector<std::uint32_t> userData(descriptor.begin(), descriptor.end());
    const std::span<const std::uint32_t> code(Code.data(), 8u);
    const auto address = reinterpret_cast<std::uintptr_t>(code.data());
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{address, std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{Threads, 1, 1}, 0u, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, address, code, 0, {}},
        {32, 0, userData, compute, std::nullopt, std::nullopt, memory},
        device.Target(),
        {0, 0, 0, 128}
    };
    request.useCache = false;
    return {ShaderRecompiler::Recompile(request), address};
}

void ExpectRange(AgcDriver::VulkanDevice& device, const char* name, std::uint32_t store, const std::array<std::uint32_t, 4>& descriptor, std::uint64_t first, std::uint64_t end) {
    const auto compiled = Compile(device, store, descriptor);
    const auto base = reinterpret_cast<std::uintptr_t>(Output.data());
    const auto& ranges = compiled.result.writeRanges;
    Require(!compiled.result.unboundedWrites && ranges.size() == 1u && ranges[0].first == base + first && ranges[0].second == base + end, std::string("store range modes: ") + name + " has no write range [base + " + Hex(first) + ", base + " + Hex(end) + ")" + (compiled.result.unboundedWrites ? " (unbounded)" : ranges.empty() ? " (none)" : " (base + " + Hex(ranges[0].first - base) + ", base + " + Hex(ranges[0].second - base) + ")"));
}

void ExpectUnbounded(AgcDriver::VulkanDevice& device, const char* name, std::uint32_t store, const std::array<std::uint32_t, 4>& descriptor) {
    Require(Compile(device, store, descriptor).result.unboundedWrites, std::string("store range modes: ") + name + " has bounded writes");
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        ExpectRange(*device, "an indexed store checking index and offset", IndexedStore, Descriptor(16u, 4u, 0u), 0u, 4u * 16u + 16u);
        ExpectRange(*device, "an indexed store checking only its index", IndexedStore, Descriptor(16u, 4u, 1u), 0u, 3u * 16u + Offset + 16u);
        ExpectUnbounded(*device, "an indexed store checking only that records are non-zero", IndexedStore, Descriptor(16u, 4u, 2u));
        ExpectUnbounded(*device, "an indexed store with a stride checking only its offset", IndexedStore, Descriptor(32u, 16u, 3u));
        ExpectRange(*device, "an indexed store without a stride checking only its offset", IndexedStore, Descriptor(0u, 16u, 3u), 0u, 16u);
        ExpectRange(*device, "a store without an index checking only its offset", UnindexedStore, Descriptor(32u, 16u, 3u), 0u, 16u);
        ExpectRange(*device, "a store without an index adding the lane checking only its offset", UnindexedStore, Descriptor(32u, 16u, 3u, LaneIndex), 0u, 63u * 32u + 16u);
        std::fill(Output.begin(), Output.end(), 0u);
        const auto compiled = Compile(*device, IndexedStore, Descriptor(WideStride, 16u, 3u));
        device->Dispatch(compiled.result, 1, 1, 1, compiled.address);
        device->WaitIdle();
        for (std::uint32_t dword = 0; dword < Output.size(); ++dword) {
            const bool stored = dword % (WideStride / 4u) == Offset / 4u && dword / (WideStride / 4u) < Threads;
            Require(Output[dword] == (stored ? Stored : 0u), "store range modes: after an indexed store with a stride of " + std::to_string(WideStride) + " checking only its offset, dword " + std::to_string(dword) + " is " + Hex(Output[dword]) + (stored ? ", expected the store" : ", expected no store"));
        }
        std::puts("store range modes tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
