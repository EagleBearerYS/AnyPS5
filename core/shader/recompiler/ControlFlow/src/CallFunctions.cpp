#include "ControlFlow/CallFunctions.hpp"
#include "ControlFlow/ControlFlowHelpers.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace ShaderRecompiler {

namespace {

struct CallSite {
    const GuestCall* call = nullptr;
    std::uint32_t callBlock = InvalidControlFlowId;
    std::uint32_t exitBlock = InvalidControlFlowId;
    std::uint32_t continuationBlock = InvalidControlFlowId;
    std::vector<std::uint32_t> blocks;
    std::vector<bool> member;
};

std::runtime_error callError(const GuestCall& call, const std::string& reason) {
    return std::runtime_error("the guest call at program counter " + toHexString(call.programCounter) + " cannot become a function: " + reason);
}

CallSite findRegion(const ControlFlowGraph& graph, const GuestCall& call, std::uint32_t callBlock) {
    CallSite site;
    site.call = &call;
    site.callBlock = callBlock;
    site.member.assign(graph.blocks.size(), false);
    const auto& caller = graph.FindBlock(callBlock);
    if (caller.successors.size() != 1u) throw callError(call, "its block has " + std::to_string(caller.successors.size()) + " successors");
    const auto entry = caller.successors.front();
    std::vector<std::uint32_t> pending{entry};
    std::vector<std::uint32_t> exits;
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (site.member[id]) continue;
        if (id == callBlock) throw callError(call, "its callee reaches the call again");
        site.member[id] = true;
        site.blocks.push_back(id);
        const auto& block = graph.FindBlock(id);
        if (block.instructionEnd > block.instructionBegin && block.instructionEnd - 1u == call.returnInstruction) {
            exits.push_back(id);
            continue;
        }
        for (const auto successor : block.successors) pending.push_back(successor);
    }
    if (exits.size() != 1u) throw callError(call, "its callee has " + std::to_string(exits.size()) + " returning blocks");
    const auto& exit = graph.FindBlock(exits.front());
    if (exit.successors.size() != 1u || site.member[exit.successors.front()]) throw callError(call, "its return does not leave the callee for one block");
    site.exitBlock = exit.id;
    site.continuationBlock = exit.successors.front();
    for (const auto id : site.blocks) {
        for (const auto predecessor : graph.FindBlock(id).predecessors) {
            if (site.member[predecessor] || (predecessor == callBlock && id == entry)) continue;
            throw callError(call, "control flow enters its callee at block " + std::to_string(id) + " from block " + std::to_string(predecessor));
        }
    }
    std::sort(site.blocks.begin(), site.blocks.end());
    return site;
}

}

void AssignCallFunctions(ControlFlowGraph& graph) {
    for (auto& block : graph.blocks) block.function = 0;
    graph.functions.clear();
    std::vector<CallSite> sites;
    for (const auto& block : graph.blocks) {
        if (block.instructionEnd == block.instructionBegin) continue;
        const auto call = std::find_if(graph.calls.begin(), graph.calls.end(), [&](const GuestCall& candidate) { return candidate.callInstruction == block.instructionEnd - 1u; });
        if (call != graph.calls.end()) sites.push_back(findRegion(graph, *call, block.id));
    }
    if (sites.empty()) return;
    std::vector<std::size_t> bySize(sites.size());
    for (std::size_t index = 0; index < sites.size(); ++index) bySize[index] = index;
    std::stable_sort(bySize.begin(), bySize.end(), [&](std::size_t first, std::size_t second) { return sites[first].blocks.size() > sites[second].blocks.size(); });
    std::vector<std::uint32_t> owner(graph.blocks.size(), 0u);
    for (const auto index : bySize) {
        for (const auto id : sites[index].blocks) owner[id] = static_cast<std::uint32_t>(index + 1u);
    }
    std::vector<std::uint32_t> parents(sites.size() + 1u, 0u);
    for (std::size_t index = 0; index < sites.size(); ++index) parents[index + 1u] = owner[sites[index].callBlock];
    const auto encloses = [&](std::uint32_t outer, std::uint32_t inner) {
        for (auto function = inner; function != 0u; function = parents[function]) {
            if (function == outer) return true;
        }
        return outer == 0u;
    };
    for (std::size_t index = 0; index < sites.size(); ++index) {
        const auto& site = sites[index];
        const auto function = static_cast<std::uint32_t>(index + 1u);
        const auto parent = parents[function];
        for (const auto id : site.blocks) {
            if (!encloses(function, owner[id]) || (parent != 0u && !sites[parent - 1u].member[id])) throw callError(*site.call, "its callee overlaps the callee of another call at block " + std::to_string(id));
        }
        if (owner[site.continuationBlock] != parent) throw callError(*site.call, "it returns to block " + std::to_string(site.continuationBlock) + " of another function");
    }
    for (const auto& block : graph.blocks) {
        const auto function = owner[block.id];
        const auto& terminator = block.terminator;
        const auto check = [&](std::uint32_t target, const char* what) {
            if (target == InvalidControlFlowId || owner[target] == function) return;
            const auto& site = sites[(function != 0u ? function : owner[target]) - 1u];
            throw callError(*site.call, std::string("the ") + what + " of block " + std::to_string(block.id) + " lies in another function");
        };
        check(terminator.mergeBlock, "merge block");
        if (terminator.loopHeader) check(terminator.continueBlock, "continue block");
    }
    for (auto& block : graph.blocks) block.function = owner[block.id];
    for (std::size_t index = 0; index < sites.size(); ++index) {
        graph.functions.push_back({parents[index + 1u], sites[index].callBlock, sites[index].exitBlock, sites[index].continuationBlock});
    }
}

}
