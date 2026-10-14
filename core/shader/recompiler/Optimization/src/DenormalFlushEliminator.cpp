#include "Optimization/DenormalFlushEliminator.hpp"
#include "Optimization/FloatIdioms.hpp"
#include <cstdint>
#include <unordered_map>

namespace ShaderRecompiler {

namespace {

class DenormalFreeValues {
public:
    bool Contains(const IrValue* value) {
        value = value->Resolve();
        if (const auto found = known.find(value); found != known.end()) return found->second;
        known.emplace(value, false);
        const bool result = compute(*value);
        known[value] = result;
        return result;
    }

private:
    bool compute(const IrValue& value) {
        if (value.HasImmediate()) {
            const std::uint32_t bits = value.ImmediateU32();
            return (bits & 0x7f800000u) != 0u || (bits & 0x7fffffffu) == 0u;
        }
        if (FlushedF32Source(value) != nullptr) return true;
        switch (value.Opcode()) {
            case IrOpcode::ConvertIToF32:
            case IrOpcode::ConvertUToF32:
            case IrOpcode::ConvertF32S32:
            case IrOpcode::ConvertF32U32:
            case IrOpcode::ConvertF32F16:
                return true;
            case IrOpcode::BitCastU32F32:
            case IrOpcode::BitCastF32U32:
            case IrOpcode::FPAbs32:
            case IrOpcode::FPNeg32:
            case IrOpcode::FPSaturate32:
                return Contains(value.Argument(0));
            case IrOpcode::BitwiseXor32:
                if (const IrValue* operand = MaskedOperand(value, IrOpcode::BitwiseXor32, 0x80000000u)) return Contains(operand);
                return false;
            case IrOpcode::BitwiseAnd32:
                if (const IrValue* operand = MaskedOperand(value, IrOpcode::BitwiseAnd32, 0x7fffffffu)) return Contains(operand);
                return false;
            case IrOpcode::SelectU32:
            case IrOpcode::SelectF32:
                return Contains(value.Argument(1)) && Contains(value.Argument(2));
            case IrOpcode::Phi:
                for (std::size_t index = 0; index < value.ArgumentCount(); ++index) {
                    if (!Contains(value.Argument(index))) return false;
                }
                return true;
            default:
                return false;
        }
    }

    std::unordered_map<const IrValue*, bool> known;
};

}

DenormalFlushEliminationStats DenormalFlushEliminator::Eliminate(IrProgram& program) const {
    DenormalFlushEliminationStats stats;
    DenormalFreeValues denormalFree;
    for (auto* block : program.BlockOrder()) {
        for (auto* inst : block->Instructions()) {
            if (!inst->HasUses()) continue;
            IrValue* source = FlushedF32Source(*inst);
            if (source == nullptr || !denormalFree.Contains(source)) continue;
            inst->ReplaceAllUsesWith(source);
            ++stats.removedFlushes;
        }
    }
    return stats;
}

}
