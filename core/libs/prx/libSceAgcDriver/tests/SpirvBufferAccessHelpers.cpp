#include "Recompiler.hpp"
#include <spirv/unified1/spirv.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace ShaderRecompiler;

struct Function {
    std::uint32_t control = 0;
    std::string name;
    std::size_t widestSwitch = 0;
};

struct Module {
    std::map<std::uint32_t, Function> functions;
    std::map<std::uint32_t, std::uint32_t> callsFromMain;
    std::size_t mainBufferMemoryLoads = 0;
};

std::vector<std::uint32_t> Recompiled(std::span<const std::uint32_t> code) {
    const std::array<std::uint32_t, 12> userData{0x10000000u, 0x00000000u, 0x100u, 0x31016facu, 0x20000000u, 0x00000000u, 0x100u, 0x31016facu, 0x30000000u, 0x00000000u, 0x100u, 0x31016facu};
    static constexpr std::array<std::string_view, 2> extensions{"SPV_KHR_physical_storage_buffer", "SPV_KHR_8bit_storage"};
    static constexpr std::array<std::uint32_t, 8> capabilities{
        spv::CapabilityInt64, spv::CapabilityPhysicalStorageBufferAddresses, spv::CapabilityStorageBuffer8BitAccess, spv::CapabilityGroupNonUniform,
        spv::CapabilityGroupNonUniformBallot, spv::CapabilityGroupNonUniformShuffle, spv::CapabilityGroupNonUniformVote, spv::CapabilityGroupNonUniformArithmetic};
    RecompileRequest request{};
    request.shader = {ShaderStage::Compute, 0x20000u, code, 0, {}};
    request.context.waveSize = 32;
    request.context.userDataBaseRegister = 0;
    request.context.userData = userData;
    request.context.compute = ShaderComputeStageInfo{{32u, 1u, 1u}, 0u, {false, false, false}, false, 1u};
    request.target.vulkanVersion = 0x00401000u;
    request.target.spirvVersion = 0x00010300u;
    request.target.subgroupSize = 32;
    request.target.bdaAbiVersion = 1;
    request.target.supportedCapabilities = capabilities;
    request.target.supportedExtensions = extensions;
    request.layout.pushConstantSizeBytes = 128;
    request.useCache = false;
    auto result = Recompile(request);
    if (result.spirv.empty()) throw std::runtime_error("the shader did not recompile");
    return result.spirv.Copy();
}

Module Parse(std::span<const std::uint32_t> words) {
    Module module;
    std::uint32_t entry = 0;
    std::set<std::uint32_t> physicalPointerTypes;
    std::set<std::uint32_t> physicalPointers;
    std::uint32_t current = 0;
    for (std::size_t cursor = 5; cursor < words.size();) {
        const auto count = words[cursor] >> 16u;
        const auto op = words[cursor] & 0xffffu;
        if (count == 0 || cursor + count > words.size()) throw std::runtime_error("truncated SPIR-V instruction");
        const auto* operands = words.data() + cursor + 1;
        if (op == spv::OpEntryPoint) entry = operands[1];
        if (op == spv::OpName) {
            std::string name(reinterpret_cast<const char*>(operands + 1), (count - 2u) * 4u);
            module.functions[operands[0]].name = name.c_str();
        }
        if (op == spv::OpTypePointer && operands[1] == spv::StorageClassPhysicalStorageBuffer) physicalPointerTypes.insert(operands[0]);
        if ((op == spv::OpConvertUToPtr || op == spv::OpAccessChain || op == spv::OpPtrAccessChain) && physicalPointerTypes.contains(operands[0])) physicalPointers.insert(operands[1]);
        if (op == spv::OpFunction) {
            current = operands[1];
            module.functions[current].control = operands[2];
        }
        if (op == spv::OpFunctionEnd) current = 0;
        if (current != 0 && op == spv::OpSwitch) {
            std::set<std::uint32_t> targets{operands[1]};
            for (std::uint32_t operand = 3; operand < count - 1u; operand += 2u) targets.insert(operands[operand]);
            auto& widest = module.functions[current].widestSwitch;
            widest = std::max(widest, targets.size());
        }
        if (current != 0 && current == entry) {
            if (op == spv::OpFunctionCall) ++module.callsFromMain[operands[2]];
            if ((op == spv::OpLoad || op == spv::OpAtomicLoad) && physicalPointers.contains(operands[2])) ++module.mainBufferMemoryLoads;
        }
        cursor += count;
    }
    return module;
}

void Expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::uint32_t> Calls(const Module& module, std::string_view prefix) {
    std::vector<std::uint32_t> calls;
    for (const auto& [id, function] : module.functions) {
        if (!function.name.starts_with(prefix)) continue;
        Expect((function.control & spv::FunctionControlDontInlineMask) != 0u, function.name + " is not DontInline");
        const auto found = module.callsFromMain.find(id);
        calls.push_back(found == module.callsFromMain.end() ? 0u : found->second);
    }
    return calls;
}

std::string Describe(const std::vector<std::uint32_t>& calls) {
    std::string text = std::to_string(calls.size()) + " helpers called";
    for (const auto count : calls) text += " " + std::to_string(count);
    return text + " times";
}

}

int main() {
    try {
        const std::vector<std::uint32_t> code{
            0x34020084u,
            0x7e200281u,
            0xe03c1000u, 0x80000201u,
            0xe03c1010u, 0x80000501u,
            0xe03c1020u, 0x80000801u,
            0xe0301000u, 0x80010b01u,
            0xe0301004u, 0x80010c01u,
            0xf4200502u, 0xfa000000u,
            0xf4200542u, 0xfa000004u,
            0xf4200582u, 0xfa000008u,
            0xe00c1000u, 0x80001401u,
            0xe0c81040u, 0x80011001u,
            0xe0c81044u, 0x80011001u,
            0xbf8c3f70u,
            0xbf8cc07fu,
            0x7e1a0214u,
            0x7e1c0215u,
            0x7e1e0216u,
            0xe07c1000u, 0x80020201u,
            0xe07c1010u, 0x80020501u,
            0xe07c1020u, 0x80020801u,
            0xe07c1060u, 0x80020d01u,
            0xe0741030u, 0x80020b01u,
            0xe0781040u, 0x80021401u,
            0xbf810000u};
        const auto module = Parse(Recompiled(code));
        const auto loads = Calls(module, "load_buffer_");
        Expect(loads.size() == 2u && ((loads[0] == 3u && loads[1] == 2u) || (loads[0] == 2u && loads[1] == 3u)), "expected one load helper called by the three dwordx3 loads and one called by the two dword loads, found " + Describe(loads));
        const auto stores = Calls(module, "store_buffer_");
        Expect(stores.size() == 1u && stores[0] == 4u, "expected one store helper called by the four dwordx3 stores, found " + Describe(stores));
        const auto scalars = Calls(module, "load_scalar_buffer_");
        Expect(scalars.size() == 1u && scalars[0] == 3u, "expected one scalar load helper called by the three s_buffer_load_dword, found " + Describe(scalars));
        const auto atomics = Calls(module, "buffer_atomic_address_");
        Expect(atomics.size() == 1u && atomics[0] == 2u, "expected one atomic address helper called by the two buffer_atomic_add, found " + Describe(atomics));
        const auto formatted = Calls(module, "load_formatted_buffer_");
        Expect(formatted.size() == 1u && formatted[0] == 1u, "expected one formatted load helper called once, found " + Describe(formatted));
        Expect(module.mainBufferMemoryLoads == 3u, "main loads buffer memory " + std::to_string(module.mainBufferMemoryLoads) + " times, expected once for each s_buffer_load_dword through its cached translation");
        for (const auto& [id, function] : module.functions) {
            if (!function.name.starts_with("load_formatted_buffer_")) continue;
            Expect(function.widestSwitch != 0u && function.widestSwitch <= 15u, function.name + " switches to " + std::to_string(function.widestSwitch) + " labels, expected one per format layout and the invalid format (at most 15)");
        }
    } catch (const std::exception& error) {
        std::cerr << "SpirvBufferAccessHelpers: " << error.what() << '\n';
        return 1;
    }
    std::puts("SpirvBufferAccessHelpers: buffer accesses call one helper per resource and shape");
    return 0;
}
