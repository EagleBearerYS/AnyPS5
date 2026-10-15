#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
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
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Threads = 32;
constexpr std::uint32_t Stored = 0x00005678u;
constexpr std::uint32_t Records = 4;
constexpr std::uint32_t Stride = 16;
constexpr std::uint32_t Offset = 8;

TestGuest::Array<std::uint32_t, 8> Code{0x7e020300u, 0x7e0002ffu, Stored, 0xe0702000u | Offset, 0x80000001u, 0xbf810000u, 0u, 0u};
TestGuest::Array<std::uint32_t, 64> Output;
TestGuest::Array<std::uint32_t, 64> Other;

std::array<std::uint32_t, 4> IndexOnlyDescriptor(const void* data) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (Stride << 16u), Records, 0x11016facu};
}

std::string Hex(std::uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(value));
    return text;
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        std::fill(Output.begin(), Output.end(), 0u);
        std::fill(Other.begin(), Other.end(), 0u);
        std::vector<std::uint32_t> userData(4, 0u);
        const auto output = IndexOnlyDescriptor(Output.data());
        std::copy(output.begin(), output.end(), userData.begin());
        const std::span<const std::uint32_t> code(Code.data(), 8u);
        const auto address = reinterpret_cast<std::uintptr_t>(code.data());
        const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{address, std::as_bytes(code)}}};
        const ShaderRecompiler::ShaderComputeStageInfo compute{{Threads, 1, 1}, 0u, {false, false, false}, false, 1};
        ShaderRecompiler::RecompileRequest request{
            {ShaderStage::Compute, address, code, 0, {}},
            {32, 0, userData, compute, std::nullopt, std::nullopt, memory},
            device->Target(),
            {0, 0, 0, 128}
        };
        request.useCache = false;
        const auto result = ShaderRecompiler::Recompile(request);
        const auto base = reinterpret_cast<std::uintptr_t>(Output.data());
        const auto end = base + (Records - 1u) * Stride + Offset + 16u;
        Require(result.bdaWrites && !result.unboundedWrites, "indexed store ranges: an indexed store with a fixed offset through an index-checked V# has no bounded write range");
        Require(result.writeRanges.size() == 1u && result.writeRanges[0].first == base && result.writeRanges[0].second == end, "indexed store ranges: the write range is not [" + Hex(base) + ", " + Hex(end) + ")");
        device->Dispatch(result, 1, 1, 1, address);
        auto* recorder = AgcDriver::Graphics::Recorder::Active();
        Require(recorder != nullptr, "indexed store ranges: the driver opened no recorder");
        Require(recorder->PendingWriteOverlaps(base, decltype(Output)::ByteSize), "indexed store ranges: the dispatch's stores are not pending");
        Require(!recorder->PendingWriteOverlaps(reinterpret_cast<std::uintptr_t>(Other.data()), decltype(Other)::ByteSize), "indexed store ranges: memory outside the dispatch's V# is pending");
        device->WaitIdle();
        for (std::uint32_t dword = 0; dword < Output.size(); ++dword) {
            const bool stored = dword % (Stride / 4u) == Offset / 4u && dword / (Stride / 4u) < Records;
            Require(Output[dword] == (stored ? Stored : 0u), "indexed store ranges: dword " + std::to_string(dword) + " is " + Hex(Output[dword]) + (stored ? ", expected the store" : ", expected no store"));
        }
        std::puts("indexed store ranges tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
