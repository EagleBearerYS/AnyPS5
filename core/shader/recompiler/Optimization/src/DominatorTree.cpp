#include "Optimization/DominatorTree.hpp"
#include <algorithm>
#include <cstddef>

namespace ShaderRecompiler {

DominatorTree::DominatorTree(const IrProgram& program) {
    const auto& order = program.BlockOrder();
    if (order.empty()) return;
    std::uint32_t blockCount = 0;
    for (const auto& block : program.Blocks()) blockCount = std::max(blockCount, block->Id() + 1u);
    constexpr std::uint32_t Unvisited = 0xffffffffu;
    std::vector<std::uint32_t> number(blockCount, Unvisited);
    std::vector<IrBlock*> postorder;
    IrBlock* entry = order.front();
    std::vector<std::pair<IrBlock*, std::size_t>> stack{{entry, 0u}};
    number[entry->Id()] = 0u;
    while (!stack.empty()) {
        IrBlock* block = stack.back().first;
        const std::size_t next = stack.back().second;
        if (next < block->Successors().size()) {
            ++stack.back().second;
            IrBlock* successor = block->Successors()[next];
            if (number[successor->Id()] != Unvisited) continue;
            number[successor->Id()] = 0u;
            stack.emplace_back(successor, 0u);
            continue;
        }
        number[block->Id()] = static_cast<std::uint32_t>(postorder.size());
        postorder.push_back(block);
        stack.pop_back();
    }
    std::vector<IrBlock*> idom(blockCount, nullptr);
    idom[entry->Id()] = entry;
    const auto intersect = [&](IrBlock* left, IrBlock* right) {
        while (left != right) {
            while (number[left->Id()] < number[right->Id()]) left = idom[left->Id()];
            while (number[right->Id()] < number[left->Id()]) right = idom[right->Id()];
        }
        return left;
    };
    for (bool changed = true; changed;) {
        changed = false;
        for (auto it = postorder.rbegin(); it != postorder.rend(); ++it) {
            IrBlock* block = *it;
            if (block == entry) continue;
            IrBlock* dominator = nullptr;
            for (IrBlock* predecessor : block->Predecessors()) {
                if (number[predecessor->Id()] == Unvisited || idom[predecessor->Id()] == nullptr) continue;
                dominator = dominator == nullptr ? predecessor : intersect(predecessor, dominator);
            }
            if (dominator != nullptr && idom[block->Id()] != dominator) {
                idom[block->Id()] = dominator;
                changed = true;
            }
        }
    }
    std::vector<std::vector<IrBlock*>> children(blockCount);
    for (auto it = postorder.rbegin(); it != postorder.rend(); ++it) {
        if (*it != entry) children[idom[(*it)->Id()]->Id()].push_back(*it);
    }
    std::vector<std::pair<IrBlock*, std::size_t>> walk{{entry, 0u}};
    preorder.reserve(postorder.size());
    preorder.emplace_back(entry, 0u);
    while (!walk.empty()) {
        IrBlock* block = walk.back().first;
        const std::size_t next = walk.back().second;
        const auto& below = children[block->Id()];
        if (next == below.size()) {
            walk.pop_back();
            continue;
        }
        ++walk.back().second;
        preorder.emplace_back(below[next], static_cast<std::uint32_t>(walk.size()));
        walk.emplace_back(below[next], 0u);
    }
}

const std::vector<std::pair<IrBlock*, std::uint32_t>>& DominatorTree::Preorder() const {
    return preorder;
}

}
