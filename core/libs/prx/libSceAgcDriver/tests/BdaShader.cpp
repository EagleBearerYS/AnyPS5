#include "BdaShader.hpp"
#include "SpirvBackend/SpirvBda.hpp"
#include "SpirvBackend/SpirvMemory/SpirvTypes.hpp"
#include "SpirvBackend/SpirvMemory/SpirvConstants.hpp"
#include "IntermediateRepresentation/IrBuilder.hpp"
#if ANYPS5_ENABLE_SPIRV_TOOLS
#include "SpirvBackend/SpirvOptimizer.hpp"
#endif

namespace {

template<typename TEmit>
std::vector<std::uint32_t> MakeShader(std::uint64_t address, bool coherent, bool stops, const TEmit& emit, std::uint32_t extracted = 0, bool writes = false) {
    using namespace ShaderRecompiler;
    IrProgram program;
    program.Resources().stage = IrShaderStage::Compute;
    program.Info().usesDma = true;
    program.Info().bdaWrites = writes;
    if (coherent) {
        MemoryInfo memory;
        memory.coherent = true;
        program.Resources().memoryInfo = {memory};
    }
    SpirvEmitterState state(program, {});
    EmitBaseHeader(state.module, program);
    const auto define = [&](std::uint32_t binding) {
        const auto variable = state.module.DefineGlobalVariable(TypeStorageBufferPointer(state), spv::StorageClassStorageBuffer);
        state.module.AddAnnotation(spv::OpDecorate, variable, spv::DecorationDescriptorSet, 0u);
        state.module.AddAnnotation(spv::OpDecorate, variable, spv::DecorationBinding, binding);
        return variable;
    };
    state.bdaPagetableVariable = define(0);
    state.faultBufferVariable = define(1);
    const auto output = define(2);
    std::vector<std::uint32_t> interfaceVariables;
    if (!stops) {
        interfaceVariables.push_back(state.module.DefineGlobalVariable(TypePointer(state, spv::StorageClassInput, TypeU32(state)), spv::StorageClassInput));
        state.module.AddAnnotation(spv::OpDecorate, interfaceVariables[0], spv::DecorationBuiltIn, spv::BuiltInLocalInvocationIndex);
    }
    state.bdaStopsInvocations = stops;
    DefineGetBdaPointer(state);
    const auto main = state.module.AllocateId();
    state.module.AddFunction(spv::OpFunction, TypeVoid(state), main, spv::FunctionControlMaskNone, TypeFunction(state));
    EmitLabel(state, state.module.AllocateId());
    SpirvValueEmitContext ctx(state);
    auto& instruction = writes ? program.CreateValue(IrOpcode::StoreAddressU32, IrType::Void) : extracted != 0u ? program.CreateValue(IrOpcode::LoadAddressU32x4, IrType::U32x4) : program.CreateValue(IrOpcode::LoadAddressU32, IrType::U32);
    IrBuilder ir(program);
    for (std::uint32_t dword = 0; dword < 4u; ++dword) {
        if ((extracted & (1u << dword)) == 0u) continue;
        auto& extract = program.CreateValue(IrOpcode::CompositeExtractU32x4, IrType::U32);
        extract.AddArgument(&instruction);
        extract.AddArgument(&ir.Constant(dword));
    }
    MemoryFlags flags{};
    flags.pc = 0x1234;
    instruction.SetFlags(flags);
    auto base = BdaConstant(state, address);
    if (!stops) {
        const auto index = state.module.AllocateId();
        state.module.AddFunction(spv::OpLoad, TypeU32(state), index, interfaceVariables[0]);
        base = Binary(state, spv::OpIAdd, TypeScalarU64(state), base, Unary(state, spv::OpUConvert, TypeScalarU64(state), index));
    }
    const auto values = emit(ctx, instruction, base);
    for (std::uint32_t index = 0; index < values.size(); ++index) state.module.AddFunction(spv::OpStore, BdaWord(state, output, ConstantU32(state, index)), values[index]);
    state.module.AddFunction(spv::OpReturn);
    state.module.AddFunction(spv::OpFunctionEnd);
    state.module.AddExecutionMode(main, spv::ExecutionModeLocalSize, 1u, 1u, 1u);
    state.module.EmitEntryPoint(spv::ExecutionModelGLCompute, main, "main", interfaceVariables);
    return state.module.Finalize();
}

}

std::vector<std::uint32_t> MakeBdaTranslateTestShader(std::uint32_t count) {
    using namespace ShaderRecompiler;
    IrProgram program;
    program.Resources().stage = IrShaderStage::Compute;
    program.Info().usesDma = true;
    SpirvEmitterState state(program, {});
    EmitBaseHeader(state.module, program);
    const auto define = [&](std::uint32_t binding) {
        const auto variable = state.module.DefineGlobalVariable(TypeStorageBufferPointer(state), spv::StorageClassStorageBuffer);
        state.module.AddAnnotation(spv::OpDecorate, variable, spv::DecorationDescriptorSet, 0u);
        state.module.AddAnnotation(spv::OpDecorate, variable, spv::DecorationBinding, binding);
        return variable;
    };
    state.bdaPagetableVariable = define(0);
    state.faultBufferVariable = define(1);
    const auto io = define(2);
    const auto invocation = state.module.DefineGlobalVariable(TypePointer(state, spv::StorageClassInput, TypeU32Vector(state, 3)), spv::StorageClassInput);
    state.module.AddAnnotation(spv::OpDecorate, invocation, spv::DecorationBuiltIn, spv::BuiltInGlobalInvocationId);
    DefineGetBdaPointer(state);
    const auto main = state.module.AllocateId();
    state.module.AddFunction(spv::OpFunction, TypeVoid(state), main, spv::FunctionControlMaskNone, TypeFunction(state));
    EmitLabel(state, state.module.AllocateId());
    const auto u32 = TypeU32(state);
    const auto u64 = TypeScalarU64(state);
    const auto ids = state.module.AllocateId();
    state.module.AddFunction(spv::OpLoad, TypeU32Vector(state, 3), ids, invocation);
    const auto index = state.module.AllocateId();
    state.module.AddFunction(spv::OpCompositeExtract, u32, index, ids, 0u);
    EmitIfCondition(state, Binary(state, spv::OpULessThan, TypeBool(state), index, ConstantU32(state, count)), [&] {
        const auto word = [&](std::uint32_t at) { return Binary(state, spv::OpIAdd, u32, Binary(state, spv::OpShiftLeftLogical, u32, index, ConstantU32(state, 2u)), ConstantU32(state, at)); };
        const auto load = [&](std::uint32_t at) {
            const auto value = state.module.AllocateId();
            state.module.AddFunction(spv::OpLoad, u32, value, BdaWord(state, io, word(at)));
            return value;
        };
        const auto address = Binary(state, spv::OpBitwiseOr, u64, Unary(state, spv::OpUConvert, u64, load(0u)), Binary(state, spv::OpShiftLeftLogical, u64, Unary(state, spv::OpUConvert, u64, load(1u)), BdaConstant(state, 32u)));
        const auto translated = state.module.AllocateId();
        state.module.AddFunction(spv::OpFunctionCall, state.module.Type(spv::OpTypeStruct, u64, u32), translated, state.bdaTranslateFunction, address, load(2u), load(3u));
        const auto device = state.module.AllocateId();
        state.module.AddFunction(spv::OpCompositeExtract, u64, device, translated, 0u);
        const auto reason = state.module.AllocateId();
        state.module.AddFunction(spv::OpCompositeExtract, u32, reason, translated, 1u);
        const auto results = count * 4u;
        state.module.AddFunction(spv::OpStore, BdaWord(state, io, word(results)), Unary(state, spv::OpUConvert, u32, device));
        state.module.AddFunction(spv::OpStore, BdaWord(state, io, word(results + 1u)), Unary(state, spv::OpUConvert, u32, Binary(state, spv::OpShiftRightLogical, u64, device, BdaConstant(state, 32u))));
        state.module.AddFunction(spv::OpStore, BdaWord(state, io, word(results + 2u)), reason);
    });
    state.module.AddFunction(spv::OpReturn);
    state.module.AddFunction(spv::OpFunctionEnd);
    state.module.AddExecutionMode(main, spv::ExecutionModeLocalSize, 64u, 1u, 1u);
    state.module.EmitEntryPoint(spv::ExecutionModelGLCompute, main, "main", std::vector<std::uint32_t>{invocation});
    return state.module.Finalize();
}

std::vector<std::uint32_t> MakeBdaTestShader(std::uint64_t address, std::uint32_t bits, std::int64_t offset) {
    using namespace ShaderRecompiler;
    return MakeShader(address, false, true, [&](SpirvValueEmitContext& ctx, const IrValue& instruction, std::uint32_t base) {
        if (offset != 0) base = AddBdaAddress(ctx, instruction, base, BdaConstant(ctx.state, offset < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(offset) : static_cast<std::uint64_t>(offset)), offset < 0);
        return std::vector<std::uint32_t>{EmitBdaRead(ctx, instruction, base, bits)};
    });
}

std::vector<std::uint32_t> MakeBdaSpanReadTestShader(std::uint64_t address, std::uint32_t offset, std::uint32_t extracted, bool coherent, bool stops) {
    using namespace ShaderRecompiler;
    return MakeShader(address, coherent, stops, [&](SpirvValueEmitContext& ctx, const IrValue& instruction, std::uint32_t base) {
        const auto values = EmitBdaDwordReads(ctx, instruction, base, offset, 4u);
        return std::vector<std::uint32_t>(values.begin(), values.end());
    }, extracted);
}

std::vector<std::uint32_t> MakeBdaDwordWriteTestShader(std::uint64_t address, std::uint32_t dwords, const std::uint32_t* values) {
    using namespace ShaderRecompiler;
    return MakeShader(address, false, true, [&](SpirvValueEmitContext& ctx, const IrValue& instruction, std::uint32_t base) {
        auto& state = ctx.state;
        std::vector<std::uint32_t> parts;
        for (std::uint32_t dword = 0; dword < dwords; ++dword) parts.push_back(ConstantU32(state, values[dword]));
        const auto composite = state.module.AllocateId();
        if (dwords == 2u) state.module.AddFunction(spv::OpCompositeConstruct, TypeU32Vector(state, 2u), composite, parts[0], parts[1]);
        else if (dwords == 3u) state.module.AddFunction(spv::OpCompositeConstruct, TypeU32Vector(state, 3u), composite, parts[0], parts[1], parts[2]);
        else state.module.AddFunction(spv::OpCompositeConstruct, TypeU32Vector(state, 4u), composite, parts[0], parts[1], parts[2], parts[3]);
        EmitBdaDwordWrites(ctx, instruction, base, 0u, dwords, composite);
        return std::vector<std::uint32_t>{ConstantU32(state, 1u)};
    }, 0u, true);
}

std::vector<std::uint32_t> MakeBdaDwordReadTestShader(std::uint64_t address, std::uint32_t dwords, bool coherent, bool stops) {
    using namespace ShaderRecompiler;
    return MakeShader(address, coherent, stops, [&](SpirvValueEmitContext& ctx, const IrValue& instruction, std::uint32_t base) {
        const auto values = EmitBdaDwordReads(ctx, instruction, base, 0u, dwords, true);
        return std::vector<std::uint32_t>(values.begin(), values.begin() + dwords);
    });
}

std::vector<std::uint32_t> MakeBdaStoreTestShader(std::uint64_t address, std::uint32_t value, std::uint32_t bits, bool coherent) {
    using namespace ShaderRecompiler;
    auto shader = MakeShader(address, coherent, true, [&](SpirvValueEmitContext& ctx, const IrValue& instruction, std::uint32_t base) {
        EmitBdaStore(ctx, instruction, base, ConstantU32(ctx.state, value), bits);
        return std::vector<std::uint32_t>{ConstantU32(ctx.state, 1u)};
    }, 0u, true);
#if ANYPS5_ENABLE_SPIRV_TOOLS
    shader = ValidateAndOptimizeSpirv(shader, VK_API_VERSION_1_1, shader[1], false, false);
#endif
    return shader;
}

std::vector<std::uint32_t> MakeBdaWriteTestShader(std::uint64_t address, std::uint32_t value, std::uint32_t bits, bool coherent) {
    using namespace ShaderRecompiler;
    auto shader = MakeShader(address, coherent, true, [&](SpirvValueEmitContext& ctx, const IrValue& instruction, std::uint32_t base) {
        EmitBdaWrite(ctx, instruction, base, ConstantU32(ctx.state, value), bits);
        return std::vector<std::uint32_t>{ConstantU32(ctx.state, 1u)};
    }, 0u, true);
#if ANYPS5_ENABLE_SPIRV_TOOLS
    shader = ValidateAndOptimizeSpirv(shader, VK_API_VERSION_1_1, shader[1], false, false);
#endif
    return shader;
}
