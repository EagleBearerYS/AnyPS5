#ifndef CORE_SHADER_RECOMPILER_OPTIMIZATION_KEYVALUESET_HPP
#define CORE_SHADER_RECOMPILER_OPTIMIZATION_KEYVALUESET_HPP

#include "IntermediateRepresentation/IrProgram.hpp"
#include "Optimization/SrtWalker.hpp"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace ShaderRecompiler {

[[nodiscard]] std::optional<std::vector<std::uint32_t>> EvaluateKeyValues(const IrResourcePlan& plan, IrValue* key, const SrtRuntime& runtime, std::vector<std::pair<std::uint64_t, std::uint64_t>>& ranges);

}

#endif
