#include "prx/libSceAgcDriver/Graphics/include/PipelineLibrary.hpp"
#include "prx/libSceAgcDriver/Graphics/include/PipelineJournal.hpp"
#include "prx/libSceAgcDriver/Execution/include/PerformanceTimer.hpp"
#include "prx/libSceAgcDriver/Execution/include/ProfileOutput.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <thread>
#include <map>
#include <mutex>
#include <optional>
#include <string_view>

namespace AgcDriver::Graphics {

namespace {

constexpr std::array<VkDynamicState, 15> dynamicStates{
    VK_DYNAMIC_STATE_VIEWPORT,
    VK_DYNAMIC_STATE_SCISSOR,
    VK_DYNAMIC_STATE_DEPTH_BIAS,
    VK_DYNAMIC_STATE_DEPTH_BOUNDS,
    VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
    VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
    VK_DYNAMIC_STATE_STENCIL_REFERENCE,
    VK_DYNAMIC_STATE_CULL_MODE_EXT,
    VK_DYNAMIC_STATE_FRONT_FACE_EXT,
    VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE_EXT,
    VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE_EXT,
    VK_DYNAMIC_STATE_DEPTH_COMPARE_OP_EXT,
    VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE_EXT,
    VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE_EXT,
    VK_DYNAMIC_STATE_STENCIL_OP_EXT,
};

using Key = std::vector<std::byte>;

struct DeviceLibraries {
    PFN_vkDestroyPipeline destroyPipeline = nullptr;
    PFN_vkDestroyPipelineLayout destroyLayout = nullptr;
    PFN_vkDestroyShaderModule destroyModule = nullptr;
    std::vector<VkShaderModule> modules;
    std::map<Key, VkPipelineLayout> layouts;
    std::array<std::map<Key, VkPipeline>, 4> parts;
    PipelineLibraryCounters counters;
    std::uint64_t prebuilt = 0;
};

struct LibraryStore {
    std::mutex mutex;
    std::map<VkDevice, DeviceLibraries> devices;
    std::condition_variable idle;
    std::condition_variable work;
    std::deque<std::pair<VkDevice, std::function<void()>>> jobs;
    std::map<VkDevice, std::size_t> pending;
    std::map<VkDevice, std::size_t> activeBuilds;
    std::thread worker;
    bool stopping = false;
};

std::atomic<unsigned> runningWorkers{0};

constexpr auto pipelineKinds = static_cast<std::size_t>(PipelineKind::Count);
std::array<std::atomic<std::uint64_t>, pipelineKinds> livePipelines{};
std::array<std::atomic<std::uint64_t>, pipelineKinds> createdPipelines{};
std::atomic<std::uint64_t> queuedPrebuilds{0};
std::atomic<std::int64_t> lastPipelineReport{0};
std::mutex reportMutex;
PipelineCounts reportedCounts;

LibraryStore& Libraries() {
    static auto* store = new LibraryStore();
    return *store;
}

void runJobs(LibraryStore& store) {
    std::unique_lock lock(store.mutex);
    for (;;) {
        store.work.wait(lock, [&] { return !store.jobs.empty() || store.stopping; });
        if (store.stopping) break;
        auto [device, job] = std::move(store.jobs.front());
        store.jobs.pop_front();
        lock.unlock();
        job();
        lock.lock();
        if (--store.pending[device] == 0) store.idle.notify_all();
    }
    runningWorkers.fetch_sub(1, std::memory_order_acq_rel);
}

void enqueue(LibraryStore& store, VkDevice device, std::function<void()> job) {
    if (store.stopping) return;
    if (!store.worker.joinable()) {
        store.worker = std::thread(runJobs, std::ref(store));
        runningWorkers.fetch_add(1, std::memory_order_acq_rel);
    }
    auto& pending = store.pending[device];
    store.jobs.emplace_back(device, std::move(job));
    ++pending;
    store.work.notify_one();
}

Key Combined(const Key& part, const Key& context) {
    Key key;
    const auto append = [&](const Key& value) {
        const auto size = value.size();
        const auto bytes = std::as_bytes(std::span(&size, 1));
        key.insert(key.end(), bytes.begin(), bytes.end());
        key.insert(key.end(), value.begin(), value.end());
    };
    append(part);
    append(context);
    return key;
}

}

std::span<const VkDynamicState> PipelineLibraryDynamicStates() {
    return dynamicStates;
}

VkPipeline LinkPipelineFromLibraries(const Context& context, const VkGraphicsPipelineCreateInfo& info, const VkPipelineRenderingCreateInfoKHR& rendering, const VkPipelineLayoutCreateInfo& layout, const PipelineLibraryKeys& keys, std::shared_ptr<OptimizedPipeline>* optimized, bool partsOnly, std::span<VkShaderModule> modules) {
    Require(info.renderPass == VK_NULL_HANDLE && info.pDynamicState != nullptr, "pipeline library parts need dynamic rendering and the library dynamic states");
    const bool split = (layout.flags & VK_PIPELINE_LAYOUT_CREATE_INDEPENDENT_SETS_BIT_EXT) != 0;
    Require(!split || (layout.setLayoutCount == 2u && layout.pSetLayouts != nullptr && ((keys.preRasterizationSets | keys.fragmentSets) & ~3u) == 0u), "split pipeline library layout requires two descriptor set slots and a valid mask");
    auto& store = Libraries();
    std::unique_lock lock(store.mutex);
    ++store.activeBuilds[context.device];
    struct ActiveBuild {
        LibraryStore& store;
        VkDevice device;
        std::unique_lock<std::mutex>& lock;
        ~ActiveBuild() {
            if (!lock.owns_lock()) lock.lock();
            if (--store.activeBuilds[device] == 0) store.idle.notify_all();
        }
    } activeBuild{store, context.device, lock};
    auto& device = store.devices[context.device];
    if (device.destroyPipeline == nullptr) {
        device.destroyPipeline = context.Function<PFN_vkDestroyPipeline>("vkDestroyPipeline");
        device.destroyLayout = context.Function<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout");
        device.destroyModule = context.Function<PFN_vkDestroyShaderModule>("vkDestroyShaderModule");
    }
    const auto layoutFor = [&](const Key& key, std::optional<std::uint32_t> mask) {
        auto found = device.layouts.find(key);
        if (found != device.layouts.end()) return found->second;
        auto info = layout;
        std::array<VkDescriptorSetLayout, 2> sets{VK_NULL_HANDLE, VK_NULL_HANDLE};
        if (mask.has_value()) {
            for (std::uint32_t set = 0; set < sets.size(); ++set) {
                if ((*mask & (1u << set)) != 0u) sets[set] = layout.pSetLayouts[set];
            }
            info.pSetLayouts = sets.data();
        }
        VkPipelineLayout handle = VK_NULL_HANDLE;
        Check(context.Function<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(context.device, &info, nullptr, &handle), "vkCreatePipelineLayout library");
        return device.layouts.emplace(key, handle).first->second;
    };
    const auto fullLayout = layoutFor(keys.layout, std::nullopt);
    const std::array<VkPipelineLayout, 4> partLayouts{VK_NULL_HANDLE, split ? layoutFor(keys.preRasterizationLayout, keys.preRasterizationSets) : fullLayout, split ? layoutFor(keys.fragmentLayout, keys.fragmentSets) : fullLayout, VK_NULL_HANDLE};
    std::vector<VkPipelineShaderStageCreateInfo> preRasterization;
    const VkPipelineShaderStageCreateInfo* fragment = nullptr;
    bool mesh = false;
    for (std::uint32_t index = 0; index < info.stageCount; ++index) {
        const auto& stage = info.pStages[index];
        if (stage.stage == VK_SHADER_STAGE_FRAGMENT_BIT) fragment = &stage;
        else preRasterization.push_back(stage);
        mesh |= stage.stage == VK_SHADER_STAGE_MESH_BIT_EXT;
    }
    Require(fragment != nullptr && !preRasterization.empty(), "a pipeline built from libraries needs a fragment and a pre-rasterization stage");
    constexpr std::array<VkGraphicsPipelineLibraryFlagsEXT, 4> flags{
        VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT,
        VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT,
        VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT,
        VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT,
    };
    const std::array<const Key*, 4> partKeys{&keys.vertexInput, &keys.preRasterization, &keys.fragmentShader, &keys.fragmentOutput};
    const Key none;
    VkPipelineRenderingCreateInfoKHR shaderRendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    shaderRendering.viewMask = rendering.viewMask;
    shaderRendering.depthAttachmentFormat = rendering.depthAttachmentFormat;
    shaderRendering.stencilAttachmentFormat = rendering.stencilAttachmentFormat;
    const std::array<std::uint32_t, 3> attachments{shaderRendering.viewMask, static_cast<std::uint32_t>(shaderRendering.depthAttachmentFormat), static_cast<std::uint32_t>(shaderRendering.stencilAttachmentFormat)};
    const auto attachmentBytes = std::as_bytes(std::span(attachments));
    const Key attachmentKey(attachmentBytes.begin(), attachmentBytes.end());
    const auto preRasterizationContext = Combined(keys.preRasterizationLayout, attachmentKey);
    const auto fragmentContext = Combined(keys.fragmentLayout, attachmentKey);
    const std::array<const Key*, 4> contextKeys{&none, &preRasterizationContext, &fragmentContext, &keys.renderPass};
    VkPipelineDepthStencilStateCreateInfo dynamicDepth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    static const bool backgroundLinking = std::getenv("APS5_NO_PIPELINE_LTO") == nullptr;
    const bool optimize = backgroundLinking && context.pipelineLinkTimeOptimization;
    std::vector<VkPipeline> libraries;
    for (std::size_t part = 0; part < flags.size(); ++part) {
        if (part == 0 && mesh) continue;
        const auto key = Combined(*partKeys[part], *contextKeys[part]);
        auto& cache = device.parts[part];
        if (const auto found = cache.find(key); found != cache.end()) {
            libraries.push_back(found->second);
            continue;
        }
        PerformanceTimer timing("Vulkan.GraphicsPipelineLibrary");
        VkGraphicsPipelineLibraryCreateInfoEXT library{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT, flags[part] == VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT ? &rendering : &shaderRendering};
        library.flags = flags[part];
        VkGraphicsPipelineCreateInfo create{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &library};
        create.flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR | VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT;
        create.pDynamicState = info.pDynamicState;
        create.layout = partLayouts[part];
        switch (flags[part]) {
        case VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT:
            create.pVertexInputState = info.pVertexInputState;
            create.pInputAssemblyState = info.pInputAssemblyState;
            create.layout = VK_NULL_HANDLE;
            break;
        case VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT:
            create.stageCount = static_cast<std::uint32_t>(preRasterization.size());
            create.pStages = preRasterization.data();
            create.pViewportState = info.pViewportState;
            create.pRasterizationState = info.pRasterizationState;
            create.pTessellationState = info.pTessellationState;
            break;
        case VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT:
            create.stageCount = 1;
            create.pStages = fragment;
            create.pMultisampleState = info.pMultisampleState;
            create.pDepthStencilState = info.pDepthStencilState != nullptr ? info.pDepthStencilState : &dynamicDepth;
            break;
        default:
            create.pMultisampleState = info.pMultisampleState;
            create.pColorBlendState = info.pColorBlendState;
            create.layout = VK_NULL_HANDLE;
            break;
        }
        VkPipeline handle = VK_NULL_HANDLE;
        // Built without the store's mutex: a draw whose parts are resident is not held behind
        // another thread's build, and builds from several threads (the registration prebuild, the
        // journal replay) run at once; a part another thread built meanwhile is kept, this one dropped.
        lock.unlock();
        VkResult created;
        {
            auto pipelineCacheLock = context.LockPipelineCacheForUse();
            created = context.Function<PFN_vkCreateGraphicsPipelines>("vkCreateGraphicsPipelines")(context.device, context.pipelineCache, 1, &create, nullptr, &handle);
        }
        lock.lock();
        Check(created, "vkCreateGraphicsPipelines library");
        if (const auto raced = cache.find(key); raced != cache.end()) {
            device.destroyPipeline(context.device, handle, nullptr);
            libraries.push_back(raced->second);
            continue;
        }
        cache.emplace(key, handle);
        if (CountsPipelines()) {
            CountPipeline(static_cast<PipelineKind>(part), true);
            if (partsOnly) {
                CountPipeline(PipelineKind::Prebuilt, true);
                ++device.prebuilt;
            }
        }
        if (optimize) {
            for (std::uint32_t stage = 0; stage < create.stageCount; ++stage) {
                for (auto& module : modules) {
                    if (module == VK_NULL_HANDLE || module != create.pStages[stage].module) continue;
                    device.modules.push_back(module);
                    module = VK_NULL_HANDLE;
                }
            }
        }
        ++device.counters.built[part];
        libraries.push_back(handle);
    }
    if (partsOnly) return VK_NULL_HANDLE;
    PerformanceTimer timing("Vulkan.GraphicsPipelineLink");
    VkPipelineLibraryCreateInfoKHR linked{VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR};
    linked.libraryCount = static_cast<std::uint32_t>(libraries.size());
    linked.pLibraries = libraries.data();
    VkGraphicsPipelineCreateInfo create{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &linked};
    create.layout = info.layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    lock.unlock();
    {
        auto pipelineCacheLock = context.LockPipelineCacheForUse();
        Check(context.Function<PFN_vkCreateGraphicsPipelines>("vkCreateGraphicsPipelines")(context.device, context.pipelineCache, 1, &create, nullptr, &pipeline), "vkCreateGraphicsPipelines link");
    }
    lock.lock();
    ++device.counters.linked;
    if (CountsPipelines()) CountPipeline(PipelineKind::FastLinked, true);
    if (optimize && optimized != nullptr) {
        auto slot = std::make_shared<OptimizedPipeline>();
        slot->device = context.device;
        slot->destroy = device.destroyPipeline;
        *optimized = slot;
        const auto createPipelines = context.Function<PFN_vkCreateGraphicsPipelines>("vkCreateGraphicsPipelines");
        const auto cache = context.pipelineCache;
        const auto cacheSynchronization = context.pipelineCacheSynchronization;
        const auto libraryLayout = fullLayout;
        std::function<void()> relink = [slot, createPipelines, cache, cacheSynchronization, libraryLayout, libraries, &store, devicePointer = &device] {
            if (slot->released.load()) return;
            VkPipelineLibraryCreateInfoKHR linked{VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR};
            linked.libraryCount = static_cast<std::uint32_t>(libraries.size());
            linked.pLibraries = libraries.data();
            VkGraphicsPipelineCreateInfo create{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &linked};
            create.flags = VK_PIPELINE_CREATE_LINK_TIME_OPTIMIZATION_BIT_EXT;
            create.layout = libraryLayout;
            VkPipeline result = VK_NULL_HANDLE;
            const auto pipelineCreationResult = [&] {
                auto pipelineCacheLock = cacheSynchronization != nullptr ? cacheSynchronization->LockForPipelineUse() : PipelineCacheSynchronization::PipelineUseLock{};
                return createPipelines(slot->device, cache, 1, &create, nullptr, &result);
            }();
            if (pipelineCreationResult != VK_SUCCESS) return;
            if (CountsPipelines()) CountPipeline(PipelineKind::Optimized, true);
            slot->handle.store(result);
            if (slot->released.load()) {
                if (const auto handle = slot->handle.exchange(VK_NULL_HANDLE)) {
                    slot->destroy(slot->device, handle, nullptr);
                    if (CountsPipelines()) CountPipeline(PipelineKind::Optimized, false);
                }
            }
            std::lock_guard lock(store.mutex);
            ++devicePointer->counters.optimized;
        };
        if (PipelineJournalReplaying()) {
            slot->relink = std::move(relink);
            slot->deferred.store(true, std::memory_order_release);
        } else {
            enqueue(store, context.device, std::move(relink));
        }
    }
    return pipeline;
}

void OptimizedPipeline::Release() noexcept {
    released.store(true);
    if (const auto pipeline = handle.exchange(VK_NULL_HANDLE)) {
        destroy(device, pipeline, nullptr);
        if (CountsPipelines()) CountPipeline(PipelineKind::Optimized, false);
    }
}

void OptimizedPipeline::Schedule() {
    if (!deferred.load(std::memory_order_acquire)) return;
    auto& store = Libraries();
    std::lock_guard lock(store.mutex);
    if (!deferred.exchange(false, std::memory_order_acq_rel)) return;
    if (relink && !released.load()) enqueue(store, device, std::move(relink));
    relink = nullptr;
}

void WaitForOptimizedPipelines(VkDevice device) {
    auto& store = Libraries();
    std::unique_lock lock(store.mutex);
    store.idle.wait(lock, [&] { return store.pending[device] == 0; });
}

void ClearPipelineLibraries(VkDevice device) {
    auto& store = Libraries();
    std::unique_lock lock(store.mutex);
    store.idle.wait(lock, [&] { return store.pending[device] == 0 && store.activeBuilds[device] == 0; });
    const auto found = store.devices.find(device);
    if (found == store.devices.end()) return;
    auto& libraries = found->second;
    for (std::size_t part = 0; part < libraries.parts.size(); ++part) {
        for (const auto& [key, handle] : libraries.parts[part]) libraries.destroyPipeline(device, handle, nullptr);
        if (CountsPipelines()) ForgetPipelines(static_cast<PipelineKind>(part), libraries.parts[part].size());
    }
    if (CountsPipelines()) ForgetPipelines(PipelineKind::Prebuilt, libraries.prebuilt);
    for (const auto& [key, handle] : libraries.layouts) libraries.destroyLayout(device, handle, nullptr);
    for (const auto module : libraries.modules) libraries.destroyModule(device, module, nullptr);
    store.devices.erase(found);
}

void StopPipelineLibraryWorker() {
    auto& store = Libraries();
    std::thread worker;
    {
        std::lock_guard lock(store.mutex);
        store.stopping = true;
        for (const auto& [device, job] : store.jobs) --store.pending[device];
        store.jobs.clear();
        worker = std::move(store.worker);
    }
    store.idle.notify_all();
    store.work.notify_all();
    if (!worker.joinable()) return;
    if (worker.get_id() == std::this_thread::get_id()) worker.detach();
    else worker.join();
}

unsigned PipelineLibraryWorkers() {
    return runningWorkers.load(std::memory_order_acquire);
}

PipelineLibraryCounters PipelineLibraryCountersOf(VkDevice device) {
    auto& store = Libraries();
    std::lock_guard lock(store.mutex);
    const auto found = store.devices.find(device);
    return found == store.devices.end() ? PipelineLibraryCounters{} : found->second.counters;
}

const char* PipelineKindName(PipelineKind kind) {
    switch (kind) {
    case PipelineKind::VertexInput: return "vertex input";
    case PipelineKind::PreRasterization: return "pre-rasterization";
    case PipelineKind::FragmentShader: return "fragment shader";
    case PipelineKind::FragmentOutput: return "fragment output";
    case PipelineKind::Prebuilt: return "prebuilt libraries";
    case PipelineKind::FastLinked: return "fast-linked";
    case PipelineKind::Optimized: return "optimized";
    case PipelineKind::Whole: return "whole";
    case PipelineKind::Count: break;
    }
    return "?";
}

bool CountsPipelines() {
    static const bool counted = std::getenv("APS5_PROFILE_DRAW") != nullptr;
    return counted;
}

void CountPipeline(PipelineKind kind, bool live) noexcept {
    const auto index = static_cast<std::size_t>(kind);
    if (!live) {
        livePipelines[index].fetch_sub(1, std::memory_order_relaxed);
        return;
    }
    livePipelines[index].fetch_add(1, std::memory_order_relaxed);
    createdPipelines[index].fetch_add(1, std::memory_order_relaxed);
}

void ForgetPipelines(PipelineKind kind, std::uint64_t count) noexcept {
    livePipelines[static_cast<std::size_t>(kind)].fetch_sub(count, std::memory_order_relaxed);
}

void CountQueuedPrebuild(bool queued) noexcept {
    if (queued) queuedPrebuilds.fetch_add(1, std::memory_order_relaxed);
    else queuedPrebuilds.fetch_sub(1, std::memory_order_relaxed);
}

PipelineCounts PipelineCountTotals() {
    PipelineCounts counts;
    for (std::size_t kind = 0; kind < pipelineKinds; ++kind) {
        counts.kinds[kind].live = livePipelines[kind].load(std::memory_order_relaxed);
        counts.kinds[kind].created = createdPipelines[kind].load(std::memory_order_relaxed);
    }
    counts.queuedPrebuilds = queuedPrebuilds.load(std::memory_order_relaxed);
    auto& store = Libraries();
    std::lock_guard lock(store.mutex);
    counts.queuedLinks = store.jobs.size();
    return counts;
}

std::string FormatPipelineCounts(const PipelineCounts& counts, const PipelineCounts& previous) {
    char line[768];
    auto used = static_cast<std::size_t>(std::snprintf(line, sizeof(line), "live (created since the last line):"));
    for (std::size_t kind = 0; kind < pipelineKinds && used < sizeof(line); ++kind) {
        const auto& count = counts.kinds[kind];
        used += static_cast<std::size_t>(std::snprintf(line + used, sizeof(line) - used, " %s %llu (+%llu),", PipelineKindName(static_cast<PipelineKind>(kind)), static_cast<unsigned long long>(count.live), static_cast<unsigned long long>(count.created - previous.kinds[kind].created)));
    }
    if (used < sizeof(line)) std::snprintf(line + used, sizeof(line) - used, " queued: %llu background links, %llu prebuilds", static_cast<unsigned long long>(counts.queuedLinks), static_cast<unsigned long long>(counts.queuedPrebuilds));
    return line;
}

void ReportPipelineCounts(bool now) {
    const auto time = std::chrono::steady_clock::now().time_since_epoch().count();
    auto last = lastPipelineReport.load(std::memory_order_relaxed);
    if (!now && last != 0 && time - last < std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::seconds(10)).count()) return;
    if (!lastPipelineReport.compare_exchange_strong(last, time, std::memory_order_relaxed) && !now) return;
    const auto counts = PipelineCountTotals();
    std::lock_guard lock(reportMutex);
    AgcDriver::ProfilePrint_nid_no_patch("[pipelines] %s\n", FormatPipelineCounts(counts, reportedCounts).c_str());
    reportedCounts = counts;
}

}
