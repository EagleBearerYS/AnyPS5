#ifndef CORE_SHADER_RECOMPILIER_OPTIMIZATION_SRTWALKER_SRTPLANBUILDER_HPP
#define CORE_SHADER_RECOMPILIER_OPTIMIZATION_SRTWALKER_SRTPLANBUILDER_HPP

#include "IntermediateRepresentation/IrProgram.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ShaderRecompiler::Detail {

class PlanBuilder {
public:
    PlanBuilder(IrProgram& program, bool deviceTables) : _program(program), _deviceTables(deviceTables) {}

    void Run();

private:
    struct Patch {
        IrValue* inst = nullptr;
        std::uint32_t slot = 0;
        bool keep = false;
    };

    bool ReadsAtRunTime() const;
    void Collect(IrValue* raw, std::uint32_t usePc);
    void Record(IrValue* inst);
    void PatchReads();

    IrProgram& _program;
    bool _deviceTables = false;
    std::vector<IrValue*> _visiting;
    std::unordered_map<IrValue*, std::size_t> _visitingIndex;
    std::unordered_set<IrValue*> _visited;
    std::vector<Patch> _patches;
};

}

#endif
