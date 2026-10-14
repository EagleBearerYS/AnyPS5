#include "Optimization/OperandIdiomMerger.hpp"
#include "Optimization/DominatorTree.hpp"
#include "Optimization/FloatIdioms.hpp"
#include <cstddef>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ShaderRecompiler {

namespace {

enum class Idiom : std::uint8_t { Flush, NanTest, IsNan, Quiet, F32View, U32View };

struct IdiomKey {
    Idiom idiom = Idiom::Flush;
    const IrValue* source = nullptr;

    bool operator==(const IdiomKey&) const = default;
};

struct IdiomKeyHash {
    std::size_t operator()(const IdiomKey& key) const {
        return std::hash<const IrValue*>{}(key.source) * 7u + static_cast<std::size_t>(key.idiom);
    }
};

std::optional<IdiomKey> keyOf(const IrValue& value) {
    if (const IrValue* source = FlushedF32Source(value)) return IdiomKey{Idiom::Flush, source};
    if (IrValue* source = NanTestedF32Source(value)) return IdiomKey{Idiom::NanTest, MagnitudeSource(*source)};
    if (const IrValue* source = QuietedF32Source(value)) return IdiomKey{Idiom::Quiet, source};
    switch (value.Opcode()) {
        case IrOpcode::FPIsNan32: return IdiomKey{Idiom::IsNan, value.Argument(0)->Resolve()};
        case IrOpcode::BitCastF32U32: return IdiomKey{Idiom::F32View, value.Argument(0)->Resolve()};
        case IrOpcode::BitCastU32F32: return IdiomKey{Idiom::U32View, value.Argument(0)->Resolve()};
        default: return std::nullopt;
    }
}

}

OperandIdiomMergeStats OperandIdiomMerger::Merge(IrProgram& program) const {
    OperandIdiomMergeStats stats;
    const DominatorTree tree(program);
    std::unordered_map<IdiomKey, IrValue*, IdiomKeyHash> known;
    std::vector<IdiomKey> added;
    std::vector<std::pair<std::uint32_t, std::size_t>> scopes;
    for (const auto& [block, depth] : tree.Preorder()) {
        while (!scopes.empty() && scopes.back().first >= depth) {
            for (std::size_t index = scopes.back().second; index < added.size(); ++index) known.erase(added[index]);
            added.resize(scopes.back().second);
            scopes.pop_back();
        }
        scopes.emplace_back(depth, added.size());
        for (IrValue* inst : block->Instructions()) {
            if (!inst->HasUses()) continue;
            const auto key = keyOf(*inst);
            if (!key.has_value()) continue;
            const auto [found, inserted] = known.emplace(*key, inst);
            if (inserted) {
                added.push_back(*key);
                continue;
            }
            inst->ReplaceAllUsesWith(found->second);
            switch (key->idiom) {
                case Idiom::Flush: ++stats.mergedFlushes; break;
                case Idiom::NanTest:
                case Idiom::IsNan:
                case Idiom::Quiet: ++stats.mergedNanTests; break;
                case Idiom::F32View:
                case Idiom::U32View: ++stats.mergedBitCasts; break;
            }
        }
    }
    return stats;
}

}
