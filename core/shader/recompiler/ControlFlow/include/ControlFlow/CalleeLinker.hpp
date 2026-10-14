#ifndef CORE_SHADER_RECOMPILIER_CONTROLFLOW_INCLUDE_CONTROLFLOW_CALLEELINKER_HPP
#define CORE_SHADER_RECOMPILIER_CONTROLFLOW_INCLUDE_CONTROLFLOW_CALLEELINKER_HPP

#include "ControlFlow/GraphBuilder.hpp"
#include "Recompiler.hpp"
#include <cstdint>
#include <span>

namespace ShaderRecompiler {

struct CalleeMemory {
    void* context = nullptr;
    bool (*readCode)(void* context, std::uint64_t address, std::span<std::uint32_t> words) = nullptr;
    bool (*readData)(void* context, std::uint64_t address, std::span<std::uint32_t> words) = nullptr;
    void (*observeTable)(void* context, const TableCall& call, std::uint64_t base, std::span<const std::uint32_t> table) = nullptr;
};

[[nodiscard]] LinkedCode LinkCallees(const RecompileRequest& request, const CalleeMemory& memory);

}

#endif
