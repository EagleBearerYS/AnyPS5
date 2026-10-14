#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include "GuestTestMemory.hpp"
#include <spirv/unified1/spirv.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderFloatMode;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Threads = 32;
constexpr std::uint32_t Inputs = 8;
constexpr std::uint32_t Operands = 4;
constexpr std::uint32_t Results = 32;
constexpr std::array<std::uint32_t, Inputs - Operands> Constants{0x3f000000u, 0x3f800000u, 0x40000000u, 0x00000000u};
constexpr std::uint32_t Sets = 8;
TestGuest::Array<std::uint32_t, Threads * Inputs> Input;
TestGuest::Array<std::uint32_t, Threads * Results> Merged;
TestGuest::Array<std::uint32_t, Threads * Results> Reloaded;

alignas(256) constexpr std::array<std::uint32_t, 73> MergedCode{
    0x34020085u, 0x160600ffu, 0x00000080u, 0xe0381000u, 0x80000401u, 0xbf8c3f70u, 0x06140b04u, 0x10160d04u,
    0xd54b000cu, 0x041a0b04u, 0xd504020du, 0x20020904u, 0x201c0f04u, 0xd508000fu, 0x08020b04u, 0x7e20810fu,
    0xd5038011u, 0x00020d04u, 0x7e244511u, 0x7e264104u, 0x7e284905u, 0x0e2a2714u, 0x0e2c0f0au, 0xd54b0017u,
    0x0412150cu, 0x7c020b04u, 0x02300905u, 0x7e321b18u, 0x7e340307u, 0x7e360306u, 0x7e3c0305u, 0x7e3e0307u,
    0xbe94037eu, 0x7c220f06u, 0x06340904u, 0x1036350au, 0xbefe0314u, 0x7e38491au, 0x7e3a7f1bu, 0xbe95037eu,
    0x7c220f04u, 0xbf880002u, 0x063c0f04u, 0x103e091eu, 0xbefe0315u, 0x7e40451eu, 0x7e421b1fu, 0x7e440d07u,
    0x104644f0u, 0x064808f2u, 0xd54b0025u, 0x03c9e904u, 0x104c0880u, 0x064e4523u, 0x7e504923u, 0x08524504u,
    0xe0781000u, 0x80010a03u, 0xe0781010u, 0x80010e03u, 0xe0781020u, 0x80011203u, 0xe0781030u, 0x80011603u,
    0xe0781040u, 0x80011a03u, 0xe0781050u, 0x80011e03u, 0xe0781060u, 0x80012203u, 0xe0781070u, 0x80012603u,
    0xbf810000u,
};

alignas(256) constexpr std::array<std::uint32_t, 293> ReloadedCode{
    0x34020085u, 0x160600ffu, 0x00000080u, 0xe0301000u, 0x80000401u, 0xe0301004u, 0x80000501u, 0xbf8c3f70u,
    0x06140b04u, 0xe0701000u, 0x80010a03u, 0xe0301000u, 0x80000401u, 0xe0301008u, 0x80000601u, 0xbf8c3f70u,
    0x10160d04u, 0xe0701004u, 0x80010b03u, 0xe0301000u, 0x80000401u, 0xe0301004u, 0x80000501u, 0xe0301008u,
    0x80000601u, 0xbf8c3f70u, 0xd54b000cu, 0x041a0b04u, 0xe0701008u, 0x80010c03u, 0xe0301000u, 0x80000401u,
    0xbf8c3f70u, 0xd504020du, 0x20020904u, 0xe070100cu, 0x80010d03u, 0xe0301000u, 0x80000401u, 0xe030100cu,
    0x80000701u, 0xbf8c3f70u, 0x201c0f04u, 0xe0701010u, 0x80010e03u, 0xe0301000u, 0x80000401u, 0xe0301004u,
    0x80000501u, 0xbf8c3f70u, 0xd508000fu, 0x08020b04u, 0xe0701014u, 0x80010f03u, 0xe0301014u, 0x80010f03u,
    0xbf8c3f70u, 0x7e20810fu, 0xe0701018u, 0x80011003u, 0xe0301000u, 0x80000401u, 0xe0301008u, 0x80000601u,
    0xbf8c3f70u, 0xd5038011u, 0x00020d04u, 0xe070101cu, 0x80011103u, 0xe030101cu, 0x80011103u, 0xbf8c3f70u,
    0x7e244511u, 0xe0701020u, 0x80011203u, 0xe0301000u, 0x80000401u, 0xbf8c3f70u, 0x7e264104u, 0xe0701024u,
    0x80011303u, 0xe0301004u, 0x80000501u, 0xbf8c3f70u, 0x7e284905u, 0xe0701028u, 0x80011403u, 0xe0301024u,
    0x80011303u, 0xe0301028u, 0x80011403u, 0xbf8c3f70u, 0x0e2a2714u, 0xe070102cu, 0x80011503u, 0xe0301000u,
    0x80010a03u, 0xe030100cu, 0x80000701u, 0xbf8c3f70u, 0x0e2c0f0au, 0xe0701030u, 0x80011603u, 0xe0301000u,
    0x80010a03u, 0xe0301008u, 0x80010c03u, 0xe0301000u, 0x80000401u, 0xbf8c3f70u, 0xd54b0017u, 0x0412150cu,
    0xe0701034u, 0x80011703u, 0xe0301000u, 0x80000401u, 0xe0301004u, 0x80000501u, 0xbf8c3f70u, 0x7c020b04u,
    0xe0301000u, 0x80000401u, 0xe0301004u, 0x80000501u, 0xbf8c3f70u, 0x02300905u, 0xe0701038u, 0x80011803u,
    0xe0301038u, 0x80011803u, 0xbf8c3f70u, 0x7e321b18u, 0xe070103cu, 0x80011903u, 0xe030100cu, 0x80000701u,
    0xbf8c3f70u, 0x7e340307u, 0xe0701040u, 0x80011a03u, 0xe0301008u, 0x80000601u, 0xbf8c3f70u, 0x7e360306u,
    0xe0701044u, 0x80011b03u, 0xe0301004u, 0x80000501u, 0xbf8c3f70u, 0x7e3c0305u, 0xe0701050u, 0x80011e03u,
    0xe030100cu, 0x80000701u, 0xbf8c3f70u, 0x7e3e0307u, 0xe0701054u, 0x80011f03u, 0xe0301008u, 0x80000601u,
    0xe030100cu, 0x80000701u, 0xbf8c3f70u, 0xbe94037eu, 0x7c220f06u, 0xe0301000u, 0x80000401u, 0xbf8c3f70u,
    0x06340904u, 0xe0701040u, 0x80011a03u, 0xe0301000u, 0x80010a03u, 0xe0301040u, 0x80011a03u, 0xbf8c3f70u,
    0x1036350au, 0xe0701044u, 0x80011b03u, 0xbefe0314u, 0xe0301040u, 0x80011a03u, 0xbf8c3f70u, 0x7e38491au,
    0xe0701048u, 0x80011c03u, 0xe0301044u, 0x80011b03u, 0xbf8c3f70u, 0x7e3a7f1bu, 0xe070104cu, 0x80011d03u,
    0xe0301000u, 0x80000401u, 0xe030100cu, 0x80000701u, 0xbf8c3f70u, 0xbe95037eu, 0x7c220f04u, 0xbf880010u,
    0xe0301000u, 0x80000401u, 0xe030100cu, 0x80000701u, 0xbf8c3f70u, 0x063c0f04u, 0xe0701050u, 0x80011e03u,
    0xe0301050u, 0x80011e03u, 0xe0301000u, 0x80000401u, 0xbf8c3f70u, 0x103e091eu, 0xe0701054u, 0x80011f03u,
    0xbefe0315u, 0xe0301050u, 0x80011e03u, 0xbf8c3f70u, 0x7e40451eu, 0xe0701058u, 0x80012003u, 0xe0301054u,
    0x80011f03u, 0xbf8c3f70u, 0x7e421b1fu, 0xe070105cu, 0x80012103u, 0xe030100cu, 0x80000701u, 0xbf8c3f70u,
    0x7e440d07u, 0xe0701060u, 0x80012203u, 0xe0301060u, 0x80012203u, 0xe0301010u, 0x80002a01u, 0xbf8c3f70u,
    0x1046452au, 0xe0701064u, 0x80012303u, 0xe0301000u, 0x80000401u, 0xe0301014u, 0x80002b01u, 0xbf8c3f70u,
    0x0648092bu, 0xe0701068u, 0x80012403u, 0xe0301000u, 0x80000401u, 0xe0301014u, 0x80002b01u, 0xe0301018u,
    0x80002c01u, 0xbf8c3f70u, 0xd54b0025u, 0x04ae5904u, 0xe070106cu, 0x80012503u, 0xe0301000u, 0x80000401u,
    0xe030101cu, 0x80002d01u, 0xbf8c3f70u, 0x104c092du, 0xe0701070u, 0x80012603u, 0xe0301060u, 0x80012203u,
    0xe0301064u, 0x80012303u, 0xbf8c3f70u, 0x064e4523u, 0xe0701074u, 0x80012703u, 0xe0301064u, 0x80012303u,
    0xbf8c3f70u, 0x7e504923u, 0xe0701078u, 0x80012803u, 0xe0301060u, 0x80012203u, 0xe0301000u, 0x80000401u,
    0xbf8c3f70u, 0x08524504u, 0xe070107cu, 0x80012903u, 0xbf810000u,
};

constexpr const char* Names[Results] = {
    "v_add_f32 v10, v4, v5",
    "v_mul_f32 v11, v4, v6",
    "v_fma_f32 v12, v4, v5, v6",
    "v_sub_f32 v13, -v4, |v4|",
    "v_max_f32 v14, v4, v7",
    "v_mul_f32 v15, v4, v5 mul:2",
    "v_frexp_mant_f32 v16, v15",
    "v_add_f32 v17, v4, v6 clamp",
    "v_ceil_f32 v18, v17",
    "v_fract_f32 v19, v4",
    "v_floor_f32 v20, v5",
    "v_mul_legacy_f32 v21, v20, v19",
    "v_mul_legacy_f32 v22, v10, v7",
    "v_fma_f32 v23, v12, v10, v4",
    "v_cndmask_b32 v24, v5, v4, vcc",
    "v_cvt_flr_i32_f32 v25, v24",
    "v_add_f32 v26, v4, v4 under v6 < v7",
    "v_mul_f32 v27, v10, v26 under v6 < v7",
    "v_floor_f32 v28, v26",
    "v_frexp_exp_i32_f32 v29, v27",
    "v_add_f32 v30, v4, v7 in a branch under v4 < v7",
    "v_mul_f32 v31, v30, v4 in a branch under v4 < v7",
    "v_ceil_f32 v32, v30",
    "v_cvt_flr_i32_f32 v33, v31",
    "v_cvt_f32_u32 v34, v7",
    "v_mul_f32 v35, 0.5, v34",
    "v_add_f32 v36, 1.0, v4",
    "v_fma_f32 v37, v4, 2.0, 1.0",
    "v_mul_f32 v38, 0, v4",
    "v_add_f32 v39, v35, v34",
    "v_floor_f32 v40, v35",
    "v_sub_f32 v41, v4, v34",
};

constexpr std::array<std::uint32_t, 32> Values{
    0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x007fffffu, 0x807fffffu, 0x00400000u, 0x80000002u,
    0x00800000u, 0x80800000u, 0x00800001u, 0x3f800000u, 0xbf800000u, 0x3f7fffffu, 0x40000000u, 0x3f000000u,
    0x7f7fffffu, 0xff7fffffu, 0x7f800000u, 0xff800000u, 0x7fc00000u, 0xffc00000u, 0x7fc00001u, 0xffd23456u,
    0x7f800001u, 0xff812345u, 0x7fbfffffu, 0x1f800000u, 0x20000000u, 0x3fc00000u, 0x41200000u, 0xc0490fdbu,
};

struct Mode {
    std::optional<ShaderFloatMode> floatMode;
    const char* name;
};

const std::array<Mode, 8> Modes{{
    {std::nullopt, "no float mode"},
    {ShaderFloatMode{0xc0u, true, false, false}, "IEEE=0 DX10_CLAMP=1 f32 denormals flushed"},
    {ShaderFloatMode{0xc0u, false, false, false}, "IEEE=0 DX10_CLAMP=0 f32 denormals flushed"},
    {ShaderFloatMode{0x00u, true, true, false}, "IEEE=1 DX10_CLAMP=1 all denormals flushed"},
    {ShaderFloatMode{0x00u, false, true, false}, "IEEE=1 DX10_CLAMP=0 all denormals flushed"},
    {ShaderFloatMode{0xc3u, true, false, false}, "IEEE=0 f32 denormals flushed, rounding toward zero"},
    {ShaderFloatMode{0xf0u, true, false, false}, "IEEE=0 denormals kept"},
    {ShaderFloatMode{0xf0u, false, true, false}, "IEEE=1 DX10_CLAMP=0 denormals kept"},
}};

std::array<std::uint32_t, 4> BufferDescriptor(const void* data, std::uint32_t bytes) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, 0x31016facu};
}

std::string Hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08x", value);
    return text;
}

ShaderRecompiler::RecompileRequest Request(std::span<const std::uint32_t> code, std::span<const std::uint32_t> output, const ShaderRecompiler::SpirvTarget& target, const std::optional<ShaderFloatMode>& floatMode,
                                           std::vector<std::uint32_t>& userData, std::array<ShaderRecompiler::MemoryRegion, 1>& memory) {
    userData.assign(8, 0u);
    const auto input = BufferDescriptor(Input.data(), static_cast<std::uint32_t>(Input.size() * 4u));
    const auto results = BufferDescriptor(output.data(), static_cast<std::uint32_t>(output.size() * 4u));
    std::copy(input.begin(), input.end(), userData.begin());
    std::copy(results.begin(), results.end(), userData.begin() + 4);
    memory = {{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{Threads, 1, 1}, 0u, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
        {32, 0, userData, compute, std::nullopt, std::nullopt, memory},
        target,
        {0, 0, 0, 128}
    };
    request.useCache = false;
    request.context.floatMode = floatMode;
    return request;
}

void FillInputs(std::uint32_t set) {
    std::uint32_t state = 0x2545f491u * (set + 1u);
    const auto next = [&]() {
        state = state * 1664525u + 1013904223u;
        return state;
    };
    const auto mixed = [&]() {
        const std::uint32_t pick = next();
        if ((pick & 1u) != 0u) return Values[(pick >> 1u) % Values.size()];
        constexpr std::array<std::uint32_t, 8> exponents{0x00u, 0x00u, 0xffu, 0x01u, 0x02u, 0x7eu, 0x7fu, 0xfeu};
        const std::uint32_t bits = next();
        return (bits & 0x807fffffu) | (exponents[(pick >> 1u) & 7u] << 23u);
    };
    for (std::uint32_t lane = 0; lane < Threads; ++lane) {
        std::uint32_t* row = &Input[lane * Inputs];
        if (set < Sets / 2u) {
            row[0] = Values[lane];
            row[1] = Values[(lane * 7u + set * 3u + 1u) % Values.size()];
            row[2] = Values[(lane * 11u + set * 5u + 2u) % Values.size()];
            row[3] = Values[(lane * 13u + set * 9u + 3u) % Values.size()];
        } else {
            for (std::uint32_t input = 0; input < Operands; ++input) row[input] = mixed();
        }
        std::copy(Constants.begin(), Constants.end(), row + Operands);
    }
}

void VerifySpirv() {
    constexpr std::array<std::uint32_t, 8> capabilities{
        spv::CapabilityShader, spv::CapabilityGroupNonUniform, spv::CapabilityGroupNonUniformBallot,
        spv::CapabilityInt64, spv::CapabilityPhysicalStorageBufferAddresses, spv::CapabilityStorageBuffer8BitAccess,
        spv::CapabilityDenormPreserve, spv::CapabilitySignedZeroInfNanPreserve,
    };
    constexpr std::array<std::string_view, 4> extensions{
        "SPV_KHR_physical_storage_buffer", "SPV_KHR_8bit_storage", "SPV_KHR_float_controls",
        "SPV_KHR_storage_buffer_storage_class",
    };
    ShaderRecompiler::SpirvTarget target{};
    target.vulkanVersion = 0x00403000u;
    target.spirvVersion = 0x00010600u;
    target.subgroupSize = 32;
    target.bdaAbiVersion = 1;
    target.supportedCapabilities = capabilities;
    target.supportedExtensions = extensions;
    target.maxWorkgroupSize = {1024, 1024, 64};
    target.maxWorkgroupInvocations = 1024;
    target.maxWorkgroupSharedMemoryBytes = 49152;
    std::vector<std::uint32_t> userData;
    std::array<ShaderRecompiler::MemoryRegion, 1> memory{};
    for (const auto& mode : Modes) {
        const auto result = ShaderRecompiler::Recompile(Request(MergedCode, Merged, target, mode.floatMode, userData, memory));
        const auto words = result.spirv.Words();
        std::unordered_map<std::uint32_t, std::uint32_t> constants;
        std::unordered_map<std::uint32_t, std::array<std::uint32_t, 3>> operations;
        std::unordered_map<std::uint32_t, std::uint32_t> opcodes;
        for (std::size_t cursor = 5; cursor < words.size();) {
            const auto count = words[cursor] >> 16u;
            const auto opcode = words[cursor] & 0xffffu;
            Require(count != 0 && cursor + count <= words.size(), "f32 operand idioms: malformed SPIR-V");
            if (opcode == spv::OpConstant && count == 4) constants[words[cursor + 2]] = words[cursor + 3];
            if ((opcode == spv::OpBitwiseAnd || opcode == spv::OpBitwiseXor || opcode == spv::OpIEqual || opcode == spv::OpUGreaterThan) && count == 5) {
                operations[words[cursor + 2]] = {words[cursor + 3], words[cursor + 4], 0u};
                opcodes[words[cursor + 2]] = opcode;
            }
            if (opcode == spv::OpSelect && count == 6) {
                operations[words[cursor + 2]] = {words[cursor + 3], words[cursor + 4], words[cursor + 5]};
                opcodes[words[cursor + 2]] = opcode;
            }
            cursor += count;
        }
        const auto masked = [&](std::uint32_t id, std::uint32_t opcode, std::uint32_t mask) -> std::optional<std::uint32_t> {
            const auto found = opcodes.find(id);
            if (found == opcodes.end() || found->second != opcode) return std::nullopt;
            const auto& operands = operations[id];
            if (constants.contains(operands[1]) && constants[operands[1]] == mask) return operands[0];
            if (constants.contains(operands[0]) && constants[operands[0]] == mask) return operands[1];
            return std::nullopt;
        };
        std::map<std::uint32_t, std::uint32_t> flushes;
        std::map<std::uint32_t, std::uint32_t> tests;
        for (const auto& [id, opcode] : opcodes) {
            const auto& operands = operations[id];
            if (opcode == spv::OpSelect) {
                const auto exponent = masked(operands[0], spv::OpIEqual, 0u);
                if (exponent && masked(*exponent, spv::OpBitwiseAnd, 0x7f800000u) == operands[2] && masked(operands[1], spv::OpBitwiseAnd, 0x80000000u) == operands[2]) ++flushes[operands[2]];
            }
            if (opcode == spv::OpUGreaterThan && constants.contains(operands[1]) && constants[operands[1]] == 0x7f800000u) {
                if (const auto source = masked(operands[0], spv::OpBitwiseAnd, 0x7fffffffu)) ++tests[*source];
            }
        }
        const bool flushing = !mode.floatMode.has_value() || (mode.floatMode->floatMode & 0x30u) == 0u;
        Require(!flushing || !flushes.empty(), std::string("f32 operand idioms: ") + mode.name + " emits no f32 flush");
        for (const auto& [source, count] : flushes) {
            Require(count == 1u, std::string("f32 operand idioms: ") + mode.name + " flushes SPIR-V value %" + std::to_string(source) + " " + std::to_string(count) + " times");
            Require(!masked(source, spv::OpBitwiseAnd, 0x7fffffffu) && !masked(source, spv::OpBitwiseXor, 0x80000000u), std::string("f32 operand idioms: ") + mode.name + " flushes SPIR-V value %" + std::to_string(source) + ", a source modifier, instead of the register value");
        }
        for (const auto& [source, count] : tests) {
            Require(count == 1u, std::string("f32 operand idioms: ") + mode.name + " tests SPIR-V value %" + std::to_string(source) + " for NaN " + std::to_string(count) + " times");
        }
    }
}

}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--verify-spirv") {
            VerifySpirv();
            std::puts("f32 operand idioms: every value is flushed and tested for NaN once");
            return 0;
        }
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        std::vector<std::uint32_t> mergedUserData;
        std::vector<std::uint32_t> reloadedUserData;
        std::array<ShaderRecompiler::MemoryRegion, 1> mergedMemory{};
        std::array<ShaderRecompiler::MemoryRegion, 1> reloadedMemory{};
        for (const auto& mode : Modes) {
            const auto merged = ShaderRecompiler::Recompile(Request(MergedCode, Merged, device->Target(), mode.floatMode, mergedUserData, mergedMemory));
            const auto reloaded = ShaderRecompiler::Recompile(Request(ReloadedCode, Reloaded, device->Target(), mode.floatMode, reloadedUserData, reloadedMemory));
            for (std::uint32_t set = 0; set < Sets; ++set) {
                FillInputs(set);
                Merged.fill(0xdeadbeefu);
                Reloaded.fill(0xfeedfaceu);
                device->Dispatch(merged, 1, 1, 1, reinterpret_cast<std::uintptr_t>(MergedCode.data()));
                device->Dispatch(reloaded, 1, 1, 1, reinterpret_cast<std::uintptr_t>(ReloadedCode.data()));
                device->WaitIdle();
                for (std::uint32_t lane = 0; lane < Threads; ++lane) {
                    const std::uint32_t* row = &Input[lane * Inputs];
                    for (std::uint32_t result = 0; result < Results; ++result) {
                        const std::uint32_t once = Merged[lane * Results + result];
                        const std::uint32_t perUse = Reloaded[lane * Results + result];
                        Require(once == perUse, std::string("f32 operand idioms: ") + mode.name + ", set " + std::to_string(set) + ", lane " + std::to_string(lane) + " (" + Hex(row[0]) + ", " + Hex(row[1]) + ", " + Hex(row[2]) + ", " + Hex(row[3]) + "): " +
                            Names[result] + " is " + Hex(once) + " with values read once and " + Hex(perUse) + " with every operand reloaded");
                    }
                }
            }
        }
        std::printf("f32 operand idioms: %zu modes x %u input sets x %u lanes agree bit for bit\n", Modes.size(), Sets, Threads);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
