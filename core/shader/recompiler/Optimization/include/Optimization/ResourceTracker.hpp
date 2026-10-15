#ifndef CORE_SHADER_RECOMPILIER_OPTIMIZATION_INCLUDE_OPTIMIZATION_RESOURCETRACKER_HPP
#define CORE_SHADER_RECOMPILIER_OPTIMIZATION_INCLUDE_OPTIMIZATION_RESOURCETRACKER_HPP

#include "IntermediateRepresentation/IrProgram.hpp"

namespace ShaderRecompiler {

class ResourceTracker {
public:
    [[nodiscard]] bool SplitDescriptorPointers(IrProgram& program) const;
    void Track(IrProgram& program) const;
};

}

#endif
