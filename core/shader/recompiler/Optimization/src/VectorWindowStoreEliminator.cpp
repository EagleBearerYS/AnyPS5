#include "Optimization/VectorWindowStoreEliminator.hpp"
#include <bitset>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace ShaderRecompiler {

namespace {

using WindowSlots = std::bitset<NumVectorRegs>;

struct SlotFlow {
    WindowSlots read;
    WindowSlots written;
    WindowSlots liveIn;
};

const IrValue* storedSlot(const IrValue& store) {
    const IrValue* slot = store.Argument(0)->Resolve();
    return slot->HasImmediate() ? slot : nullptr;
}

WindowSlots loadedSlots(const IrValue& load, const WindowSlots& window) {
    const IrValue* slot = load.Argument(0)->Resolve();
    if (!slot->HasImmediate()) return window;
    WindowSlots single;
    single.set(slot->ImmediateU32());
    return single;
}

SlotFlow summarize(const IrBlock& block, const WindowSlots& window) {
    SlotFlow flow;
    const auto& instructions = block.Instructions();
    for (auto it = instructions.rbegin(); it != instructions.rend(); ++it) {
        const IrValue& inst = **it;
        if (inst.Opcode() == IrOpcode::LoadVectorWindow) {
            flow.read |= loadedSlots(inst, window);
        } else if (inst.Opcode() == IrOpcode::StoreVectorWindow) {
            if (const IrValue* slot = storedSlot(inst)) {
                flow.read.reset(slot->ImmediateU32());
                flow.written.set(slot->ImmediateU32());
            }
        }
    }
    return flow;
}

}

VectorWindowStoreEliminationStats VectorWindowStoreEliminator::Eliminate(IrProgram& program) const {
    VectorWindowStoreEliminationStats stats;
    const auto& metadata = program.Metadata();
    if (metadata.vectorWindowSize == 0u || metadata.vectorWindowIndexedWrites) return stats;
    WindowSlots window;
    for (std::uint32_t slot = 0; slot < metadata.vectorWindowSize; ++slot) window.set(slot);

    const auto& blocks = program.BlockOrder();
    std::unordered_map<const IrBlock*, std::size_t> indices;
    indices.reserve(blocks.size());
    for (std::size_t index = 0; index < blocks.size(); ++index) indices.emplace(blocks[index], index);
    std::vector<SlotFlow> flows;
    flows.reserve(blocks.size());
    for (const IrBlock* block : blocks) flows.push_back(summarize(*block, window));

    const auto liveOut = [&](const IrBlock& block) {
        WindowSlots live;
        for (const IrBlock* successor : block.Successors()) live |= flows[indices.at(successor)].liveIn;
        return live;
    };
    for (bool changed = true; changed;) {
        changed = false;
        for (std::size_t index = blocks.size(); index-- > 0;) {
            auto& flow = flows[index];
            const WindowSlots liveIn = flow.read | (liveOut(*blocks[index]) & ~flow.written);
            if (liveIn == flow.liveIn) continue;
            flow.liveIn = liveIn;
            changed = true;
        }
    }

    for (IrBlock* block : blocks) {
        WindowSlots live = liveOut(*block);
        auto& instructions = block->Instructions();
        for (auto it = instructions.end(); it != instructions.begin();) {
            --it;
            IrValue* inst = *it;
            if (inst->Opcode() == IrOpcode::LoadVectorWindow) {
                live |= loadedSlots(*inst, window);
                continue;
            }
            if (inst->Opcode() != IrOpcode::StoreVectorWindow) continue;
            const IrValue* slot = storedSlot(*inst);
            if (slot == nullptr) continue;
            if (live.test(slot->ImmediateU32())) {
                live.reset(slot->ImmediateU32());
                continue;
            }
            inst->Invalidate();
            it = instructions.erase(it);
            inst->SetParent(nullptr);
            ++stats.removedStores;
        }
    }
    return stats;
}

}
