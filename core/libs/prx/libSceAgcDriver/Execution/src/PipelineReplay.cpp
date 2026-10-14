#include "prx/libSceAgcDriver/Execution/include/PipelineReplay.hpp"
#include "Optimization/ResourceProgram.hpp"
#include "CompiledVariant.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <exception>
#include <stdexcept>

namespace AgcDriver {

namespace {

struct ReplayTotals {
    std::atomic<std::uint64_t> loaded{0};
    std::atomic<std::uint64_t> replayed{0};
    std::atomic<std::uint64_t> recorded{0};
    std::atomic<std::uint64_t> notRecordable{0};
    std::atomic<std::uint64_t> failed{0};
    std::atomic<std::uint64_t> inFrames{0};
};

ReplayTotals& ReplayCounters() {
    static ReplayTotals totals;
    return totals;
}

std::uint32_t Written(const ShaderRecompiler::DescriptorBinding& binding) {
    const auto any = [&](const std::vector<bool>& written) { return written.size() < binding.count || std::ranges::any_of(written, [](bool element) { return element; }); };
    if (binding.role == ShaderRecompiler::DescriptorRole::GuestBuffers) return any(binding.bufferWritten) ? 1u : 0u;
    if (binding.role == ShaderRecompiler::DescriptorRole::GuestImages) return any(binding.imageWritten) ? 1u : 0u;
    return 0u;
}

bool Recordable(const Graphics::State& state, std::span<const Graphics::CompiledShader> shaders) {
    if (state.rectList) return false;
    for (const auto& shader : shaders) {
        if (shader.program == nullptr || shader.stage == ShaderRecompiler::ShaderStage::Geometry || shader.program->identity == std::array<std::uint64_t, 2>{}) return false;
    }
    return true;
}

}

PipelineReplay::PipelineReplay(PipelineManifest& manifest, std::function<Graphics::Context()> context, std::function<Target()> target) : manifest(manifest), context(std::move(context)), target(std::move(target)) {
    for (auto& record : manifest.GraphicsRecords()) {
        auto& entry = pending.emplace_back();
        entry.handles.resize(record.stages.size());
        entry.missing = record.stages.size();
        for (std::size_t stage = 0; stage < record.stages.size(); ++stage) waiting[record.stages[stage].program].emplace_back(pending.size() - 1u, stage);
        entry.record = std::move(record);
    }
    ReplayCounters().loaded.store(pending.size(), std::memory_order_relaxed);
}

void PipelineReplay::Created(const Graphics::Context& context, const Graphics::State& state, const Graphics::VertexInputLayout& vertexInput, const Graphics::PipelineSetLayouts& resources, std::span<const Graphics::CompiledShader> shaders, VkImageLayout attachmentLayout) {
    ReplayCounters().inFrames.fetch_add(1, std::memory_order_relaxed);
    if (!Recordable(state, shaders)) {
        ReplayCounters().notRecordable.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const auto described = Graphics::DescribeSetLayouts(context, shaders);
    if (described.layouts.split != resources.split || !std::ranges::equal(described.mainKey, resources.mainKey) || !std::ranges::equal(described.fragmentKey, resources.fragmentKey)) {
        ReplayCounters().notRecordable.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    GraphicsPipelineRecord record;
    for (const auto& shader : shaders) {
        const auto& program = *shader.program;
        auto& stage = record.stages.emplace_back();
        stage.stage = static_cast<std::uint32_t>(shader.stage);
        stage.pushOffset = shader.pushConstantOffset;
        stage.program = program.identity;
        stage.constants = program.moduleConstants;
        stage.pushBytes = static_cast<std::uint32_t>(program.pushConstants.size());
        for (const auto& binding : program.bindings) stage.bindings.push_back({static_cast<std::uint32_t>(binding.kind), static_cast<std::uint32_t>(binding.role), binding.descriptorSet, binding.binding, binding.count, Written(binding)});
    }
    record.description = Graphics::EncodePipelineDescription(state, vertexInput, attachmentLayout);
    const auto decoded = Graphics::DecodePipelineDescription(record.description);
    const Graphics::PipelineDescription original{state, vertexInput, attachmentLayout};
    if (!decoded.has_value() || !Graphics::SamePipelineKey(context, resources, shaders, original, *decoded)) throw std::logic_error("AGC driver: a graphics pipeline description does not rebuild its pipeline key");
    if (manifest.Record(std::move(record))) ReplayCounters().recorded.fetch_add(1, std::memory_order_relaxed);
}

std::vector<std::function<void()>> PipelineReplay::Prepared(const std::shared_ptr<const ShaderRecompiler::SourceHandle>& handle) {
    std::vector<std::function<void()>> ready;
    if (handle == nullptr || handle->artifact == nullptr) return ready;
    const auto& identity = ShaderRecompiler::GetPreparedArtifact(*handle).identity;
    std::lock_guard lock(mutex);
    const auto found = waiting.find(identity);
    if (found == waiting.end()) return ready;
    for (const auto& [index, stage] : found->second) {
        auto& entry = pending[index];
        if (entry.handles[stage] != nullptr) continue;
        entry.handles[stage] = handle;
        if (--entry.missing != 0u) continue;
        ready.push_back([this, record = entry.record, handles = std::move(entry.handles)] { create(record, handles); });
        entry.handles.clear();
    }
    waiting.erase(found);
    return ready;
}

void PipelineReplay::create(const GraphicsPipelineRecord& record, const std::vector<std::shared_ptr<const ShaderRecompiler::SourceHandle>>& handles) {
    try {
        const auto device = context();
        const auto spirvTarget = target();
        const auto description = Graphics::DecodePipelineDescription(record.description);
        if (!description.has_value()) throw std::runtime_error("the recorded pipeline description does not decode");
        std::vector<std::shared_ptr<ShaderRecompiler::RecompileResult>> results;
        std::vector<Graphics::CompiledShader> shaders;
        for (std::size_t index = 0; index < record.stages.size(); ++index) {
            const auto& stage = record.stages[index];
            auto result = ShaderRecompiler::SpecializedStageResult(*handles[index], stage.constants, spirvTarget);
            result->pushConstants.assign(stage.pushBytes, std::byte{0});
            for (const auto& shape : stage.bindings) {
                auto& binding = result->bindings.emplace_back();
                binding.kind = static_cast<ShaderRecompiler::DescriptorKind>(shape.kind);
                binding.role = static_cast<ShaderRecompiler::DescriptorRole>(shape.role);
                binding.descriptorSet = shape.descriptorSet;
                binding.binding = shape.binding;
                binding.count = shape.count;
                binding.bufferWritten.assign(shape.count, shape.written != 0u);
                binding.imageWritten.assign(shape.count, shape.written != 0u);
            }
            shaders.push_back({static_cast<ShaderRecompiler::ShaderStage>(stage.stage), result.get(), stage.pushOffset});
            results.push_back(std::move(result));
        }
        const auto described = Graphics::DescribeSetLayouts(device, shaders);
        static_cast<void>(Graphics::CachedPipeline(device, description->state, description->vertexInput, described.View(), shaders, description->attachmentLayout));
        ReplayCounters().replayed.fetch_add(1, std::memory_order_relaxed);
    } catch (const std::exception& error) {
        ReplayCounters().failed.fetch_add(1, std::memory_order_relaxed);
        std::fprintf(stderr, "[pipeline-manifest] a graphics pipeline recorded by an earlier run could not be created ahead: %s\n", error.what());
    }
}

PipelineReplay::Counters PipelineReplay::Totals() {
    auto& totals = ReplayCounters();
    return {totals.loaded.load(std::memory_order_relaxed), totals.replayed.load(std::memory_order_relaxed), totals.recorded.load(std::memory_order_relaxed), totals.notRecordable.load(std::memory_order_relaxed), totals.failed.load(std::memory_order_relaxed), totals.inFrames.load(std::memory_order_relaxed)};
}

}
