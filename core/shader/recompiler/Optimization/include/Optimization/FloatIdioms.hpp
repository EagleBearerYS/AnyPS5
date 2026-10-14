#ifndef CORE_SHADER_RECOMPILIER_OPTIMIZATION_INCLUDE_OPTIMIZATION_FLOATIDIOMS_HPP
#define CORE_SHADER_RECOMPILIER_OPTIMIZATION_INCLUDE_OPTIMIZATION_FLOATIDIOMS_HPP

#include "IntermediateRepresentation/IrValue.hpp"
#include <cstdint>

namespace ShaderRecompiler {

[[nodiscard]] IrValue* MaskedOperand(const IrValue& value, IrOpcode opcode, std::uint32_t bits);
[[nodiscard]] IrValue* FlushedF32Source(const IrValue& value);
[[nodiscard]] IrValue* NanTestedF32Source(const IrValue& value);
[[nodiscard]] IrValue* QuietedF32Source(const IrValue& value);
[[nodiscard]] IrValue* MagnitudeSource(const IrValue& value);

}

#endif
