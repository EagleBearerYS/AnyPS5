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
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t MaxThreads = 64;
constexpr std::uint32_t Inputs = 4;
constexpr std::uint32_t Results = 32;
constexpr std::uint32_t Checked = 9;
constexpr std::uint32_t ThenLanes = 16;
TestGuest::Array<std::uint32_t, MaxThreads * Inputs> Input;
TestGuest::Array<std::uint32_t, MaxThreads * Results> Output;

alignas(256) constexpr std::array<std::uint32_t, 92> Wave32Code{
    0x34020084, 0x34060087, 0xe0301000, 0x80000201, 0xe0301004, 0x80000401, 0xbf8c3f70, 0x7e100504,
    0x4a140481, 0x4a160491, 0x4a140482, 0x4a1804ff, 0x00000050, 0x4a1a04ff, 0x00000060, 0x4a140483,
    0x4a2004ff, 0x00000700, 0x4a2204ff, 0x00000900, 0x4a1c0480, 0x4a1e04ff, 0x00005000, 0x7e0e0280,
    0x807c8008, 0x7e0a870a, 0xe0701000, 0x80010503, 0x7d880090, 0xbe943c6a, 0xbf880004, 0x4a1804ff,
    0x00000100, 0x4a1a04ff, 0x00000200, 0x8a7e7e14, 0xbf880002, 0x4a1804ff, 0x00000300, 0xbefe0314,
    0x807c8208, 0x7e0a870a, 0xe0701004, 0x80010503, 0x807c8308, 0x7e0a870a, 0xe0701008, 0x80010503,
    0xbf068008, 0xbf840002, 0x4a2004ff, 0x00000800, 0xbf078008, 0xbf840002, 0x4a2204ff, 0x00000a00,
    0x807c8608, 0x7e0a870a, 0xe070100c, 0x80010503, 0x807c8708, 0x7e0a870a, 0xe0701010, 0x80010503,
    0xbe890383, 0x807c8508, 0x7e0c870a, 0x4a0e0f06, 0x4a1e1e90, 0x4a1c050e, 0x80898109, 0xbf078009,
    0xbf85fff8, 0x7e1e02ff, 0x00000077, 0x807c8408, 0x7e0a870a, 0xe0701014, 0x80010503, 0x807c8508,
    0x7e0a870a, 0xe0701018, 0x80010503, 0xe070101c, 0x80010703, 0xbefc0381, 0x7e0a870a, 0xe0701020,
    0x80010503, 0x7e140280, 0x7e160280, 0xbf810000,
};

alignas(256) constexpr std::array<std::uint32_t, 92> Wave64Code{
    0x34020084, 0x34060087, 0xe0301000, 0x80000201, 0xe0301004, 0x80000401, 0xbf8c3f70, 0x7e100504,
    0x4a140481, 0x4a160491, 0x4a140482, 0x4a1804ff, 0x00000050, 0x4a1a04ff, 0x00000060, 0x4a140483,
    0x4a2004ff, 0x00000700, 0x4a2204ff, 0x00000900, 0x4a1c0480, 0x4a1e04ff, 0x00005000, 0x7e0e0280,
    0x807c8008, 0x7e0a870a, 0xe0701000, 0x80010503, 0x7d880090, 0xbe94246a, 0xbf880004, 0x4a1804ff,
    0x00000100, 0x4a1a04ff, 0x00000200, 0x8afe7e14, 0xbf880002, 0x4a1804ff, 0x00000300, 0xbefe0414,
    0x807c8208, 0x7e0a870a, 0xe0701004, 0x80010503, 0x807c8308, 0x7e0a870a, 0xe0701008, 0x80010503,
    0xbf068008, 0xbf840002, 0x4a2004ff, 0x00000800, 0xbf078008, 0xbf840002, 0x4a2204ff, 0x00000a00,
    0x807c8608, 0x7e0a870a, 0xe070100c, 0x80010503, 0x807c8708, 0x7e0a870a, 0xe0701010, 0x80010503,
    0xbe890383, 0x807c8508, 0x7e0c870a, 0x4a0e0f06, 0x4a1e1e90, 0x4a1c050e, 0x80898109, 0xbf078009,
    0xbf85fff8, 0x7e1e02ff, 0x00000077, 0x807c8408, 0x7e0a870a, 0xe0701014, 0x80010503, 0x807c8508,
    0x7e0a870a, 0xe0701018, 0x80010503, 0xe070101c, 0x80010703, 0xbefc0381, 0x7e0a870a, 0xe0701020,
    0x80010503, 0x7e140280, 0x7e160280, 0xbf810000,
};

constexpr const char* Names[Checked] = {"rewritten_before_read", "written_in_both_arms", "written_in_then_arm", "written_in_taken_uniform_arm", "written_before_skipped_uniform_arm",
    "accumulated_in_loop", "rewritten_after_loop", "read_in_loop_after_previous_iteration_write", "constant_index_read"};

std::uint32_t Lane(std::uint32_t tid) {
    return 0x10000u * (tid + 1u) + tid;
}

std::array<std::uint32_t, Checked> ExpectedFor(std::uint32_t tid) {
    const std::uint32_t x = Lane(tid);
    const bool then = tid < ThenLanes;
    return {x + 3u, x + (then ? 0x100u : 0x300u), x + (then ? 0x200u : 0x60u), x + 0x800u, x + 0x900u, 4u * x, 0x77u, 3u * x + 0xf030u, x + 0x11u};
}

std::array<std::uint32_t, 4> BufferDescriptor(const void* data, std::uint32_t bytes) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, 0x31016facu};
}

std::string Hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08x", value);
    return text;
}

void Run(AgcDriver::VulkanDevice& device, std::span<const std::uint32_t> code, std::uint32_t waveSize) {
    Input.fill(0u);
    for (std::uint32_t tid = 0; tid < waveSize; ++tid) Input[tid * Inputs] = Lane(tid);
    Output.fill(0xdeadbeefu);
    std::vector<std::uint32_t> userData(8, 0u);
    const auto input = BufferDescriptor(Input.data(), static_cast<std::uint32_t>(Input.size() * 4u));
    const auto output = BufferDescriptor(Output.data(), static_cast<std::uint32_t>(Output.size() * 4u));
    std::copy(input.begin(), input.end(), userData.begin());
    std::copy(output.begin(), output.end(), userData.begin() + 4);
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{waveSize, 1, 1}, 0u, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
        {waveSize, 0, userData, compute, std::nullopt, std::nullopt, memory},
        device.Target(),
        {0, 0, 0, 128}
    };
    request.useCache = false;
    const auto result = ShaderRecompiler::Recompile(request);
    device.Dispatch(result, 1, 1, 1, reinterpret_cast<std::uintptr_t>(code.data()));
    device.WaitIdle();
}

void Check(std::uint32_t waveSize) {
    for (std::uint32_t tid = 0; tid < waveSize; ++tid) {
        const std::uint32_t* out = &Output[tid * Results];
        const auto expected = ExpectedFor(tid);
        for (std::uint32_t index = 0; index < Checked; ++index) {
            Require(out[index] == expected[index], std::string("vector window control flow: wave") + std::to_string(waveSize) + " lane " + std::to_string(tid) + " " + Names[index] + " is " + Hex(out[index]) + ", expected " + Hex(expected[index]));
        }
    }
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        if (device->Target().subgroupSize < 32u) {
            std::printf("skipped, subgroup size %u cannot hold a wave32\n", device->Target().subgroupSize);
            return VulkanTestSkipped;
        }
        Run(*device, Wave32Code, 32);
        Check(32);
        Run(*device, Wave64Code, 64);
        Check(64);
        std::puts("vector window control flow tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
