#include "SpirvBackend/SpirvBda.hpp"
#include "SpirvBackend/SpirvMemory/SpirvTypes.hpp"
#include "SpirvBackend/SpirvMemory/SpirvConstants.hpp"
#include <array>
#include <bit>
#include <stdexcept>

namespace ShaderRecompiler {

namespace {

std::uint32_t TranslationType(SpirvEmitterState& state) {
    return state.module.Type(spv::OpTypeStruct, TypeScalarU64(state), TypeU32(state));
}

void ReturnValueIf(SpirvEmitterState& state, std::uint32_t condition, std::uint32_t value) {
    const auto taken = state.module.AllocateId();
    const auto next = state.module.AllocateId();
    state.module.AddFunction(spv::OpSelectionMerge, next, spv::SelectionControlMaskNone);
    state.module.AddFunction(spv::OpBranchConditional, condition, taken, next);
    EmitLabel(state, taken);
    state.module.AddFunction(spv::OpReturnValue, value);
    EmitLabel(state, next);
}

std::uint32_t DefineBdaTranslate(SpirvEmitterState& state) {
    const auto u32 = TypeU32(state);
    const auto u64 = TypeScalarU64(state);
    const auto boolean = TypeBool(state);
    const auto entryType = TypeU32Vector(state, 4);
    const auto result = TranslationType(state);
    const auto constant = [&](std::uint32_t value) { return ConstantU32(state, value); };
    const auto binary = [&](std::uint32_t op, std::uint32_t type, std::uint32_t left, std::uint32_t right) { return Binary(state, op, type, left, right); };
    const auto widen = [&](std::uint32_t value) { return Unary(state, spv::OpUConvert, u64, value); };
    const auto load = [&](std::uint32_t type, std::uint32_t pointer) {
        const auto value = state.module.AllocateId();
        state.module.AddFunction(spv::OpLoad, type, value, pointer);
        return value;
    };
    const auto loadDevice = [&](std::uint32_t type, std::uint32_t address, std::uint32_t alignment) {
        const auto pointer = state.module.AllocateId();
        state.module.AddFunction(spv::OpConvertUToPtr, TypePointer(state, spv::StorageClassPhysicalStorageBuffer, type), pointer, address);
        const auto value = state.module.AllocateId();
        state.module.AddFunction(spv::OpLoad, type, value, pointer, spv::MemoryAccessAlignedMask, alignment);
        return value;
    };
    const auto component = [&](std::uint32_t vector, std::uint32_t index) {
        const auto value = state.module.AllocateId();
        state.module.AddFunction(spv::OpCompositeExtract, u32, value, vector, index);
        return value;
    };
    const auto pair = [&](std::uint32_t low, std::uint32_t high) { return binary(spv::OpBitwiseOr, u64, widen(low), binary(spv::OpShiftLeftLogical, u64, widen(high), BdaConstant(state, 32u))); };
    const auto translation = [&](std::uint32_t device, std::uint32_t reason) {
        const auto value = state.module.AllocateId();
        state.module.AddFunction(spv::OpCompositeConstruct, result, value, device, reason);
        return value;
    };
    const auto fail = [&](std::uint32_t condition, BdaAbi::FaultReason reason) {
        ReturnValueIf(state, condition, state.module.Constant(spv::OpConstantComposite, result, BdaConstant(state, 0u), constant(static_cast<std::uint32_t>(reason))));
    };
    const auto function = state.module.AllocateId();
    state.module.AddName(function, "bda_translate");
    state.module.AddFunction(spv::OpFunction, result, function, spv::FunctionControlDontInlineMask, state.module.Type(spv::OpTypeFunction, result, u64, u32, u32));
    const auto address = state.module.AllocateId();
    const auto bytes = state.module.AllocateId();
    const auto permission = state.module.AllocateId();
    state.module.AddFunction(spv::OpFunctionParameter, u64, address);
    state.module.AddFunction(spv::OpFunctionParameter, u32, bytes);
    state.module.AddFunction(spv::OpFunctionParameter, u32, permission);
    EmitLabel(state, state.module.AllocateId());
    const auto variable = [&](std::uint32_t type) {
        const auto id = state.module.AllocateId();
        state.module.AddFunction(spv::OpVariable, TypePointer(state, spv::StorageClassFunction, type), id, spv::StorageClassFunction);
        return id;
    };
    const auto low = variable(u32);
    const auto high = variable(u32);
    const auto entry = variable(entryType);
    const auto shift = variable(u32);
    const auto length = state.module.AllocateId();
    state.module.AddFunction(spv::OpArrayLength, u32, length, state.bdaPagetableVariable, 0u);
    fail(binary(spv::OpULessThan, boolean, length, constant(BdaAbi::HeaderWords)), BdaAbi::FaultReason::InvalidTable);
    std::array<std::uint32_t, BdaAbi::HeaderWords> fields{};
    for (std::uint32_t word = 0; word < BdaAbi::HeaderWords; ++word) fields[word] = BdaLoadWord(state, constant(word));
    const auto differs = [&](std::uint32_t value, std::uint32_t expected) { return binary(spv::OpINotEqual, boolean, value, expected); };
    const auto either = [&](std::uint32_t left, std::uint32_t right) { return binary(spv::OpLogicalOr, boolean, left, right); };
    const auto count = fields[1];
    const auto available = binary(spv::OpISub, u32, length, constant(BdaAbi::HeaderWords));
    const auto reserved = binary(spv::OpBitwiseOr, u32, fields[3], binary(spv::OpBitwiseOr, u32, fields[BdaAbi::HeaderWords - 2u], fields[BdaAbi::HeaderWords - 1u]));
    const auto layout = either(either(differs(fields[0], constant(BdaAbi::TableVersion)), differs(fields[2], constant(sizeof(BdaAbi::Range)))), differs(reserved, constant(0)));
    const auto sized = either(differs(binary(spv::OpBitwiseAnd, u32, available, constant(7)), constant(0)), differs(count, binary(spv::OpShiftRightLogical, u32, available, constant(3))));
    fail(either(layout, sized), BdaAbi::FaultReason::InvalidTable);
    const auto end = binary(spv::OpIAdd, u64, address, widen(bytes));
    fail(binary(spv::OpULessThanEqual, boolean, end, address), BdaAbi::FaultReason::Overflow);
    const auto pages = pair(fields[BdaAbi::PageTableWord], fields[BdaAbi::PageTableWord + 1u]);
    const auto walk = binary(spv::OpLogicalAnd, boolean, binary(spv::OpINotEqual, boolean, pages, BdaConstant(state, 0u)), binary(spv::OpULessThan, boolean, address, BdaConstant(state, BdaAbi::PageTableEnd)));
    EmitIfCondition(state, walk, [&] {
        const auto window = binary(spv::OpShiftLeftLogical, u64, binary(spv::OpShiftRightLogical, u64, address, BdaConstant(state, BdaAbi::WindowShift)), BdaConstant(state, 3u));
        const auto leaf = loadDevice(u64, binary(spv::OpIAdd, u64, pages, window), sizeof(std::uint64_t));
        EmitIfCondition(state, binary(spv::OpINotEqual, boolean, leaf, BdaConstant(state, 0u)), [&] {
            const auto page = binary(spv::OpShiftLeftLogical, u64, binary(spv::OpBitwiseAnd, u64, binary(spv::OpShiftRightLogical, u64, address, BdaConstant(state, BdaAbi::PageShift)), BdaConstant(state, BdaAbi::WindowPages - 1u)), BdaConstant(state, 4u));
            state.module.AddFunction(spv::OpStore, entry, loadDevice(entryType, binary(spv::OpIAdd, u64, leaf, page), sizeof(BdaAbi::PageEntry)));
            state.module.AddFunction(spv::OpStore, shift, constant(BdaAbi::PageShift));
            const auto header = state.module.AllocateId();
            const auto body = state.module.AllocateId();
            const auto continuation = state.module.AllocateId();
            const auto merge = state.module.AllocateId();
            state.module.AddFunction(spv::OpBranch, header);
            EmitLabel(state, header);
            const auto current = load(entryType, entry);
            const auto level = load(u32, shift);
            const auto split = binary(spv::OpIEqual, boolean, binary(spv::OpShiftRightLogical, u32, component(current, 2u), constant(BdaAbi::KindShift)), constant(static_cast<std::uint32_t>(BdaAbi::PageKind::Split)));
            const auto descend = binary(spv::OpLogicalAnd, boolean, split, binary(spv::OpINotEqual, boolean, level, constant(0)));
            state.module.AddFunction(spv::OpLoopMerge, merge, continuation, spv::LoopControlMaskNone);
            state.module.AddFunction(spv::OpBranchConditional, descend, body, merge);
            EmitLabel(state, body);
            const auto next = binary(spv::OpISub, u32, level, constant(BdaAbi::SplitShift));
            const auto child = binary(spv::OpBitwiseAnd, u32, Unary(state, spv::OpUConvert, u32, binary(spv::OpShiftRightLogical, u64, address, widen(next))), constant(BdaAbi::SplitChildren - 1u));
            const auto children = binary(spv::OpIAdd, u64, leaf, pair(component(current, 0u), component(current, 1u)));
            state.module.AddFunction(spv::OpStore, entry, loadDevice(entryType, binary(spv::OpIAdd, u64, children, widen(binary(spv::OpShiftLeftLogical, u32, child, constant(4u)))), sizeof(BdaAbi::PageEntry)));
            state.module.AddFunction(spv::OpStore, shift, next);
            state.module.AddFunction(spv::OpBranch, continuation);
            EmitLabel(state, continuation);
            state.module.AddFunction(spv::OpBranch, header);
            EmitLabel(state, merge);
            const auto found = load(entryType, entry);
            const auto bounds = component(found, 2u);
            const auto kind = binary(spv::OpShiftRightLogical, u32, bounds, constant(BdaAbi::KindShift));
            fail(binary(spv::OpUGreaterThan, boolean, kind, constant(static_cast<std::uint32_t>(BdaAbi::PageKind::Segment))), BdaAbi::FaultReason::InvalidTable);
            const auto mask = binary(spv::OpISub, u32, binary(spv::OpShiftLeftLogical, u32, constant(1u), load(u32, shift)), constant(1u));
            const auto offset = widen(binary(spv::OpBitwiseAnd, u32, Unary(state, spv::OpUConvert, u32, address), mask));
            const auto first = widen(binary(spv::OpBitwiseAnd, u32, bounds, constant(BdaAbi::FirstMask)));
            const auto limit = pair(component(found, 3u), binary(spv::OpBitwiseAnd, u32, binary(spv::OpShiftRightLogical, u32, bounds, constant(BdaAbi::LimitHighShift)), constant(1u)));
            const auto granted = binary(spv::OpIEqual, boolean, binary(spv::OpBitwiseAnd, u32, binary(spv::OpShiftRightLogical, u32, bounds, constant(BdaAbi::PermissionShift)), permission), permission);
            const auto inside = binary(spv::OpLogicalAnd, boolean, binary(spv::OpUGreaterThanEqual, boolean, offset, first), binary(spv::OpULessThan, boolean, offset, limit));
            const auto segment = binary(spv::OpIEqual, boolean, kind, constant(static_cast<std::uint32_t>(BdaAbi::PageKind::Segment)));
            const auto held = binary(spv::OpLogicalAnd, boolean, binary(spv::OpLogicalAnd, boolean, segment, inside), granted);
            fail(binary(spv::OpLogicalAnd, boolean, held, binary(spv::OpUGreaterThan, boolean, binary(spv::OpIAdd, u64, offset, widen(bytes)), limit)), BdaAbi::FaultReason::Unmapped);
            ReturnValueIf(state, held, translation(binary(spv::OpIAdd, u64, address, pair(component(found, 0u), component(found, 1u))), constant(0)));
        });
    });
    state.module.AddFunction(spv::OpStore, low, constant(0));
    state.module.AddFunction(spv::OpStore, high, count);
    const auto header = state.module.AllocateId();
    const auto body = state.module.AllocateId();
    const auto continuation = state.module.AllocateId();
    const auto merge = state.module.AllocateId();
    state.module.AddFunction(spv::OpBranch, header);
    EmitLabel(state, header);
    const auto lower = load(u32, low);
    const auto upper = load(u32, high);
    const auto search = binary(spv::OpULessThan, boolean, lower, upper);
    state.module.AddFunction(spv::OpLoopMerge, merge, continuation, spv::LoopControlMaskNone);
    state.module.AddFunction(spv::OpBranchConditional, search, body, merge);
    EmitLabel(state, body);
    const auto midpoint = binary(spv::OpIAdd, u32, lower, binary(spv::OpShiftRightLogical, u32, binary(spv::OpISub, u32, upper, lower), constant(1)));
    const auto probe = binary(spv::OpIAdd, u32, constant(BdaAbi::HeaderWords), binary(spv::OpIMul, u32, midpoint, constant(8)));
    const auto before = binary(spv::OpULessThan, boolean, address, BdaLoadAddress(state, probe));
    const auto newLow = state.module.AllocateId();
    const auto newHigh = state.module.AllocateId();
    state.module.AddFunction(spv::OpSelect, u32, newLow, before, lower, binary(spv::OpIAdd, u32, midpoint, constant(1)));
    state.module.AddFunction(spv::OpSelect, u32, newHigh, before, midpoint, upper);
    state.module.AddFunction(spv::OpStore, low, newLow);
    state.module.AddFunction(spv::OpStore, high, newHigh);
    state.module.AddFunction(spv::OpBranch, continuation);
    EmitLabel(state, continuation);
    state.module.AddFunction(spv::OpBranch, header);
    EmitLabel(state, merge);
    const auto index = load(u32, low);
    fail(binary(spv::OpIEqual, boolean, index, constant(0)), BdaAbi::FaultReason::Unmapped);
    const auto selected = binary(spv::OpIAdd, u32, constant(BdaAbi::HeaderWords), binary(spv::OpIMul, u32, binary(spv::OpISub, u32, index, constant(1)), constant(8)));
    const auto at = [&](std::uint32_t offset) { return binary(spv::OpIAdd, u32, selected, constant(offset)); };
    const auto begin = BdaLoadAddress(state, selected);
    const auto finish = BdaLoadAddress(state, at(2));
    const auto base = BdaLoadAddress(state, at(4));
    const auto permissions = BdaLoadWord(state, at(6));
    fail(binary(spv::OpINotEqual, boolean, BdaLoadWord(state, at(7)), constant(0)), BdaAbi::FaultReason::InvalidTable);
    fail(binary(spv::OpUGreaterThanEqual, boolean, begin, finish), BdaAbi::FaultReason::InvalidTable);
    fail(binary(spv::OpUGreaterThan, boolean, end, finish), BdaAbi::FaultReason::Unmapped);
    fail(binary(spv::OpINotEqual, boolean, binary(spv::OpBitwiseAnd, u32, permissions, permission), permission), BdaAbi::FaultReason::Permission);
    fail(binary(spv::OpIEqual, boolean, base, BdaConstant(state, 0)), BdaAbi::FaultReason::InvalidTable);
    const auto device = binary(spv::OpIAdd, u64, base, binary(spv::OpISub, u64, address, begin));
    fail(binary(spv::OpULessThan, boolean, device, base), BdaAbi::FaultReason::Overflow);
    const auto deviceEnd = binary(spv::OpIAdd, u64, device, widen(bytes));
    fail(binary(spv::OpULessThanEqual, boolean, deviceEnd, device), BdaAbi::FaultReason::Overflow);
    state.module.AddFunction(spv::OpReturnValue, translation(device, constant(0)));
    state.module.AddFunction(spv::OpFunctionEnd);
    return function;
}

std::uint32_t DefineBdaLookup(SpirvEmitterState& state, const char* name, bool recordFaults, std::uint32_t permission = BdaAbi::Read) {
    const auto u32 = TypeU32(state);
    const auto u64 = TypeScalarU64(state);
    if (recordFaults && state.bdaFaultFunction == 0) throw std::runtime_error("BDA fault function is missing");
    const auto function = state.module.AllocateId();
    state.module.AddName(function, name);
    state.module.AddFunction(spv::OpFunction, u64, function, spv::FunctionControlDontInlineMask, state.module.Type(spv::OpTypeFunction, u64, u64, u32, u32));
    const auto address = state.module.AllocateId();
    const auto bytes = state.module.AllocateId();
    const auto instruction = state.module.AllocateId();
    state.module.AddFunction(spv::OpFunctionParameter, u64, address);
    state.module.AddFunction(spv::OpFunctionParameter, u32, bytes);
    state.module.AddFunction(spv::OpFunctionParameter, u32, instruction);
    EmitLabel(state, state.module.AllocateId());
    const auto translated = state.module.AllocateId();
    state.module.AddFunction(spv::OpFunctionCall, TranslationType(state), translated, state.bdaTranslateFunction, address, bytes, ConstantU32(state, permission));
    const auto device = state.module.AllocateId();
    state.module.AddFunction(spv::OpCompositeExtract, u64, device, translated, 0u);
    if (recordFaults) {
        const auto reason = state.module.AllocateId();
        state.module.AddFunction(spv::OpCompositeExtract, u32, reason, translated, 1u);
        EmitIfCondition(state, Binary(state, spv::OpINotEqual, TypeBool(state), reason, ConstantU32(state, 0u)), [&] {
            state.module.AddFunction(spv::OpFunctionCall, TypeVoid(state), state.module.AllocateId(), state.bdaFaultFunction, reason, address, bytes, instruction);
        });
    }
    state.module.AddFunction(spv::OpReturnValue, device);
    state.module.AddFunction(spv::OpFunctionEnd);
    return function;
}

}

static std::uint32_t DefineBdaNoteWrite(SpirvEmitterState& state) {
    const auto u32 = TypeU32(state);
    const auto boolean = TypeBool(state);
    const auto constant = [&](std::uint32_t value) { return ConstantU32(state, value); };
    const auto binary = [&](std::uint32_t op, std::uint32_t type, std::uint32_t left, std::uint32_t right) { return Binary(state, op, type, left, right); };
    const auto voidType = state.module.Type(spv::OpTypeVoid);
    const auto function = state.module.AllocateId();
    state.module.AddName(function, "note_bda_write");
    state.module.AddFunction(spv::OpFunction, voidType, function, spv::FunctionControlMaskNone, state.module.Type(spv::OpTypeFunction, voidType, TypeScalarU64(state)));
    const auto address = state.module.AllocateId();
    state.module.AddFunction(spv::OpFunctionParameter, TypeScalarU64(state), address);
    EmitLabel(state, state.module.AllocateId());
    const auto done = state.module.AllocateId();
    state.module.AddFunction(spv::OpVariable, TypePointer(state, spv::StorageClassFunction, boolean), done, spv::StorageClassFunction);
    const auto counter = state.module.AllocateId();
    state.module.AddFunction(spv::OpVariable, TypePointer(state, spv::StorageClassFunction, u32), counter, spv::StorageClassFunction);
    state.module.AddFunction(spv::OpStore, done, ConstantBool(state, false));
    state.module.AddFunction(spv::OpStore, counter, constant(0u));
    const auto u64 = TypeScalarU64(state);
    const auto key = binary(spv::OpIAdd, u32, Unary(state, spv::OpUConvert, u32, binary(spv::OpShiftRightLogical, u64, address, BdaConstant(state, BdaAbi::WrittenChunkShift))), constant(1));
    const auto page = binary(spv::OpBitwiseAnd, u32, Unary(state, spv::OpUConvert, u32, binary(spv::OpShiftRightLogical, u64, address, BdaConstant(state, BdaAbi::WrittenPageShift))), constant(BdaAbi::WrittenChunkPages - 1u));
    const auto bit = binary(spv::OpShiftLeftLogical, u32, constant(1u), page);
    const auto hash = binary(spv::OpShiftRightLogical, u32, binary(spv::OpIMul, u32, key, constant(0x9e3779b1u)), constant(32u - static_cast<std::uint32_t>(std::countr_zero(BdaAbi::WrittenPageSlots))));
    const auto scope = constant(spv::ScopeDevice);
    const auto relaxed = constant(spv::MemorySemanticsMaskNone);
    const auto header = state.module.AllocateId();
    const auto body = state.module.AllocateId();
    const auto continuation = state.module.AllocateId();
    const auto merge = state.module.AllocateId();
    state.module.AddFunction(spv::OpBranch, header);
    EmitLabel(state, header);
    const auto probe = state.module.AllocateId();
    state.module.AddFunction(spv::OpLoad, u32, probe, counter);
    const auto found = state.module.AllocateId();
    state.module.AddFunction(spv::OpLoad, boolean, found, done);
    const auto searching = binary(spv::OpLogicalAnd, boolean, Unary(state, spv::OpLogicalNot, boolean, found), binary(spv::OpULessThan, boolean, probe, constant(BdaAbi::WrittenPageProbes)));
    state.module.AddFunction(spv::OpLoopMerge, merge, continuation, spv::LoopControlMaskNone);
    state.module.AddFunction(spv::OpBranchConditional, searching, body, merge);
    EmitLabel(state, body);
    const auto slot = binary(spv::OpBitwiseAnd, u32, binary(spv::OpIAdd, u32, hash, probe), constant(BdaAbi::WrittenPageSlots - 1u));
    const auto keyWord = binary(spv::OpIAdd, u32, constant(BdaAbi::WrittenSlotsWord), binary(spv::OpShiftLeftLogical, u32, slot, constant(1u)));
    const auto current = state.module.AllocateId();
    state.module.AddFunction(spv::OpAtomicLoad, u32, current, BdaWord(state, state.faultBufferVariable, keyWord), scope, relaxed);
    const auto same = binary(spv::OpIEqual, boolean, current, key);
    state.module.AddFunction(spv::OpStore, done, same);
    EmitIfCondition(state, Unary(state, spv::OpLogicalNot, boolean, same), [&] {
        const auto previous = state.module.AllocateId();
        state.module.AddFunction(spv::OpAtomicCompareExchange, u32, previous, BdaWord(state, state.faultBufferVariable, keyWord), scope, relaxed, relaxed, key, constant(0u));
        const auto claimed = binary(spv::OpIEqual, boolean, previous, constant(0u));
        state.module.AddFunction(spv::OpStore, done, binary(spv::OpLogicalOr, boolean, claimed, binary(spv::OpIEqual, boolean, previous, key)));
        EmitIfCondition(state, claimed, [&] {
            const auto index = state.module.AllocateId();
            state.module.AddFunction(spv::OpAtomicIAdd, u32, index, BdaWord(state, state.faultBufferVariable, constant(BdaAbi::WrittenCountWord)), scope, relaxed, constant(1u));
            state.module.AddFunction(spv::OpAtomicStore, BdaWord(state, state.faultBufferVariable, binary(spv::OpIAdd, u32, constant(BdaAbi::WrittenListWord), index)), scope, relaxed, slot);
        });
    });
    const auto holds = state.module.AllocateId();
    state.module.AddFunction(spv::OpLoad, boolean, holds, done);
    EmitIfCondition(state, holds, [&] {
        const auto maskWord = binary(spv::OpIAdd, u32, keyWord, constant(1u));
        const auto mask = state.module.AllocateId();
        state.module.AddFunction(spv::OpAtomicLoad, u32, mask, BdaWord(state, state.faultBufferVariable, maskWord), scope, relaxed);
        EmitIfCondition(state, binary(spv::OpIEqual, boolean, binary(spv::OpBitwiseAnd, u32, mask, bit), constant(0u)), [&] {
            state.module.AddFunction(spv::OpAtomicOr, u32, state.module.AllocateId(), BdaWord(state, state.faultBufferVariable, maskWord), scope, relaxed, bit);
        });
    });
    state.module.AddFunction(spv::OpBranch, continuation);
    EmitLabel(state, continuation);
    state.module.AddFunction(spv::OpStore, counter, binary(spv::OpIAdd, u32, probe, constant(1u)));
    state.module.AddFunction(spv::OpBranch, header);
    EmitLabel(state, merge);
    const auto noted = state.module.AllocateId();
    state.module.AddFunction(spv::OpLoad, boolean, noted, done);
    EmitIfCondition(state, Unary(state, spv::OpLogicalNot, boolean, noted), [&] {
        state.module.AddFunction(spv::OpAtomicStore, BdaWord(state, state.faultBufferVariable, constant(BdaAbi::WrittenOverflowWord)), scope, relaxed, constant(1u));
    });
    state.module.AddFunction(spv::OpReturn);
    state.module.AddFunction(spv::OpFunctionEnd);
    return function;
}

void DefineGetBdaPointer(SpirvEmitterState& state) {
    if (state.faultBufferVariable != 0) DefineBdaFaultFunction(state);
    if (!state.program.Info().usesDma) return;
    state.bdaTranslateFunction = DefineBdaTranslate(state);
    state.bdaPointerFunction = DefineBdaLookup(state, "get_bda_pointer", true);
    if (!BdaByteReadsForced()) state.bdaProbeFunction = DefineBdaLookup(state, "probe_bda_pointer", false);
    DefineBdaDwordReadFunctions(state);
    DefineBdaSpanReadFunctions(state);
    if (state.program.Info().bdaWrites) {
        state.bdaWritePointerFunction = DefineBdaLookup(state, "get_bda_write_pointer", true, BdaAbi::Write);
        if (!BdaByteReadsForced()) state.bdaWriteProbeFunction = DefineBdaLookup(state, "probe_bda_write_pointer", false, BdaAbi::Write);
        state.bdaAtomicPointerFunction = DefineBdaLookup(state, "get_bda_atomic_pointer", true, BdaAbi::Read | BdaAbi::Write);
        state.bdaNoteWriteFunction = DefineBdaNoteWrite(state);
        DefineBdaByteWriteFunctions(state);
    }
}

}
