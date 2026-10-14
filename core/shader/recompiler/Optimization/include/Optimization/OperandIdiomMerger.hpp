#ifndef CORE_SHADER_RECOMPILIER_OPTIMIZATION_INCLUDE_OPTIMIZATION_OPERANDIDIOMMERGER_HPP
#define CORE_SHADER_RECOMPILIER_OPTIMIZATION_INCLUDE_OPTIMIZATION_OPERANDIDIOMMERGER_HPP

#include "IntermediateRepresentation/IrProgram.hpp"
#include <cstdint>

namespace ShaderRecompiler {

struct OperandIdiomMergeStats {
    std::uint32_t mergedFlushes = 0;
    std::uint32_t mergedNanTests = 0;
    std::uint32_t mergedBitCasts = 0;

    [[nodiscard]] bool Changed() const { return mergedFlushes + mergedNanTests + mergedBitCasts != 0u; }
};

class OperandIdiomMerger {
public:
    [[nodiscard]] OperandIdiomMergeStats Merge(IrProgram& program) const;
};

}

#endif
