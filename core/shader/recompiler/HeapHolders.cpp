#include "HeapHolders.hpp"
#include "CompiledVariant.hpp"
#include "Recompiler.hpp"
#include "IntermediateRepresentation/IrMetadata/ResourcePlan.hpp"
#include "IntermediateRepresentation/IrBlock.hpp"
#include "IntermediateRepresentation/IrValue.hpp"
#include "Optimization/DescriptorBindingBuilder.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <list>

namespace ShaderRecompiler::HeapHolders {

namespace {

std::array<std::atomic<std::int64_t>, HolderCount> objectCounts{};
std::array<std::atomic<std::int64_t>, HolderCount> byteCounts{};
std::atomic<long long> lastReport{0};

std::uint64_t ArtifactBytes(const CompiledShaderArtifact& artifact) {
    return VectorBytes(artifact.vertexInputPatches) + VectorBytes(artifact.runtimeImageResources) + VectorBytes(artifact.vertexInputs) + VectorBytes(artifact.parameterExports) + VectorBytes(artifact.fragmentParameters) + VectorBytes(artifact.stencilReferences);
}

std::uint64_t BindingBytes(const DescriptorBinding& binding) {
    return VectorBytes(binding.guestDescriptor) + VectorBytes(binding.samplerDepthCompare) + VectorBytes(binding.imageWritten) + VectorBytes(binding.imageDepthCompare) + VectorBytes(binding.imageAtomic) + VectorBytes(binding.imageAtomic64) + VectorBytes(binding.imageShapes) + VectorBytes(binding.bufferAtomic) + VectorBytes(binding.bufferWritten) + VectorBytes(binding.samplerUnnormalized) + VectorBytes(binding.samplerIntegerBorder) + VectorBytes(binding.imageUnnormalized) + VectorBytes(binding.imageUnnormalizedBaseLevel) + VectorBytes(binding.imageSamplers) + VectorBytes(binding.bufferRead);
}

std::uint64_t LayoutBytes(const IrBindingLayout& layout) {
    std::uint64_t bytes = VectorBytes(layout.userDataRegisters) + VectorBytes(layout.descriptors);
    for (const auto& descriptor : layout.descriptors) bytes += VectorBytes(descriptor.resources);
    return bytes;
}

std::uint64_t ValueBytes(const IrValue& value) {
    return VectorBytes(value.Arguments()) + VectorBytes(value.Uses()) + VectorBytes(value.OperandUses()) + VectorBytes(value.PhiBlocks());
}

std::uint64_t BlockBytes(const IrBlock& block) {
    constexpr std::uint64_t ListNode = 2u * sizeof(void*) + sizeof(IrValue*);
    return sizeof(IrBlock) + block.Instructions().size() * ListNode + VectorBytes(block.Predecessors()) + VectorBytes(block.Successors());
}

}

void Add(Holder holder, std::int64_t objects, std::int64_t bytes) {
    if (!Profiled()) return;
    const auto index = static_cast<std::size_t>(holder);
    objectCounts[index].fetch_add(objects, std::memory_order_relaxed);
    byteCounts[index].fetch_add(bytes, std::memory_order_relaxed);
}

Totals Read() {
    Totals totals;
    for (std::size_t index = 0; index < HolderCount; ++index) {
        totals.objects[index] = objectCounts[index].load(std::memory_order_relaxed);
        totals.bytes[index] = byteCounts[index].load(std::memory_order_relaxed);
    }
    return totals;
}

std::string Line(const Totals& totals) {
    const auto field = [&](Holder holder) {
        const auto index = static_cast<std::size_t>(holder);
        char text[64];
        std::snprintf(text, sizeof(text), "%lld (%.1f MiB)", static_cast<long long>(totals.objects[index]), static_cast<double>(totals.bytes[index]) / (1024.0 * 1024.0));
        return std::string(text);
    };
    return "[heap] shader holders: plans " + field(Holder::Plans) + ", modules " + field(Holder::Modules) + ", variant metadata " + field(Holder::VariantMetadata) + ", results " + field(Holder::Results) + ", sources " + field(Holder::Sources) + ", registry " + field(Holder::Registry) + "; draw cache " + field(Holder::DrawCache) + ", dispatch cache " + field(Holder::DispatchCache);
}

bool Profiled() {
    static const bool profiled = [] {
        const char* text = std::getenv("APS5_PROFILE_HEAP");
        return text != nullptr && text[0] == '1';
    }();
    return profiled;
}

void MaybeReport() {
    if (!Profiled()) return;
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto last = lastReport.load(std::memory_order_relaxed);
    if (last != 0 && now - last < 10'000) return;
    if (!lastReport.compare_exchange_strong(last, now, std::memory_order_relaxed)) return;
    if (last == 0) return;
    std::fprintf(stderr, "%s\n", Line(Read()).c_str());
}

std::uint64_t ResultBytes(const RecompileResult& result) {
    std::uint64_t bytes = sizeof(RecompileResult) + ArtifactBytes(result) + VectorBytes(result.specialization) + VectorBytes(result.bindings) + VectorBytes(result.pushConstants) + VectorBytes(result.vertexAttributes) + VectorBytes(result.imageTablePoison) + VectorBytes(result.imageTableRanges) + VectorBytes(result.imageTableReadRanges) + VectorBytes(result.shaderDataUserWords);
    for (const auto& ranges : result.imageTableReadRanges) bytes += VectorBytes(ranges);
    for (const auto& binding : result.bindings) bytes += BindingBytes(binding);
    return bytes;
}

std::uint64_t ShaderInfoBytes(const ShaderInfo& info) {
    std::uint64_t bytes = VectorBytes(info.buffers) + VectorBytes(info.images) + VectorBytes(info.runtimeImageModes) + VectorBytes(info.samplers) + VectorBytes(info.sampledPairs) + VectorBytes(info.inputs) + VectorBytes(info.outputs);
    for (const auto& modes : info.runtimeImageModes) bytes += VectorBytes(modes);
    for (const auto& input : info.inputs) bytes += StringBytes(input.debugName);
    for (const auto& output : info.outputs) bytes += StringBytes(output.debugName);
    return bytes;
}

std::uint64_t PlanBytes(const IrResourcePlan& plan) {
    std::uint64_t bytes = sizeof(IrResourcePlan) + plan.valueStorage.Capacity() * sizeof(IrValue) + VectorBytes(plan.blockStorage) + VectorBytes(plan.memoryInfo) + VectorBytes(plan.descriptorSources) + VectorBytes(plan.controlFlow) + VectorBytes(plan.materializationSources) + VectorBytes(plan.srtReads) + VectorBytes(plan.guardedSrtSlots) + VectorBytes(plan.cleanFlatSlots) + VectorBytes(plan.pureFlatSlots);
    bytes += VectorBytes(plan.compact.values) + VectorBytes(plan.compact.arguments) + VectorBytes(plan.compact.sourceDwords) + VectorBytes(plan.compact.srtReads) + VectorBytes(plan.compact.conditions);
    for (const auto& value : plan.valueStorage) bytes += ValueBytes(value);
    for (const auto& block : plan.blockStorage) {
        if (block != nullptr) bytes += BlockBytes(*block);
    }
    for (const auto& source : plan.descriptorSources) {
        if (source.tableColumn.has_value()) bytes += VectorBytes(source.tableColumn->keyDomains);
    }
    for (const auto& block : plan.controlFlow) bytes += VectorBytes(block.successors) + VectorBytes(block.sources) + VectorBytes(block.images);
    return bytes + ShaderInfoBytes(plan.info);
}

std::uint64_t VariantMetadataBytes(const CompiledVariant& variant) {
    return ShaderInfoBytes(variant.info.info) + LayoutBytes(variant.info.bindings) + LayoutBytes(variant.bindings.layout);
}

std::uint64_t VariantBytes(const CompiledVariant& variant) {
    return sizeof(CompiledVariant) + ArtifactBytes(variant.artifact);
}

std::uint64_t BindingPlanBytes(const DescriptorBindingPlan& plan) {
    std::uint64_t bytes = VectorBytes(plan.bindings) + VectorBytes(plan.imageMetadata) + VectorBytes(plan.specialization) + VectorBytes(plan.runtimeState);
    for (const auto& binding : plan.bindings) bytes += BindingBytes(binding.descriptor) + VectorBytes(binding.resources) + VectorBytes(binding.identityImageSwizzle) + VectorBytes(binding.samplerFilterElements) + VectorBytes(binding.inactive) + VectorBytes(binding.tableSamplerMasks);
    return bytes;
}

}
