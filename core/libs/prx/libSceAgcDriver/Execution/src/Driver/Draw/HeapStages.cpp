#include "prx/libSceAgcDriver/Execution/include/Driver/Draw/HeapStages.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawPipeline.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Synchronization/DeferredLabels.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/BindlessHeap.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
#include "prx/libc/include/HostThreadLocal.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace AgcDriver::DriverDetail {

namespace {

std::atomic<std::uint64_t>& heapStages() {
    static std::atomic<std::uint64_t> count{0};
    return count;
}

bool sameResult(const ShaderRecompiler::RecompileResult& left, const ShaderRecompiler::RecompileResult& right) {
    bool equal = left.PipelineVariantId() == right.PipelineVariantId() && left.pushConstants == right.pushConstants && left.bindings.size() == right.bindings.size() && left.specialization.size() == right.specialization.size();
    for (std::size_t binding = 0; equal && binding < left.bindings.size(); ++binding) {
        const auto& a = left.bindings[binding];
        const auto& b = right.bindings[binding];
        equal = a.role == b.role && a.kind == b.kind && a.binding == b.binding && a.count == b.count && a.guestDescriptor == b.guestDescriptor;
    }
    for (std::size_t constant = 0; equal && constant < left.specialization.size(); ++constant) equal = left.specialization[constant].id == right.specialization[constant].id && left.specialization[constant].value == right.specialization[constant].value;
    equal = equal && left.vertexAttributes.size() == right.vertexAttributes.size();
    for (std::size_t attribute = 0; equal && attribute < left.vertexAttributes.size(); ++attribute) {
        const auto& a = left.vertexAttributes[attribute];
        const auto& b = right.vertexAttributes[attribute];
        equal = a.location == b.location && a.components == b.components && a.resource.fields == b.resource.fields && a.fetchIndex == b.fetchIndex && a.formatComponents == b.formatComponents;
    }
    return equal;
}

struct VerifyCounts {
    std::atomic<std::uint64_t> verified{0}, differ{0};
};

VerifyCounts& verifyCounts() {
    static VerifyCounts counts;
    static std::once_flag registered;
    std::call_once(registered, [] {
        std::atexit([] {
            auto& c = verifyCounts();
            if (c.verified.load() != 0u) std::fprintf(stderr, "[heap-verify] %llu heap stages compared with a full capture, %llu differed\n", static_cast<unsigned long long>(c.verified.load()), static_cast<unsigned long long>(c.differ.load()));
        });
    });
    return counts;
}

}

bool HeapFragmentsEnabled() {
    return Graphics::GpuHeapEnabled();
}

bool HeapStageEligible(ShaderRecompiler::ShaderStage stage, const ShaderRecompiler::IrResourcePlan* plan, bool gpuConstantsVertex) {
    if (plan == nullptr || (stage != ShaderRecompiler::ShaderStage::Fragment && stage != ShaderRecompiler::ShaderStage::Vertex)) return false;
    if (stage == ShaderRecompiler::ShaderStage::Vertex && !(gpuConstantsVertex && plan->gpuConstants)) return false;
    return (!plan->info.images.empty() || (gpuConstantsVertex && plan->srtReads.size() == plan->gpuConstantCheckSlots)) && ShaderRecompiler::HeapFragmentEligible(*plan);
}

namespace {

struct HeapFragmentKey {
    const void* snapshot;
    std::size_t codeOffset;
    bool operator==(const HeapFragmentKey&) const = default;
};

struct HeapFragmentKeyHash {
    std::size_t operator()(const HeapFragmentKey& key) const { return std::hash<const void*>{}(key.snapshot) ^ (key.codeOffset * 0x9e3779b97f4a7c15ull); }
};

std::unordered_map<HeapFragmentKey, bool, HeapFragmentKeyHash>& heapFragments() {
    struct FragmentsStorage {};
    auto& value = HostThreadLocal<std::unordered_map<HeapFragmentKey, bool, HeapFragmentKeyHash>, FragmentsStorage>();
    return value;
}

}

std::optional<bool> HeapFragmentKnown(const void* snapshot, std::size_t codeOffset) {
    const auto& known = heapFragments();
    const auto found = known.find({snapshot, codeOffset});
    return found == known.end() ? std::nullopt : std::optional(found->second);
}

void NoteHeapFragment(const void* snapshot, std::size_t codeOffset, bool eligible) {
    heapFragments()[{snapshot, codeOffset}] = eligible;
}

bool SampleHeapStageVerify() {
    static const std::uint64_t every = [] {
        const char* text = std::getenv("APS5_VERIFY_GPU_HEAP");
        if (text == nullptr) return std::uint64_t{0};
        const auto value = std::strtoull(text, nullptr, 10);
        return value == 0 ? std::uint64_t{1} : static_cast<std::uint64_t>(value);
    }();
    if (every == 0) return false;
    thread_local std::uint64_t calls = 0;
    return calls++ % every == 0;
}

namespace {

struct HeapWalkEntry {
    std::uint64_t variant = 0;
    std::uint64_t shaderBase = 0;
    std::vector<std::uint32_t> userData;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
    std::vector<std::uint32_t> values;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> accessible;
    std::shared_ptr<const ShaderRecompiler::RecompileResult> result;
    std::shared_ptr<const ShaderRecompiler::HeapStageWords> words;
};

constexpr std::size_t HeapWalkWays = 4;

std::unordered_map<std::uint64_t, std::vector<HeapWalkEntry>>& heapWalkMemo() {
    struct WalkMemoStorage {};
    auto& memo = HostThreadLocal<std::unordered_map<std::uint64_t, std::vector<HeapWalkEntry>>, WalkMemoStorage>();
    return memo;
}

}

std::shared_ptr<const ShaderRecompiler::RecompileResult> MaterializeHeapStage(const ShaderRecompiler::PreparedShaderInvocation& invocation, std::span<const std::uint32_t> userData, std::uint64_t shaderBase, ShaderRecompiler::HeapStageWords* words, std::shared_ptr<const ShaderRecompiler::HeapStageWords>* sharedWords) {
    const auto finish = [&](const std::shared_ptr<const ShaderRecompiler::ResourceCapture>& capture) {
        if (words != nullptr) {
            if (auto shared = invocation.MaterializeHeap(*capture, *words)) return shared;
        }
        return invocation.Materialize(*capture);
    };
    {
        struct Reads {
            std::vector<std::uint64_t> words;
            std::uint64_t page = ~std::uint64_t{0};
            std::vector<std::pair<std::uint64_t, std::uint64_t>> accessible;
            bool failed = false;
        };
        struct ReadsStorage {};
        struct RangesStorage {};
        auto& reads = HostThreadLocal<Reads, ReadsStorage>();
        auto& ranges = HostThreadLocal<std::vector<std::pair<std::uint64_t, std::uint64_t>>, RangesStorage>();
        reads.words.clear();
        reads.accessible.clear();
        reads.failed = false;
        reads.page = ~std::uint64_t{0};
        const auto before = Graphics::Recorder::PendingWriteSnapshot();
        const auto generation = GuestMemory::TrackerGeneration();
        const bool memoOn = words != nullptr && !invocation.Request().context.vertex.has_value();
        std::uint64_t memoKey = 0;
        if (memoOn) {
            memoKey = invocation.VariantId() * 0x9e3779b97f4a7c15ull ^ shaderBase;
            for (const auto word : userData) memoKey = (memoKey ^ word) * 0x100000001b3ull;
            auto& memo = heapWalkMemo();
            const auto found = memo.find(memoKey);
            for (std::size_t way = 0; found != memo.end() && way < found->second.size(); ++way) {
                const auto& entry = found->second[way];
                bool equal = entry.variant == invocation.VariantId() && entry.shaderBase == shaderBase && std::equal(entry.userData.begin(), entry.userData.end(), userData.begin(), userData.end());
                if (!equal) continue;
                std::size_t cursor = 0;
                for (std::size_t r = 0; equal && r < entry.ranges.size(); ++r) {
                    const auto [begin, end] = entry.ranges[r];
                    const auto count = static_cast<std::size_t>((end - begin) / 4u);
                    equal = GuestMemory::CompareMapped(begin, std::as_bytes(std::span(entry.values.data() + cursor, count))) == GuestMemory::Compare::Equal;
                    cursor += count;
                }
                for (std::size_t a = 0; equal && a < entry.accessible.size(); ++a) equal = GuestMemory::Accessible(reinterpret_cast<const void*>(entry.accessible[a].first), static_cast<std::size_t>(entry.accessible[a].second));
                if (!equal) {
                    continue;
                }
                bool settled = !(DrawPipeline::Active() && DrawPipeline::Queue0().Busy() && DrawPipeline::Queue0().OverlapsAny(entry.ranges)) && Graphics::Recorder::ReadsSettled(entry.ranges, before.get());
                for (std::size_t r = 0; settled && r < entry.ranges.size(); ++r) settled = GuestMemory::NewestStamp(entry.ranges[r].first, static_cast<std::size_t>(entry.ranges[r].second - entry.ranges[r].first)) <= generation;
                if (!settled) {
                    break;
                }
                for (const auto& [begin, end] : entry.ranges) GuestMemory::GuardRead(begin, static_cast<std::size_t>(end - begin));
                if (sharedWords != nullptr) *sharedWords = entry.words;
                else *words = *entry.words;
                auto result = entry.result;
                if (way != 0) std::rotate(found->second.begin(), found->second.begin() + static_cast<std::ptrdiff_t>(way), found->second.begin() + static_cast<std::ptrdiff_t>(way) + 1);
                return result;
            }
        }
        ShaderRecompiler::SrtRuntime plain;
        plain.userData = userData;
        plain.shaderBase = shaderBase;
        plain.userContext = &reads;
        plain.readMemory = [](void* context, std::uint64_t address, std::uint32_t* value) {
            auto& self = *static_cast<Reads*>(context);
            const auto page = address & ~std::uint64_t{0xfff};
            if (page != self.page || (address & 0xfffu) > 0x1000u - sizeof(*value)) {
                if (GuestMemory::Accessible(reinterpret_cast<const void*>(page), 0x1000u)) self.page = page;
                else if (!GuestMemory::Accessible(reinterpret_cast<const void*>(address), sizeof(*value))) {
                    self.failed = true;
                    return false;
                }
            }
            std::memcpy(value, reinterpret_cast<const void*>(address), sizeof(*value));
            self.words.push_back(address);
            return true;
        };
        plain.accessible = [](void* context, std::uint64_t address, std::uint64_t bytes) {
            auto& self = *static_cast<Reads*>(context);
            const bool accessible = GuestMemory::Accessible(reinterpret_cast<const void*>(address), static_cast<std::size_t>(bytes));
            if (accessible) self.accessible.emplace_back(address, bytes);
            else self.failed = true;
            return accessible;
        };
        auto capture = invocation.Capture(plain);
        std::sort(reads.words.begin(), reads.words.end());
        ranges.clear();
        for (const auto address : reads.words) {
            if (!ranges.empty() && address <= ranges.back().second) {
                ranges.back().second = std::max(ranges.back().second, address + 4u);
                continue;
            }
            ranges.emplace_back(address, address + 4u);
        }
        struct ValuesStorage {};
        auto& values = HostThreadLocal<std::vector<std::uint32_t>, ValuesStorage>();
        if (memoOn && !reads.failed) {
            values.clear();
            for (const auto& [begin, end] : ranges) {
                const auto offset = values.size();
                values.resize(offset + static_cast<std::size_t>((end - begin) / 4u));
                std::memcpy(values.data() + offset, reinterpret_cast<const void*>(begin), static_cast<std::size_t>(end - begin));
            }
        }
        bool settled = !(DrawPipeline::Active() && DrawPipeline::Queue0().Busy() && DrawPipeline::Queue0().OverlapsAny(ranges)) && Graphics::Recorder::ReadsSettled(ranges, before.get());
        for (std::size_t i = 0; settled && i < ranges.size(); ++i) settled = GuestMemory::NewestStamp(ranges[i].first, static_cast<std::size_t>(ranges[i].second - ranges[i].first)) <= generation;
        if (settled) {
            for (const auto& [begin, end] : ranges) GuestMemory::GuardRead(begin, static_cast<std::size_t>(end - begin));
            auto result = finish(capture);
            if (memoOn && !reads.failed && result != nullptr) {
                auto& memo = heapWalkMemo();
                if (memo.size() >= 16384) memo.clear();
                auto& ways = memo[memoKey];
                if (ways.size() >= HeapWalkWays) ways.pop_back();
                ways.emplace(ways.begin());
                auto& entry = ways.front();
                entry.variant = invocation.VariantId();
                entry.shaderBase = shaderBase;
                entry.userData.assign(userData.begin(), userData.end());
                entry.ranges = ranges;
                entry.values = values;
                entry.accessible = reads.accessible;
                entry.result = result;
                entry.words = std::make_shared<const ShaderRecompiler::HeapStageWords>(*words);
            }
            return result;
        }
    }
    return nullptr;
}

std::uint64_t HeapStagesTaken() {
    return heapStages().load();
}

std::shared_ptr<const ShaderRecompiler::RecompileResult> PatchedHeapResult(const ShaderRecompiler::RecompileResult& shared, const ShaderRecompiler::HeapStageWords& words) {
    auto result = std::make_shared<ShaderRecompiler::RecompileResult>(shared);
    for (auto& binding : result->bindings) {
        if (binding.role == ShaderRecompiler::DescriptorRole::FlattenedSrt && !words.flattenedSrt.empty()) binding.guestDescriptor = words.flattenedSrt;
        if (binding.role == ShaderRecompiler::DescriptorRole::ShaderData) std::copy_n(words.userData.begin(), std::min(words.userData.size(), binding.guestDescriptor.size()), binding.guestDescriptor.begin());
    }
    if (!words.bindings.empty()) {
        if (words.bindings.size() != result->bindings.size()) throw std::runtime_error("a heap stage's per-draw bindings differ in number from its template");
        for (std::size_t index = 0; index < words.bindings.size(); ++index) {
            if (words.bindings[index].empty()) continue;
            if (result->bindings[index].role != ShaderRecompiler::DescriptorRole::FlattenedSrt && words.bindings[index].size() != result->bindings[index].guestDescriptor.size()) throw std::runtime_error("a heap stage's per-draw descriptor words differ in size from its template");
            result->bindings[index].guestDescriptor = words.bindings[index];
        }
    }
    if (!words.vertexAttributes.empty()) result->vertexAttributes = words.vertexAttributes;
    return result;
}

void NoteHeapDirect() {
    heapStages().fetch_add(1, std::memory_order_relaxed);
}

void NoteHeapDirectVerify(const ShaderRecompiler::RecompileResult& direct, const ShaderRecompiler::RecompileResult& expected) {
    auto& counts = verifyCounts();
    counts.verified.fetch_add(1, std::memory_order_relaxed);
    if (!sameResult(direct, expected)) counts.differ.fetch_add(1, std::memory_order_relaxed);
}

}
