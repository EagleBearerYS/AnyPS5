#ifndef CORE_SHADER_RECOMPILIER_OPTIMIZATION_INCLUDE_OPTIMIZATION_NANCHECKELIMINATOR_HPP
#define CORE_SHADER_RECOMPILIER_OPTIMIZATION_INCLUDE_OPTIMIZATION_NANCHECKELIMINATOR_HPP

#include "IntermediateRepresentation/IrProgram.hpp"
#include <cstdint>

namespace ShaderRecompiler {

struct NanCheckEliminationStats {
    std::uint32_t removedSelects = 0;
    std::uint32_t removedDefaultNans = 0;

    [[nodiscard]] bool Changed() const { return removedSelects + removedDefaultNans != 0u; }
};

class NanCheckEliminator {
public:
    [[nodiscard]] NanCheckEliminationStats Eliminate(IrProgram& program) const;
};

}

#endif
