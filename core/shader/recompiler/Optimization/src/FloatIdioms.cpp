#include "Optimization/FloatIdioms.hpp"

namespace ShaderRecompiler {

namespace {

bool isImmediate(const IrValue* value, std::uint32_t bits) {
    return value->HasImmediate() && value->ImmediateU32() == bits;
}

}

IrValue* MaskedOperand(const IrValue& value, IrOpcode opcode, std::uint32_t bits) {
    if (value.Opcode() != opcode || value.ArgumentCount() != 2u) return nullptr;
    IrValue* lhs = value.Argument(0)->Resolve();
    IrValue* rhs = value.Argument(1)->Resolve();
    if (isImmediate(rhs, bits)) return lhs;
    if (isImmediate(lhs, bits)) return rhs;
    return nullptr;
}

IrValue* FlushedF32Source(const IrValue& value) {
    if (value.Opcode() != IrOpcode::SelectU32 || value.ArgumentCount() != 3u) return nullptr;
    IrValue* source = value.Argument(2)->Resolve();
    const IrValue* exponent = MaskedOperand(*value.Argument(0)->Resolve(), IrOpcode::IEqual32, 0u);
    if (exponent == nullptr || MaskedOperand(*exponent, IrOpcode::BitwiseAnd32, 0x7f800000u) != source) return nullptr;
    return MaskedOperand(*value.Argument(1)->Resolve(), IrOpcode::BitwiseAnd32, 0x80000000u) == source ? source : nullptr;
}

IrValue* NanTestedF32Source(const IrValue& value) {
    if (value.Opcode() != IrOpcode::UGreaterThan32 || value.ArgumentCount() != 2u || !isImmediate(value.Argument(1)->Resolve(), 0x7f800000u)) return nullptr;
    return MaskedOperand(*value.Argument(0)->Resolve(), IrOpcode::BitwiseAnd32, 0x7fffffffu);
}

IrValue* QuietedF32Source(const IrValue& value) {
    return MaskedOperand(value, IrOpcode::BitwiseOr32, 0x00400000u);
}

IrValue* MagnitudeSource(IrValue& value) {
    IrValue* current = value.Resolve();
    while (true) {
        IrValue* inner = MaskedOperand(*current, IrOpcode::BitwiseAnd32, 0x7fffffffu);
        if (inner == nullptr) inner = MaskedOperand(*current, IrOpcode::BitwiseXor32, 0x80000000u);
        if (inner == nullptr) return current;
        current = inner;
    }
}

}
