#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_PIPELINELIBRARY_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_PIPELINELIBRARY_HPP

#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace AgcDriver::Graphics {

struct PipelineLibraryKeys {
    std::vector<std::byte> vertexInput;
    std::vector<std::byte> preRasterization;
    std::vector<std::byte> fragmentShader;
    std::vector<std::byte> fragmentOutput;
    std::vector<std::byte> renderPass;
    std::vector<std::byte> layout;
    std::vector<std::byte> preRasterizationLayout;
    std::vector<std::byte> fragmentLayout;
    std::uint32_t preRasterizationSets = 0;
    std::uint32_t fragmentSets = 0;
};

std::span<const VkDynamicState> PipelineLibraryDynamicStates();

struct OptimizedPipeline {
    VkDevice device = VK_NULL_HANDLE;
    PFN_vkDestroyPipeline destroy = nullptr;
    std::atomic<VkPipeline> handle{VK_NULL_HANDLE};
    std::atomic<bool> released{false};
    // The relink of a pipeline the journal replay built waits for the first draw that binds it
    // (Schedule), so the relink thread serves the title's own pipelines first and a replayed
    // pipeline the title never draws is never relinked.
    std::atomic<bool> deferred{false};
    std::function<void()> relink;
    void Release() noexcept;
    void Schedule();
};

VkPipeline LinkPipelineFromLibraries(const Context& context, const VkGraphicsPipelineCreateInfo& info, const VkPipelineRenderingCreateInfoKHR& rendering, const VkPipelineLayoutCreateInfo& layout, const PipelineLibraryKeys& keys, std::shared_ptr<OptimizedPipeline>* optimized = nullptr, bool partsOnly = false, std::span<VkShaderModule> modules = {});

void WaitForOptimizedPipelines(VkDevice device);

void ClearPipelineLibraries(VkDevice device);

void StopPipelineLibraryWorker();
unsigned PipelineLibraryWorkers();

struct PipelineLibraryCounters {
    std::array<std::uint64_t, 4> built{};
    std::uint64_t linked = 0;
    std::uint64_t optimized = 0;
};

PipelineLibraryCounters PipelineLibraryCountersOf(VkDevice device);

enum class PipelineKind : std::uint8_t { VertexInput = 0, PreRasterization, FragmentShader, FragmentOutput, Prebuilt, FastLinked, Optimized, Whole, Count };
const char* PipelineKindName(PipelineKind kind);
bool CountsPipelines();
void CountPipeline(PipelineKind kind, bool live) noexcept;
void ForgetPipelines(PipelineKind kind, std::uint64_t count) noexcept;
void CountQueuedPrebuild(bool queued) noexcept;

struct PipelineKindCount {
    std::uint64_t live = 0;
    std::uint64_t created = 0;
};

struct PipelineCounts {
    std::array<PipelineKindCount, static_cast<std::size_t>(PipelineKind::Count)> kinds{};
    std::uint64_t queuedLinks = 0;
    std::uint64_t queuedPrebuilds = 0;
    const PipelineKindCount& Kind(PipelineKind kind) const { return kinds[static_cast<std::size_t>(kind)]; }
};

PipelineCounts PipelineCountTotals();
std::string FormatPipelineCounts(const PipelineCounts& counts, const PipelineCounts& previous);
void ReportPipelineCounts(bool now);

}

#endif
