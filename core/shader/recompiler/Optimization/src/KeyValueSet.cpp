#include "Optimization/KeyValueSet.hpp"
#include "Optimization/ShaderStageInputInfo.hpp"
#include "Optimization/SrtWalker/SrtEvaluator.hpp"
#include "ImageTableAbi.hpp"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace ShaderRecompiler {

namespace {

constexpr std::size_t SetLimit = ImageTableAbi::MaxKeys;
constexpr std::uint64_t ProductLimit = std::uint64_t{1} << 22u;
constexpr std::uint64_t ElementLimit = std::uint64_t{1} << 20u;
constexpr std::uint64_t ScanBytes = std::uint64_t{64} << 20u;
constexpr std::size_t VisitLimit = 4096u;
constexpr std::uint32_t LaneLimit = 64u;

struct Values {
    bool bounded = true;
    bool cyclic = false;
    std::vector<std::uint32_t> set;
};

Values Unbounded() {
    Values values;
    values.bounded = false;
    return values;
}

Values Single(std::uint32_t value) {
    Values values;
    values.set.push_back(value);
    return values;
}

Values Normalized(Values values) {
    std::sort(values.set.begin(), values.set.end());
    values.set.erase(std::unique(values.set.begin(), values.set.end()), values.set.end());
    if (values.set.size() > SetLimit) return Unbounded();
    return values;
}

class KeyEvaluator {
public:
    KeyEvaluator(const IrResourcePlan& plan, const SrtRuntime& runtime, std::vector<std::pair<std::uint64_t, std::uint64_t>>& ranges, IrValue* key)
        : plan(plan), runtime(runtime), ranges(ranges), uniform(plan, runtime) {
        CollectExecFlags(key);
    }

    Values Evaluate(IrValue* raw) {
        IrValue* value = raw->Resolve();
        if (const auto found = done.find(value); found != done.end()) return found->second;
        if (active.contains(value)) {
            Values cycle;
            cycle.cyclic = true;
            return cycle;
        }
        if (++visits > VisitLimit) return Unbounded();
        active.insert(value);
        auto result = Compute(*value);
        active.erase(value);
        if (!result.cyclic) done.emplace(value, result);
        return result;
    }

private:
    void CollectExecFlags(IrValue* key) {
        std::vector<IrValue*> pending{key};
        std::unordered_set<IrValue*> seen;
        while (!pending.empty()) {
            IrValue* value = pending.back()->Resolve();
            pending.pop_back();
            if (!seen.insert(value).second) continue;
            const auto op = value->Opcode();
            if ((op == IrOpcode::LoadBufferU32 || op == IrOpcode::LoadBufferU32x2 || op == IrOpcode::LoadBufferU32x3 || op == IrOpcode::LoadBufferU32x4) && value->ArgumentCount() == 5u) {
                execFlags.insert(value->Argument(4)->Resolve());
            }
            for (std::size_t index = 0; index < value->ArgumentCount(); index++) {
                if (value->Argument(index) != nullptr) pending.push_back(value->Argument(index));
            }
        }
    }

    Values Compute(IrValue& inst) {
        switch (inst.Opcode()) {
            case IrOpcode::ReadFirstLane:
            case IrOpcode::BitCastU32F32:
            case IrOpcode::BitCastF32U32: return Evaluate(inst.Argument(0));
            case IrOpcode::Phi: return Union(inst);
            case IrOpcode::SelectU32:
            case IrOpcode::SelectF32: return Select(inst);
            case IrOpcode::LoadBufferU32: return Load(inst, 0u);
            case IrOpcode::CompositeExtractU32x2:
            case IrOpcode::CompositeExtractU32x3:
            case IrOpcode::CompositeExtractU32x4: return Extract(inst);
            case IrOpcode::LaneId: {
                Values lanes;
                for (std::uint32_t lane = 0; lane < LaneLimit; lane++) lanes.set.push_back(lane);
                return lanes;
            }
            case IrOpcode::GetBuiltin: return Builtin(inst);
            case IrOpcode::BitwiseAnd32: return Binary(inst, [](std::uint32_t a, std::uint32_t b) { return a & b; });
            case IrOpcode::BitwiseOr32: return Binary(inst, [](std::uint32_t a, std::uint32_t b) { return a | b; });
            case IrOpcode::BitwiseXor32: return Binary(inst, [](std::uint32_t a, std::uint32_t b) { return a ^ b; });
            case IrOpcode::ShiftLeftLogical32: return Binary(inst, [](std::uint32_t a, std::uint32_t b) { return a << (b & 31u); });
            case IrOpcode::ShiftRightLogical32: return Binary(inst, [](std::uint32_t a, std::uint32_t b) { return a >> (b & 31u); });
            case IrOpcode::ShiftRightArithmetic32: return Binary(inst, [](std::uint32_t a, std::uint32_t b) { return static_cast<std::uint32_t>(static_cast<std::int32_t>(a) >> (b & 31u)); });
            case IrOpcode::IAdd32: return Binary(inst, [](std::uint32_t a, std::uint32_t b) { return a + b; });
            case IrOpcode::ISub32: return Binary(inst, [](std::uint32_t a, std::uint32_t b) { return a - b; });
            case IrOpcode::IMul32: return Binary(inst, [](std::uint32_t a, std::uint32_t b) { return a * b; });
            case IrOpcode::UMin32: return Binary(inst, [](std::uint32_t a, std::uint32_t b) { return std::min(a, b); });
            case IrOpcode::SMin32: return Binary(inst, [](std::uint32_t a, std::uint32_t b) { return static_cast<std::uint32_t>(std::min(static_cast<std::int32_t>(a), static_cast<std::int32_t>(b))); });
            case IrOpcode::BitwiseNot32: return Binary(inst, [](std::uint32_t a, std::uint32_t) { return ~a; }, 1u);
            case IrOpcode::BitFieldUExtract: return FieldExtract(inst);
            default: break;
        }
        std::uint64_t result = 0;
        if (uniform.EvaluateWide(&inst, result)) return Single(static_cast<std::uint32_t>(result));
        return Unbounded();
    }

    Values Builtin(IrValue& inst) {
        if (inst.ArgumentCount() != 2u || !inst.Argument(0)->Resolve()->HasImmediate() || !inst.Argument(1)->Resolve()->HasImmediate()) return Unbounded();
        const auto kind = static_cast<StageInputKind>(inst.Argument(0)->Resolve()->ImmediateU32());
        const auto component = inst.Argument(1)->Resolve()->ImmediateU32();
        const auto& threads = plan.workgroupThreads;
        std::uint64_t count = 0;
        if (kind == StageInputKind::LocalInvocationId && component < threads.size()) count = threads[component];
        if (kind == StageInputKind::LocalInvocationIndex) count = std::uint64_t{threads[0]} * threads[1] * threads[2];
        if (count == 0u || count > SetLimit) return Unbounded();
        Values values;
        for (std::uint32_t value = 0; value < count; value++) values.set.push_back(value);
        return values;
    }

    Values Union(IrValue& phi) {
        Values result;
        for (std::size_t index = 0; index < phi.ArgumentCount(); index++) {
            const auto incoming = Evaluate(phi.Argument(index));
            if (!incoming.bounded) return Unbounded();
            result.cyclic = result.cyclic || incoming.cyclic;
            result.set.insert(result.set.end(), incoming.set.begin(), incoming.set.end());
        }
        return Normalized(std::move(result));
    }

    Values Select(IrValue& select) {
        if (select.ArgumentCount() != 3u) return Unbounded();
        std::uint64_t condition = 0;
        if (uniform.EvaluateWide(select.Argument(0), condition)) return Evaluate(select.Argument(condition != 0u ? 1u : 2u));
        if (execFlags.contains(select.Argument(0)->Resolve())) return Evaluate(select.Argument(1));
        auto chosen = Evaluate(select.Argument(1));
        const auto other = Evaluate(select.Argument(2));
        if (!chosen.bounded || !other.bounded) return Unbounded();
        chosen.cyclic = chosen.cyclic || other.cyclic;
        chosen.set.insert(chosen.set.end(), other.set.begin(), other.set.end());
        return Normalized(std::move(chosen));
    }

    Values Binary(IrValue& inst, const std::function<std::uint32_t(std::uint32_t, std::uint32_t)>& op, std::uint32_t arguments = 2u) {
        if (inst.ArgumentCount() != arguments) return Unbounded();
        const auto left = Evaluate(inst.Argument(0));
        const auto right = arguments == 2u ? Evaluate(inst.Argument(1)) : Single(0u);
        if (!left.bounded || !right.bounded || left.cyclic || right.cyclic) return Unbounded();
        if (static_cast<std::uint64_t>(left.set.size()) * right.set.size() > ProductLimit) return Unbounded();
        Values result;
        for (const auto a : left.set) {
            for (const auto b : right.set) result.set.push_back(op(a, b));
        }
        return Normalized(std::move(result));
    }

    Values FieldExtract(IrValue& inst) {
        if (inst.ArgumentCount() != 3u) return Unbounded();
        const auto value = Evaluate(inst.Argument(0));
        const auto offsets = Evaluate(inst.Argument(1));
        const auto widths = Evaluate(inst.Argument(2));
        if (!value.bounded || !offsets.bounded || !widths.bounded || value.cyclic || offsets.cyclic || widths.cyclic) return Unbounded();
        if (static_cast<std::uint64_t>(value.set.size()) * offsets.set.size() * widths.set.size() > ProductLimit) return Unbounded();
        Values result;
        for (const auto offset : offsets.set) {
            for (const auto width : widths.set) {
                if (offset > 32u || width > 32u - offset) return Unbounded();
                const auto mask = width == 32u ? 0xffffffffu : (std::uint32_t{1} << width) - 1u;
                for (const auto word : value.set) result.set.push_back(width == 0u ? 0u : (word >> offset) & mask);
            }
        }
        return Normalized(std::move(result));
    }

    Values Extract(IrValue& inst) {
        IrValue* source = inst.Argument(0)->Resolve();
        IrValue* index = inst.Argument(1)->Resolve();
        const auto op = source->Opcode();
        if (index->HasImmediate() && (op == IrOpcode::LoadBufferU32x2 || op == IrOpcode::LoadBufferU32x3 || op == IrOpcode::LoadBufferU32x4)) return Load(*source, index->ImmediateU32());
        std::uint64_t result = 0;
        if (uniform.EvaluateWide(&inst, result)) return Single(static_cast<std::uint32_t>(result));
        return Unbounded();
    }

    bool ReadWord(std::uint64_t address, std::uint32_t& word) {
        if (runtime.readWords != nullptr) return runtime.readWords(runtime.userContext, address, &word, 1u);
        return runtime.readMemory != nullptr && runtime.readMemory(runtime.userContext, address, &word);
    }

    bool Readable(std::uint64_t address, std::uint64_t bytes) {
        if (runtime.accessible != nullptr && !runtime.accessible(runtime.userContext, address, bytes)) return false;
        return runtime.pendingWrite == nullptr || !runtime.pendingWrite(runtime.userContext, address, bytes);
    }

    Values Load(IrValue& load, std::uint32_t component) {
        const auto memoryIndex = load.Flags<MemoryFlags>().index;
        if (memoryIndex >= plan.memoryInfo.size() || load.ArgumentCount() != 5u) return Unbounded();
        const auto& memory = plan.memoryInfo[memoryIndex];
        if (memory.kind != ResourceKind::Buffer || memory.typed || memory.formatted || memory.dataBits != 32u || component >= memory.dataDwords) return Unbounded();
        IrValue* handle = load.Argument(0)->Resolve();
        if (handle->Opcode() != IrOpcode::GetBufferResource || handle->ArgumentCount() != 4u) return Unbounded();
        ShaderBufferResource descriptor{};
        for (std::uint32_t dword = 0; dword < 4u; dword++) {
            std::uint64_t word = 0;
            if (!uniform.EvaluateWide(handle->Argument(dword), word)) return Unbounded();
            descriptor.fields[dword] = static_cast<std::uint32_t>(word);
        }
        if (descriptor.Type() != 0u || descriptor.SwizzleEnabled() || descriptor.AddTid()) return Unbounded();
        const auto soffsets = Evaluate(load.Argument(3));
        const auto voffsets = memory.offen ? Evaluate(load.Argument(2)) : Single(0u);
        if (!soffsets.bounded || !voffsets.bounded || soffsets.cyclic || voffsets.cyclic || soffsets.set.size() != 1u) return Unbounded();
        const std::uint64_t base = descriptor.Base48();
        const std::uint64_t size = descriptor.GetSize();
        const std::uint64_t stride = descriptor.Stride();
        const std::uint64_t immediate = static_cast<std::uint64_t>(memory.offset) + soffsets.set[0] + component * 4u;
        Values result = Single(0u);
        const auto read = [&](std::uint64_t offset) {
            std::uint32_t word = 0;
            if (offset + 4u > size || !Readable(base + offset, 4u) || !ReadWord(base + offset, word)) return false;
            result.set.push_back(word);
            return true;
        };
        if (!memory.idxen || stride == 0u) {
            if (memory.idxen) return Unbounded();
            for (const auto voffset : voffsets.set) {
                if (immediate + voffset + 4u <= size && !read(immediate + voffset)) return Unbounded();
            }
        } else {
            const auto indices = Evaluate(load.Argument(1));
            if (indices.cyclic) return Unbounded();
            if (indices.bounded) {
                if (static_cast<std::uint64_t>(indices.set.size()) * voffsets.set.size() > ProductLimit) return Unbounded();
                for (const auto index : indices.set) {
                    for (const auto voffset : voffsets.set) {
                        const auto field = immediate + voffset;
                        if (index < descriptor.NumRecords() && field + 4u <= stride && !read(index * stride + field)) return Unbounded();
                    }
                }
            } else {
                const std::uint64_t records = descriptor.NumRecords();
                if (records == 0u || stride % 4u != 0u || records > ElementLimit) return records == 0u ? result : Unbounded();
                for (const auto voffset : voffsets.set) {
                    const auto field = immediate + voffset;
                    if (field + 4u > stride) continue;
                    const auto bytes = (records - 1u) * stride + 4u;
                    if (bytes > ScanBytes || field % 4u != 0u || !Readable(base + field, bytes)) return Unbounded();
                    std::vector<std::uint32_t> words(static_cast<std::size_t>(bytes / 4u));
                    if (runtime.readWords != nullptr) {
                        if (!runtime.readWords(runtime.userContext, base + field, words.data(), words.size())) return Unbounded();
                    } else {
                        for (std::size_t word = 0; word < words.size(); word++) {
                            if (!ReadWord(base + field + word * 4u, words[word])) return Unbounded();
                        }
                    }
                    for (std::uint64_t record = 0; record < records; record++) result.set.push_back(words[static_cast<std::size_t>(record * stride / 4u)]);
                    result = Normalized(std::move(result));
                    if (!result.bounded) return result;
                }
            }
        }
        ranges.emplace_back(base, size);
        return Normalized(std::move(result));
    }

    const IrResourcePlan& plan;
    const SrtRuntime& runtime;
    std::vector<std::pair<std::uint64_t, std::uint64_t>>& ranges;
    Detail::Evaluator uniform;
    std::unordered_map<IrValue*, Values> done;
    std::unordered_set<IrValue*> active;
    std::unordered_set<IrValue*> execFlags;
    std::size_t visits = 0;
};

}

std::optional<std::vector<std::uint32_t>> EvaluateKeyValues(const IrResourcePlan& plan, IrValue* key, const SrtRuntime& runtime, std::vector<std::pair<std::uint64_t, std::uint64_t>>& ranges) {
    if (key == nullptr) return std::nullopt;
    KeyEvaluator evaluator(plan, runtime, ranges, key);
    auto values = evaluator.Evaluate(key);
    if (!values.bounded) return std::nullopt;
    return std::move(values.set);
}

}
