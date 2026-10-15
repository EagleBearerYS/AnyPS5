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

constexpr std::uint32_t Threads = 32;
constexpr std::uint32_t TableValue = 0x12345678u;
constexpr std::uint32_t TableDword = 14;

TestGuest::Array<std::uint32_t, 16> Code{
    0xbe801f00u, 0x800000ffu, TableDword * 4u - 4u, 0x82010180u, 0xbe8303ffu, 0x31016facu, 0xb0020010u, 0xf4200100u,
    0xfa000000u, 0xbf8cc07fu, 0x7e000204u, 0xe0700000u, 0x80020000u, 0xbf810000u, TableValue, 0u};
TestGuest::Array<std::uint32_t, 4> Output;

std::array<std::uint32_t, 4> BufferDescriptor(const void* data, std::uint32_t bytes) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, 0x31016facu};
}

std::string Hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08x", value);
    return text;
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        Output = {0xdeadbeefu, 0xdeadbeefu, 0xdeadbeefu, 0xdeadbeefu};
        std::vector<std::uint32_t> userData(12, 0u);
        const auto output = BufferDescriptor(Output.data(), static_cast<std::uint32_t>(decltype(Output)::ByteSize));
        std::copy(output.begin(), output.end(), userData.begin() + 8);
        const std::span<const std::uint32_t> code(Code.data(), 16u);
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
        device->Dispatch(result, 1, 1, 1, address);
        device->WaitIdle();
        Require(Output[0] == TableValue, "shader base table: the s_buffer_load through a V# built from s_getpc_b64 read " + Hex(Output[0]) + ", expected " + Hex(TableValue) + " from the table in the shader's code");
        std::puts("shader base table tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
