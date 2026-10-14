#ifndef CORE_SHADER_RECOMPILIER_OPTIMIZATION_INCLUDE_OPTIMIZATION_DOMINATORTREE_HPP
#define CORE_SHADER_RECOMPILIER_OPTIMIZATION_INCLUDE_OPTIMIZATION_DOMINATORTREE_HPP

#include "IntermediateRepresentation/IrProgram.hpp"
#include <cstdint>
#include <utility>
#include <vector>

namespace ShaderRecompiler {

class DominatorTree {
public:
    explicit DominatorTree(const IrProgram& program);

    [[nodiscard]] const std::vector<std::pair<IrBlock*, std::uint32_t>>& Preorder() const;

private:
    std::vector<std::pair<IrBlock*, std::uint32_t>> preorder;
};

}

#endif
