#include "Optimization/SrtWalker/SrtPlanBuilder.hpp"
#include "Optimization/SrtWalker/SrtDiagnostics.hpp"
#include "Optimization/SrtWalker/SrtInstructionPredicates.hpp"
#include "Optimization/SrtWalker/SrtRuntimeValidator.hpp"

#include "IntermediateRepresentation/IrBuilder.hpp"

#include <algorithm>
#include <stdexcept>

namespace ShaderRecompiler::Detail {

void PlanBuilder::Run() {
    _program.Resources().srtReads.clear();
    _program.Metadata().dynamicReads.clear();
    const bool runtime = ReadsAtRunTime();
    for (auto& block : _program.Blocks()) {
        for (IrValue* inst : block->Instructions()) {
            const auto op = inst->Opcode();
            if (op == IrOpcode::LoadAddressU32 || op == IrOpcode::ReadConstBuffer) {
                const auto flags = inst->Flags<MemoryFlags>();
                if (flags.index < _program.Resources().memoryInfo.size()) {
                    const auto kind = _program.Resources().memoryInfo[flags.index].kind;
                    const bool crosswired = (op == IrOpcode::LoadAddressU32 && kind == ResourceKind::ScalarBuffer) || (op == IrOpcode::ReadConstBuffer && kind == ResourceKind::ScalarAddress);
                    if (crosswired) {
                        Fail(_program.Resources(), flags.pc, std::string(IrOpcodeName(op)) + " has incompatible scalar memory metadata");
                    }
                }
            }
            if (!runtime && IsDescriptorHandle(inst->Opcode())) {
                for (std::size_t index = 0; index < inst->ArgumentCount(); index++) {
                    Collect(inst->Argument(index), 0);
                }
            }
        }
    }
    if (runtime) {
        IrBuilder builder(_program);
        for (auto& block : _program.Blocks()) {
            std::vector<std::pair<IrValue*, IrValue*>> reads;
            const auto& instructions = block->Instructions();
            for (auto it = instructions.begin(); it != instructions.end(); ++it) {
                if ((*it)->Opcode() != IrOpcode::LoadAddressU32 || !IsRawRead(_program.Resources(), **it)) continue;
                const auto next = std::next(it);
                if (next != instructions.end()) reads.emplace_back(*it, *next);
            }
            for (const auto& [read, next] : reads) {
                builder.SetInsertionPoint(*block);
                IrValue& keep = builder.Emit(IrOpcode::ReferenceU32, IrType::Void, {read});
                block->RemoveInstruction(&keep);
                block->InsertInstructionBefore(next, &keep);
            }
        }
        return;
    }
    for (auto& block : _program.Blocks()) {
        for (IrValue* inst : block->Instructions()) {
            if (inst->Opcode() == IrOpcode::LoadAddressU32 && IsRawRead(_program.Resources(), *inst) && inst->Argument(1)->Resolve()->HasImmediate() && RuntimeValidator(_program.Resources(), RuntimeValueType::Any).Run(inst)) {
                Collect(inst, inst->Flags<MemoryFlags>().pc);
            }
            if (inst->Opcode() == IrOpcode::GetLinkedWord) {
                Collect(inst, 0);
            }
        }
    }
    PatchReads();
}

bool PlanBuilder::ReadsAtRunTime() const {
    if (!_deviceTables || _program.Resources().stage != IrShaderStage::Compute) return false;
    const auto readsMemory = [](const IrValue* root) {
        std::vector<const IrValue*> pending{root};
        std::unordered_set<const IrValue*> seen;
        while (!pending.empty()) {
            const IrValue* value = pending.back()->Resolve();
            pending.pop_back();
            if (!seen.insert(value).second) continue;
            if (value->Opcode() == IrOpcode::LoadAddressU32 || value->Opcode() == IrOpcode::GetLinkedWord) return true;
            for (std::size_t index = 0; index < value->ArgumentCount(); index++) pending.push_back(value->Argument(index));
        }
        return false;
    };
    for (const auto& info : _program.Metadata().blockInfo) {
        if (info.indirectTarget != nullptr) return false;
        if (info.condition != nullptr && readsMemory(info.condition)) return false;
    }
    const auto dynamicWords = [](const IrValue& handle) {
        std::vector<const IrValue*> pending;
        for (std::size_t index = 0; index < handle.ArgumentCount(); index++) pending.push_back(handle.Argument(index));
        std::unordered_set<const IrValue*> seen;
        while (!pending.empty()) {
            const IrValue* value = pending.back()->Resolve();
            pending.pop_back();
            if (!seen.insert(value).second) continue;
            if (value->Opcode() == IrOpcode::LoadAddressU32 && !value->Argument(1)->Resolve()->HasImmediate()) return true;
            if (value->Opcode() == IrOpcode::Phi || value->Opcode() == IrOpcode::GetShaderBase) return true;
            for (std::size_t index = 0; index < value->ArgumentCount(); index++) pending.push_back(value->Argument(index));
        }
        return false;
    };
    const auto& memoryInfo = _program.Resources().memoryInfo;
    const auto deviceRead = [&](const IrValue& user, const IrValue& handle, bool sampler) {
        switch (user.Opcode()) {
            case IrOpcode::ImageSampleRaw:
            case IrOpcode::ImageGatherRaw:
            case IrOpcode::ImageQueryLod:
                break;
            case IrOpcode::ImageRead:
            case IrOpcode::ImageQueryDimensions:
                if (sampler) return false;
                break;
            default:
                return false;
        }
        const auto index = user.Flags<MemoryFlags>().index;
        if (index >= memoryInfo.size() || memoryInfo[index].imagePacked || memoryInfo[index].imageByElements != 0u) return false;
        const std::size_t operand = sampler ? 1u : 0u;
        if (user.ArgumentCount() <= operand || user.Argument(operand)->Resolve() != &handle) return false;
        for (std::size_t index = 0; index < user.ArgumentCount(); index++) {
            if (index != operand && user.Argument(index)->Resolve() == &handle) return false;
        }
        return true;
    };
    for (const auto& block : _program.Blocks()) {
        for (const IrValue* inst : block->Instructions()) {
            const auto op = inst->Opcode();
            if (op == IrOpcode::GetLinkedWord) return false;
            if (op != IrOpcode::GetImageResource && op != IrOpcode::GetSamplerResource) continue;
            if (dynamicWords(*inst)) return false;
            for (const IrValue* user : inst->Uses()) {
                if (!deviceRead(*user, *inst, op == IrOpcode::GetSamplerResource)) return false;
            }
        }
    }
    return true;
}

void PlanBuilder::Collect(IrValue* raw, std::uint32_t usePc) {
    struct Frame {
        IrValue* inst = nullptr;
        std::size_t next = 0;
    };
    std::vector<Frame> stack;
    const auto enter = [&](IrValue* candidate) {
        IrValue* inst = candidate->Resolve();
        if (inst->Opcode() == IrOpcode::Void) {
            return;
        }
        if (const auto cycle = _visitingIndex.find(inst); cycle != _visitingIndex.end()) {
            const auto containsPhi = std::any_of(_visiting.begin() + static_cast<std::ptrdiff_t>(cycle->second), _visiting.end(), [](IrValue* candidate) { return candidate->Opcode() == IrOpcode::Phi; });
            if (containsPhi) {
                return;
            }
            Fail(_program.Resources(), usePc, "cyclic typed planning value " + std::string(IrOpcodeName(inst->Opcode())) + " without a phi");
        }
        if (_visited.contains(inst)) {
            return;
        }
        _visitingIndex.emplace(inst, _visiting.size());
        _visiting.push_back(inst);
        stack.push_back({inst, 0});
    };
    enter(raw);
    while (!stack.empty()) {
        auto& frame = stack.back();
        if (frame.next < frame.inst->ArgumentCount()) {
            IrValue* argument = frame.inst->Argument(frame.next);
            frame.next++;
            enter(argument);
            continue;
        }
        IrValue* inst = frame.inst;
        stack.pop_back();
        _visiting.pop_back();
        _visitingIndex.erase(inst);
        _visited.insert(inst);
        Record(inst);
    }
}

void PlanBuilder::Record(IrValue* inst) {
    const bool linked = inst->Opcode() == IrOpcode::GetLinkedWord;
    if (!linked && !IsRawRead(_program.Resources(), *inst)) {
        return;
    }
    IrValue* offset = inst->Argument(linked ? 0u : 1u)->Resolve();
    const auto foldable = offset->HasImmediate() && offset->Type() == IrType::U32 && RuntimeValidator(_program.Resources(), RuntimeValueType::Any).Run(inst);
    if (!foldable) {
        if (std::find(_program.Metadata().dynamicReads.begin(), _program.Metadata().dynamicReads.end(), inst) == _program.Metadata().dynamicReads.end()) {
            _program.Metadata().dynamicReads.push_back(inst);
        }
        return;
    }
    for (std::uint32_t slot = 0; slot < _program.Resources().srtReads.size(); slot++) {
        if (EquivalentValue(_program.Resources(), inst, _program.Resources().srtReads[slot].value)) {
            _patches.push_back({inst, slot, false});
            return;
        }
    }
    const auto slot = static_cast<std::uint32_t>(_program.Resources().srtReads.size());
    _program.Resources().srtReads.push_back({inst, slot});
    _patches.push_back({inst, slot, true});
}

void PlanBuilder::PatchReads() {
    IrBuilder builder(_program);
    for (const auto& patch : _patches) {
        IrBlock* block = patch.inst->Parent();
        if (block == nullptr) {
            throw std::runtime_error("SrtWalker::BuildPlan patched value has no parent block");
        }
        builder.SetInsertionPoint(*block);
        IrValue& resource = builder.Emit(IrOpcode::GetSrtResource, IrType::SrtResource, {});
        block->RemoveInstruction(&resource);
        block->InsertInstructionBefore(patch.inst, &resource);
        IrValue& slotConstant = builder.Constant(patch.slot);
        IrValue& flat = builder.Emit(IrOpcode::ReadConst, IrType::U32, {&resource, &slotConstant});
        block->RemoveInstruction(&flat);
        block->InsertInstructionBefore(patch.inst, &flat);
        patch.inst->ReplaceAllUsesWith(&flat);
        for (auto& info : _program.Metadata().blockInfo) {
            if (info.condition != nullptr && info.condition->Resolve() == patch.inst) {
                info.condition = &flat;
            }
            if (info.indirectTarget != nullptr && info.indirectTarget->Resolve() == patch.inst) {
                info.indirectTarget = &flat;
            }
        }
        if (patch.keep) {
            if (IsRawRead(_program.Resources(), *patch.inst)) {
                _program.Resources().memoryInfo[patch.inst->Flags<MemoryFlags>().index].planningOnly = true;
            }
            const auto& irVal = builder.Emit(IrOpcode::ReferenceU32, IrType::Void, {patch.inst});
        }
    }
}

}
