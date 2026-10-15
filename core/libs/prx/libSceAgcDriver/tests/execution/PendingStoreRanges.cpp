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
constexpr std::uint32_t Stored = 0x00001234u;

TestGuest::Array<std::uint32_t, 8> Code{0x7e0002ffu, Stored, 0xe0700000u, 0x80000000u, 0xbf810000u, 0u, 0u, 0u};
TestGuest::Array<std::uint32_t, 64> Output;
TestGuest::Array<std::uint32_t, 64> Other;

std::array<std::uint32_t, 4> BufferDescriptor(const void* data, std::uint32_t bytes) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, 0x31016facu};
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        std::fill(Output.begin(), Output.end(), 0u);
        std::fill(Other.begin(), Other.end(), 0u);
        std::vector<std::uint32_t> userData(4, 0u);
        const auto output = BufferDescriptor(Output.data(), static_cast<std::uint32_t>(decltype(Output)::ByteSize));
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
        Require(result.bdaWrites && !result.unboundedWrites && result.writeRanges.size() == 1u, "pending store ranges: a store through a user-data V# has no bounded write range");
        device->Dispatch(result, 1, 1, 1, address);
        auto* recorder = AgcDriver::Graphics::Recorder::Active();
        Require(recorder != nullptr, "pending store ranges: the driver opened no recorder");
        Require(recorder->PendingWriteOverlaps(reinterpret_cast<std::uintptr_t>(Output.data()), decltype(Output)::ByteSize), "pending store ranges: the dispatch's store through its V# is not pending");
        Require(!recorder->PendingWriteOverlaps(reinterpret_cast<std::uintptr_t>(Other.data()), decltype(Other)::ByteSize), "pending store ranges: memory outside the dispatch's V# is pending");
        device->WaitIdle();
        Require(Output[0] == Stored, "pending store ranges: the store did not land");
        std::puts("pending store ranges tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
