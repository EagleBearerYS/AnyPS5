#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include "GuestTestMemory.hpp"
#include <spirv/unified1/spirv.hpp>
#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

TestGuest::Array<std::uint32_t, 8> Code{0x10020100u, 0xe0702000u, 0x80000100u, 0xbf810000u, 0u, 0u, 0u, 0u};
TestGuest::Array<std::uint32_t, 64> Output;

struct Counts {
    std::size_t isNan = 0;
    std::size_t maskedNanTests = 0;
};

Counts Count(std::span<const std::uint32_t> words) {
    Counts counts;
    std::map<std::uint32_t, std::uint32_t> constants;
    std::map<std::uint32_t, std::uint32_t> magnitudes;
    for (std::size_t cursor = 5; cursor < words.size();) {
        const auto count = words[cursor] >> spv::WordCountShift;
        const auto op = words[cursor] & spv::OpCodeMask;
        Require(count != 0 && cursor + count <= words.size(), "float NaN tests: malformed SPIR-V");
        const auto* operands = words.data() + cursor + 1;
        if (op == spv::OpConstant && count == 4u) constants[operands[1]] = operands[2];
        if (op == spv::OpBitwiseAnd && count == 5u && (constants[operands[3]] == 0x7fffffffu || constants[operands[2]] == 0x7fffffffu)) magnitudes[operands[1]] = 1u;
        if (op == spv::OpUGreaterThan && count == 5u && magnitudes.contains(operands[2]) && constants[operands[3]] == 0x7f800000u) ++counts.maskedNanTests;
        if (op == spv::OpIsNan) ++counts.isNan;
        cursor += count;
    }
    return counts;
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        const auto address = reinterpret_cast<std::uintptr_t>(Output.data());
        const std::vector<std::uint32_t> userData{static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (4u << 16u), 64u, 0x11016facu};
        const std::span<const std::uint32_t> code(Code.data(), 8u);
        const auto codeAddress = reinterpret_cast<std::uintptr_t>(code.data());
        const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{codeAddress, std::as_bytes(code)}}};
        const ShaderRecompiler::ShaderComputeStageInfo compute{{32u, 1u, 1u}, 0u, {false, false, false}, false, 1u};
        ShaderRecompiler::RecompileRequest request{
            {ShaderStage::Compute, codeAddress, code, 0, {}},
            {32, 0, userData, compute, std::nullopt, std::nullopt, memory},
            device->Target(),
            {0, 0, 0, 128}
        };
        request.useCache = false;
        const auto result = ShaderRecompiler::Recompile(request);
        const auto counts = Count(result.spirv.Words());
        Require(counts.isNan >= 2u, "float NaN tests: a v_mul_f32 tests its result and its operand for NaN with " + std::to_string(counts.isNan) + " OpIsNan, expected at least 2");
        Require(counts.maskedNanTests == 0u, "float NaN tests: " + std::to_string(counts.maskedNanTests) + " NaN tests still mask the magnitude and compare it with the infinity bits although the float value is at hand");
        std::puts("float NaN tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
