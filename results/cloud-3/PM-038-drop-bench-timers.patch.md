# PM-038: drop the bench and step-timer commits from perf/shader-image-table-reuse-v3

Apply on the branch head 830bfa53f with `git apply PM-038-drop-bench-timers.patch.md` and commit it as `chore(agc): drop the Village-like bench and the step timers`. Result: tree 5a7364a66, the same as the clean history 805155311.

```diff
diff --git a/core/libs/prx/libSceAgcDriver/Execution/include/StepTimers.hpp b/core/libs/prx/libSceAgcDriver/Execution/include/StepTimers.hpp
deleted file mode 100644
index 94a3e0e89..000000000
--- a/core/libs/prx/libSceAgcDriver/Execution/include/StepTimers.hpp
+++ /dev/null
@@ -1,86 +0,0 @@
-#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_STEPTIMERS_HPP
-#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_STEPTIMERS_HPP
-
-#include "prx/libSceAgcDriver/Execution/include/ProfileOutput.hpp"
-#include <atomic>
-#include <chrono>
-#include <cstdint>
-#include <cstdio>
-#include <cstdlib>
-#include <string>
-
-namespace AgcDriver {
-
-struct StepSite {
-    const char* name;
-    std::atomic<std::uint64_t> nanoseconds{0};
-    std::atomic<std::uint64_t> calls{0};
-    StepSite* next = nullptr;
-};
-
-inline std::atomic<StepSite*>& StepSites() {
-    static std::atomic<StepSite*> head{nullptr};
-    return head;
-}
-
-inline bool StepsTimed() {
-    static const bool timed = std::getenv("APS5_PROFILE_DRAW") != nullptr;
-    return timed;
-}
-
-inline StepSite& RegisterStep(StepSite& site) {
-    site.next = StepSites().load(std::memory_order_relaxed);
-    while (!StepSites().compare_exchange_weak(site.next, &site, std::memory_order_release, std::memory_order_relaxed)) {
-    }
-    return site;
-}
-
-inline void ReportSteps(std::chrono::steady_clock::time_point now) {
-    static std::atomic<std::int64_t> lastReport{0};
-    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
-    auto last = lastReport.load(std::memory_order_relaxed);
-    if (last == 0) {
-        lastReport.compare_exchange_strong(last, seconds);
-        return;
-    }
-    if (seconds - last < 10 || !lastReport.compare_exchange_strong(last, seconds)) return;
-    std::string report;
-    for (auto* site = StepSites().load(std::memory_order_acquire); site != nullptr; site = site->next) {
-        const auto count = site->calls.exchange(0, std::memory_order_relaxed);
-        const auto total = site->nanoseconds.exchange(0, std::memory_order_relaxed);
-        if (count == 0) continue;
-        char text[160];
-        std::snprintf(text, sizeof(text), " %s %.1f us x%llu;", site->name, static_cast<double>(total) / 1000.0 / static_cast<double>(count), static_cast<unsigned long long>(count));
-        report += text;
-    }
-    ProfilePrint_nid_no_patch("[steps] per call (10 s):%s\n", report.c_str());
-}
-
-class StepTimer {
-public:
-    explicit StepTimer(StepSite& site) : site(site), enabled(StepsTimed()), start(enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}) {}
-    ~StepTimer() {
-        if (!enabled) return;
-        const auto now = std::chrono::steady_clock::now();
-        site.nanoseconds.fetch_add(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count()), std::memory_order_relaxed);
-        site.calls.fetch_add(1, std::memory_order_relaxed);
-        ReportSteps(now);
-    }
-    StepTimer(const StepTimer&) = delete;
-    StepTimer& operator=(const StepTimer&) = delete;
-
-private:
-    StepSite& site;
-    bool enabled;
-    std::chrono::steady_clock::time_point start;
-};
-
-#define APS5_STEP_JOIN(a, b) a##b
-#define APS5_STEP_NAME(a, b) APS5_STEP_JOIN(a, b)
-#define APS5_STEP(text)                                                                                            \
-    static ::AgcDriver::StepSite& APS5_STEP_NAME(stepSite, __LINE__) = ::AgcDriver::RegisterStep(*new ::AgcDriver::StepSite{text}); \
-    const ::AgcDriver::StepTimer APS5_STEP_NAME(stepTimer, __LINE__)(APS5_STEP_NAME(stepSite, __LINE__))
-
-}
-
-#endif
diff --git a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/Dispatch.cpp b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/Dispatch.cpp
index 9c53f2367..b22506d74 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/Dispatch.cpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/Dispatch.cpp
@@ -2,7 +2,6 @@
 #include "prx/libSceAgcDriver/Execution/include/PerformanceTimer.hpp"
 #include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
 #include "prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawPipeline.hpp"
-#include "prx/libSceAgcDriver/Execution/include/StepTimers.hpp"
 #include "prx/libSceAgcDriver/Execution/include/Driver/Diagnostics.hpp"
 #include "prx/libSceAgcDriver/Execution/include/Driver/Memory/BufferCopy.hpp"
 #include "prx/libSceAgcDriver/Execution/include/Driver/Shaders/ShaderRegistry.hpp"
@@ -259,7 +258,6 @@ void Driver::dispatch(QueueState& queue, std::span<const std::uint32_t> packet,
             forgetAtCapture = GuestMemory::ForgetSerial();
             capture = [&] {
                 const SampledReadScope sampling(evidenceReads);
-                APS5_STEP("dispatch: capture (miss)");
                 return shaderMemory->Capture(invocation);
             }();
             captured = shaderMemory->Regions();
diff --git a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/DispatchInsert.cpp b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/DispatchInsert.cpp
index ce1485501..e732e518e 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/DispatchInsert.cpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/DispatchInsert.cpp
@@ -1,5 +1,4 @@
 #include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
-#include "prx/libSceAgcDriver/Execution/include/StepTimers.hpp"
 #include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
 #include "Optimization/ResourceProgram.hpp"
 #include "ImageTableAbi.hpp"
@@ -16,7 +15,6 @@ bool CacheableResult(const ShaderRecompiler::RecompileResult& compiled) {
 }
 
 void Driver::insertDispatch(std::uint64_t address, std::uint64_t key, bool noDispatchCache, bool profile, const std::shared_ptr<const ShaderSnapshot>& registeredShader, std::uint64_t forgetAtCapture, std::span<const ShaderRecompiler::MemoryRegion> memory, const std::shared_ptr<ShaderMemory>& shaderMemory, const std::vector<ShaderRecompiler::MemoryRegion>& captured, const std::shared_ptr<const ShaderRecompiler::ResourceCapture>& capture, const std::shared_ptr<const ShaderRecompiler::RecompileResult>& compiledResult, const std::shared_ptr<DispatchEntry>& missedEntry, bool missedDiffering, std::shared_ptr<DispatchVariant>& attachVariant, DispatchPhaseTiming& phaseTiming) {
-    APS5_STEP("dispatch: insert (miss)");
     if (!noDispatchCache) {
         auto fresh = std::make_shared<DispatchVariant>();
         fresh->compiled = compiledResult;
diff --git a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/DispatchLookup.cpp b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/DispatchLookup.cpp
index 7fefde663..b0b37b790 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/DispatchLookup.cpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/DispatchLookup.cpp
@@ -1,5 +1,4 @@
 #include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
-#include "prx/libSceAgcDriver/Execution/include/StepTimers.hpp"
 #include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
 #include <cstdlib>
 
@@ -33,7 +32,6 @@ void Driver::lookupDispatch(std::uint64_t address, const Submission& submission,
             std::vector<std::pair<std::uint32_t, std::uint32_t>> liveData;
             const auto waitedBeforeValidate = profile ? Graphics::Recorder::ThreadWaitedMs() : 0.0;
             if (!stampValidate()) {
-                APS5_STEP("dispatch: compare variants");
                 const GuestMemory::ReadSiteScope site(GuestMemory::ReadSite::DispatchCache);
 
                 std::optional<SampledReadScope> sampling;
diff --git a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/VariantValidation.cpp b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/VariantValidation.cpp
index d5fd98965..e4136cbeb 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/VariantValidation.cpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/VariantValidation.cpp
@@ -1,6 +1,5 @@
 #include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
 #include "prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawPipeline.hpp"
-#include "prx/libSceAgcDriver/Execution/include/StepTimers.hpp"
 #include "prx/libSceAgcDriver/Execution/include/Driver/Synchronization/DeferredLabels.hpp"
 #include "ThreadOwned.hpp"
 #include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
@@ -11,7 +10,6 @@
 namespace AgcDriver::DriverDetail {
 
 void Driver::appendEntryRegions(const DispatchVariant& variant, std::vector<ShaderRecompiler::MemoryRegion>& regions, const std::vector<std::uint32_t>* words) {
-    APS5_STEP("validate: append entry regions");
     const auto& source = words != nullptr ? *words : variant.words;
     regions.reserve(regions.size() + variant.runs.size());
     std::size_t offset = 0;
@@ -92,7 +90,6 @@ bool Driver::vouchedValidation() {
 }
 
 EntryOutcome Driver::validateVariant(std::uint64_t program, std::uint32_t queue, const DispatchVariant& variant, std::span<const ShaderRecompiler::MemoryRegion> regions, std::uint64_t& imagesFlushed, std::uint64_t& runsSynced, std::optional<SampledReadScope>& sampling, std::vector<std::pair<std::uint32_t, std::uint32_t>>* live) {
-    APS5_STEP("validate: whole variant");
     if (live != nullptr) live->clear();
     if (DrawPipeline::Active()) {
         auto& pipeline = DrawPipeline::Queue0();
diff --git a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Memory/CaptureValidation.cpp b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Memory/CaptureValidation.cpp
index 9733d726f..46d3c6604 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Memory/CaptureValidation.cpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Memory/CaptureValidation.cpp
@@ -1,6 +1,5 @@
 #include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
 #include "ThreadOwned.hpp"
-#include "prx/libSceAgcDriver/Execution/include/StepTimers.hpp"
 #include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
 #include <cstdlib>
 #include <cstring>
@@ -60,7 +59,6 @@ bool Driver::captureStable(std::span<const ShaderRecompiler::MemoryRegion> captu
 
 bool Driver::validateCaptured(std::uint64_t program, std::uint32_t queue, std::span<const ShaderRecompiler::MemoryRegion> captured, const ShaderRecompiler::RecompileResult& compiled, bool inPlace, const PendingView& view, bool* unmapped, std::optional<SampledReadScope>* sampling, const DataMask* data, std::span<const VouchedRegions> vouched) {
     using Policy = ShaderMemory::PendingWrite;
-    APS5_STEP("validate: captured regions");
 
     thread_local std::vector<std::size_t>* pendingSlot = nullptr;
     auto& pending = ShaderRecompiler::ThreadOwned(pendingSlot);
diff --git a/core/libs/prx/libSceAgcDriver/Execution/src/ShaderMemory.cpp b/core/libs/prx/libSceAgcDriver/Execution/src/ShaderMemory.cpp
index b3adf2e3e..7dc06125c 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/src/ShaderMemory.cpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/src/ShaderMemory.cpp
@@ -6,7 +6,6 @@
 #include "Optimization/RequestMemoryView.hpp"
 #include "Optimization/ResourceMaterializer.hpp"
 #include "Optimization/ResourceProgram.hpp"
-#include "prx/libSceAgcDriver/Execution/include/StepTimers.hpp"
 #include "ThreadOwned.hpp"
 #include <algorithm>
 #include <atomic>
@@ -394,7 +393,6 @@ bool ShaderMemory::pending(void* context, std::uint64_t address, std::uint64_t b
 }
 
 ShaderRecompiler::SrtWriteStamp ShaderMemory::stampWrites(void* context, std::uint64_t address, std::uint64_t bytes) {
-    APS5_STEP("capture: stamp");
     const auto& self = *static_cast<const ShaderMemory*>(context);
     if (bytes == 0 || address > std::numeric_limits<std::uint64_t>::max() - bytes || self.overlapsInitial(address, address + bytes)) return {};
     const auto fetched = self.pageAtOrAfter(address & ~static_cast<std::uint64_t>(PageBytes - 1));
@@ -408,7 +406,6 @@ ShaderRecompiler::SrtWriteStamp ShaderMemory::stampWrites(void* context, std::ui
 }
 
 bool ShaderMemory::Vouched(PendingWriteQuery pending, std::uint64_t address, std::uint64_t bytes, const ShaderRecompiler::SrtWriteStamp& stamp) {
-    APS5_STEP("vouch: whole");
     CaptureTotals().vouchCalls.fetch_add(1, std::memory_order_relaxed);
     if (stamp.generation == 0 || bytes == 0 || address > std::numeric_limits<std::uint64_t>::max() - bytes || stamp.layout != GuestAllocations::GuestAllocationsGeneration_nid_postfix()) return false;
     if (pending != nullptr && pending(address, static_cast<std::size_t>(bytes), {}) != PendingWrite::None) return false;
@@ -418,7 +415,6 @@ bool ShaderMemory::Vouched(PendingWriteQuery pending, std::uint64_t address, std
 }
 
 bool ShaderMemory::unchangedSince(void* context, std::uint64_t address, std::uint64_t bytes, const ShaderRecompiler::SrtWriteStamp& stamp) {
-    APS5_STEP("capture: unchanged since (per call)");
     return Vouched(static_cast<const ShaderMemory*>(context)->pendingSpan, address, bytes, stamp);
 }
 
@@ -478,7 +474,6 @@ void ShaderMemory::spillTouching(std::uint64_t begin, std::uint64_t end) {
 }
 
 bool ShaderMemory::supplyWords(void* context, const std::shared_ptr<const ShaderRecompiler::SrtGuestWords>& words) {
-    APS5_STEP("capture: supply");
     auto& self = *static_cast<ShaderMemory*>(context);
     if (words == nullptr || words->runs.empty()) return false;
     std::uint64_t total = 0;
@@ -743,7 +738,6 @@ bool SameCapturedWords(std::span<const std::uint32_t> left, std::span<const Shar
 }
 
 std::size_t VouchSharedRegions(std::span<const SharedWords> shared, std::size_t regions, ShaderMemory::PendingWriteQuery pending, std::vector<VouchedRegions>& vouched) {
-    APS5_STEP("validate: vouch shared regions");
     vouched.clear();
     if (shared.empty() || regions == 0) return 0;
     thread_local std::vector<std::pair<const ShaderRecompiler::SrtGuestWords*, bool>>* blocksSlot = nullptr;
diff --git a/core/libs/prx/libSceAgcDriver/Graphics/src/Recorder.cpp b/core/libs/prx/libSceAgcDriver/Graphics/src/Recorder.cpp
index 4465b1cd0..b494d51ff 100644
--- a/core/libs/prx/libSceAgcDriver/Graphics/src/Recorder.cpp
+++ b/core/libs/prx/libSceAgcDriver/Graphics/src/Recorder.cpp
@@ -2,7 +2,6 @@
 #include "prx/libSceAgcDriver/Execution/include/CaptureTrace.hpp"
 #include "prx/libc/include/HostThreadLocal.hpp"
 #include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
-#include "prx/libSceAgcDriver/Execution/include/StepTimers.hpp"
 #include "prx/libSceAgcDriver/Graphics/include/Resources.hpp"
 #include "prx/libSceAgcDriver/Graphics/include/ParallelCompare.hpp"
 #include "prx/libSceAgcDriver/Graphics/include/Texture.hpp"
@@ -1002,7 +1001,6 @@ private:
 // work first; then storage image results pending for the range are stored. The recorder is only
 // dereferenced under the GpuMutex (it is destroyed with its device under that lock).
 void FlushForAccess(std::uint64_t address, std::size_t bytes) {
-    APS5_STEP("flush hook");
     hookCalls.fetch_add(1, std::memory_order_relaxed);
     // A label this worker queued but has not recorded writes the range: in queue order the label
     // precedes this access, so the group is recorded first (it then notes the range as a pending
diff --git a/core/libs/prx/libSceAgcDriver/Graphics/src/ShaderResources.cpp b/core/libs/prx/libSceAgcDriver/Graphics/src/ShaderResources.cpp
index 129fcb275..ae5dc666e 100644
--- a/core/libs/prx/libSceAgcDriver/Graphics/src/ShaderResources.cpp
+++ b/core/libs/prx/libSceAgcDriver/Graphics/src/ShaderResources.cpp
@@ -2,7 +2,6 @@
 #include "prx/libSceAgcDriver/Execution/include/PerformanceTimer.hpp"
 #include "prx/libSceAgcDriver/Execution/include/CaptureTrace.hpp"
 #include "ThreadOwned.hpp"
-#include "prx/libSceAgcDriver/Execution/include/StepTimers.hpp"
 #include "prx/libc/include/HostThreadLocal.hpp"
 #include "prx/libSceAgcDriver/Graphics/include/MultisampleTarget.hpp"
 #include "prx/libSceAgcDriver/Graphics/include/DepthSurface.hpp"
@@ -1452,7 +1451,6 @@ bool SameTable(const ReusedTable& table, VkDevice device, const ShaderRecompiler
 }
 
 ReusedTable* FindReusedTable(VkDevice device, const ShaderRecompiler::DescriptorBinding& binding, const TableState& state) {
-    APS5_STEP("table: find reused");
     if (state.epoch == 0) return nullptr;
     auto& tables = ReusedTables();
     std::erase_if(tables, [&](const ReusedTable& table) { return table.state != state; });
@@ -1463,7 +1461,6 @@ ReusedTable* FindReusedTable(VkDevice device, const ShaderRecompiler::Descriptor
 }
 
 bool TableCurrent(ReusedTable& table) {
-    APS5_STEP("table: current");
     const auto serial = StorageTexture::PendingSerial();
     if (serial != table.pendingSerial) {
         if (StorageTexture::AnyPendingOverlaps(table.ranges) || StorageTexture::PendingSerial() != serial) return false;
@@ -1473,7 +1470,6 @@ bool TableCurrent(ReusedTable& table) {
 }
 
 bool CachedTableTextures(const ReusedTable& table, std::vector<std::shared_ptr<Texture>>& cached) {
-    APS5_STEP("table: cached textures");
     auto& cache = Textures();
     std::lock_guard lock(cache.mutex);
     std::vector<std::list<CachedTexture>::iterator> entries;
@@ -1958,7 +1954,6 @@ void ShaderResources::buildComplete() {
             };
             for (const auto& binding : bindings) {
                 if (binding.flags != 0) {
-                    APS5_STEP("descriptors: table infos");
                     const auto set = setOf(binding);
                     const bool sampled = binding.layout.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                     const auto& allocation = set == 0 ? cacheAllocation : fragment.cacheAllocation;
@@ -2039,10 +2034,7 @@ void ShaderResources::buildComplete() {
                 writes.push_back(write);
             }
             if (keptWrites.empty()) {
-                {
-                APS5_STEP("descriptors: update");
                 context.Resolved(&DeviceFunctions::updateDescriptorSets, "vkUpdateDescriptorSets")(context.device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
-            }
             } else {
                 std::vector<VkWriteDescriptorSet> made;
                 made.reserve(writes.size());
@@ -3489,7 +3481,6 @@ constexpr std::array<VkDescriptorPoolSize, 4> ChainPoolCreateSizes{{{VK_DESCRIPT
 }
 
 DescriptorCache::SetAllocation DescriptorCache::Allocate(VkDescriptorSetLayout layout, std::span<const VkDescriptorPoolSize> sizes, bool updateAfterBind, std::optional<std::uint32_t> variableCount) {
-    APS5_STEP("descriptors: allocate set");
     bool fits = true;
     for (const auto& size : sizes) {
         const auto limit = updateAfterBind && size.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER ? context.descriptorIndexingLimits.maxDescriptorSetUpdateAfterBindStorageBuffers : SetDescriptorLimit(context.limits, size.type);
@@ -3508,7 +3499,6 @@ DescriptorCache::SetAllocation DescriptorCache::Allocate(VkDescriptorSetLayout l
     allocation.descriptorSetCount = 1;
     allocation.pSetLayouts = &layout;
     if (!fits) {
-        APS5_STEP("descriptors: dedicated pool");
         dedicated.reserve(dedicated.size() + 1);
         const auto same = std::find_if(kept.begin(), kept.end(), [&](const SetAllocation& set) {
             return set.layout == layout && set.updateAfterBind == updateAfterBind && set.variableCount == variableCount && std::equal(set.sizes.begin(), set.sizes.end(), sizes.begin(), sizes.end(), [](const VkDescriptorPoolSize& a, const VkDescriptorPoolSize& b) { return a.type == b.type && a.descriptorCount == b.descriptorCount; });
@@ -4254,7 +4244,6 @@ void ShaderResources::addSamplerTable(const ShaderRecompiler::DescriptorBinding&
 
 void ShaderResources::resolveImageTables() {
     if (tableBindings[0] == std::numeric_limits<std::size_t>::max() && tableBindings[1] == std::numeric_limits<std::size_t>::max()) return;
-    APS5_STEP("table: resolve all");
     auto& counters = TextureCounts();
     for (std::size_t set = 0; set < tableBindings.size(); ++set) {
         if (tableBindings[set] != std::numeric_limits<std::size_t>::max()) bindings[tableBindings[set]].imageAllocations.assign(std::min(tableElements[set], context.imageTableCapacity), std::numeric_limits<std::size_t>::max());
@@ -4305,7 +4294,6 @@ void ShaderResources::resolveImageTables() {
         const auto state = CurrentTableState();
         std::vector<std::shared_ptr<Texture>> cached;
         if (auto* reused = FindReusedTable(context.device, binding, state); reused != nullptr && TableCurrent(*reused) && CachedTableTextures(*reused, cached)) {
-            APS5_STEP("table: rebind hit");
             const bool replay = TableReplay() && reused->replayable && reused->imageSamplers == binding.imageSamplers && reused->samplers == samplerKey;
             if (!replay) reused->bound.assign(binding.count, {});
             auto next = cached.begin();
@@ -4337,7 +4325,6 @@ void ShaderResources::resolveImageTables() {
             (replay ? counters.tablesReplayed : counters.tablesReused).fetch_add(1, std::memory_order_relaxed);
             continue;
         }
-        APS5_STEP("table: resolve miss");
         ReusedTable fresh;
         fresh.bound.assign(binding.count, {});
         bool reusable = state.epoch != 0;
diff --git a/core/libs/prx/libSceAgcDriver/tests/execution/ImageTableVillageBench.cpp b/core/libs/prx/libSceAgcDriver/tests/execution/ImageTableVillageBench.cpp
deleted file mode 100644
index 8ab9a61a0..000000000
--- a/core/libs/prx/libSceAgcDriver/tests/execution/ImageTableVillageBench.cpp
+++ /dev/null
@@ -1,234 +0,0 @@
-#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
-#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
-#include "prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp"
-#include "prx/libSceAgcDriver/Submit/include/Acb.hpp"
-#include "prx/libSceAgcDriver/Submit/include/Dcb.hpp"
-#include "prx/libc/include/GuestAllocations.hpp"
-#include "prx/libc/include/GuestArena.hpp"
-#include "prx/libc/include/GuestWriteWatch.hpp"
-#include "VulkanTestDevice.hpp"
-#ifdef _WIN32
-#ifndef NOMINMAX
-#define NOMINMAX
-#endif
-#include <windows.h>
-#else
-#include <sys/mman.h>
-#endif
-#include <algorithm>
-#include <array>
-#include <atomic>
-#include <chrono>
-#include <cstddef>
-#include <cstdint>
-#include <cstdio>
-#include <cstdlib>
-#include <cstring>
-#include <initializer_list>
-#include <iostream>
-#include <string>
-#include <thread>
-#include <vector>
-
-namespace {
-
-using AgcDriver::Graphics::Require;
-
-constexpr std::uint32_t TextureBytes = 2048;
-constexpr std::uint32_t Aliases = 256;
-constexpr std::uint32_t Format8888UNorm = 56;
-constexpr std::uint32_t Type2D = 9;
-constexpr std::size_t SrtBytes = 256;
-
-constexpr std::array<std::uint32_t, 17> TableCode{0xf4080100u, 0xfa000000u, 0xf4080200u, 0xfa000010u, 0xf4080300u, 0xfa000020u, 0xf4080700u, 0xfa000030u, 0x7e200500u, 0x8f108510u, 0xf42c0502u, 0x20000000u, 0xf09c0f08u, 0x00450000u, 0xe0700000u, 0x80070000u, 0xbf810000u};
-
-std::array<std::uint32_t, 4> V(std::uint64_t address, std::uint32_t stride, std::uint32_t records) {
-    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (stride << 16u), records, stride == 0u ? 0x31016facu : 0x01016facu};
-}
-
-std::vector<std::uint32_t> Pm4(std::uint32_t opcode, std::initializer_list<std::uint32_t> body) {
-    std::vector<std::uint32_t> words{0xc0000000u | (static_cast<std::uint32_t>(body.size() - 1) << 16u) | (opcode << 8u)};
-    words.insert(words.end(), body);
-    return words;
-}
-
-std::uint32_t BenchSetting(const char* name, std::uint32_t fallback) {
-    const char* value = std::getenv(name);
-    return value != nullptr ? static_cast<std::uint32_t>(std::strtoul(value, nullptr, 10)) : fallback;
-}
-
-constexpr std::array<std::uint32_t, 7> BenchStoreCode{0x7e020d00u, 0x082802f7u, 0x7e3c0300u, 0x7e3e0280u, 0xf0201108u, 0x0001141eu, 0xbf810000u};
-
-class BenchBlock {
-public:
-    explicit BenchBlock(std::size_t bytes) : bytes((bytes + 65535u) & ~static_cast<std::size_t>(65535)) {
-#ifdef _WIN32
-        block = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(this->bytes, 65536));
-        if (block != nullptr) GuestArena::GuestArenaCommit_nid_postfix(block, this->bytes, PAGE_READWRITE, this->bytes);
-#else
-        constexpr std::uintptr_t alignment = 65536;
-        void* mapped = mmap(nullptr, this->bytes + alignment, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
-        if (mapped != MAP_FAILED) {
-            const auto begin = reinterpret_cast<std::uintptr_t>(mapped);
-            const auto aligned = (begin + alignment - 1) & ~(alignment - 1);
-            if (aligned != begin) munmap(mapped, aligned - begin);
-            if (aligned + this->bytes != begin + this->bytes + alignment) munmap(reinterpret_cast<void*>(aligned + this->bytes), begin + alignment - aligned);
-            block = reinterpret_cast<std::uint8_t*>(aligned);
-            GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(block, this->bytes);
-        }
-#endif
-        Require(block != nullptr, "image table bench: cannot allocate the guest block");
-        GuestAllocations::Mutation().Add(block, this->bytes, true, true, true);
-    }
-
-    BenchBlock(const BenchBlock&) = delete;
-    BenchBlock& operator=(const BenchBlock&) = delete;
-
-    std::uint64_t At(std::size_t offset) const { return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(block + offset)); }
-    std::uint8_t* Bytes(std::size_t offset) const { return block + offset; }
-
-private:
-    std::size_t bytes;
-    std::uint8_t* block = nullptr;
-};
-
-struct BenchLayout {
-    std::uint32_t records, stride, elements, threads, outputs, storage, heaps;
-    std::size_t storeCode = 256, srts = 4096, outputsAt = 0, heap = 0, heapBytes = 0, textures = 0, storageAt = 0, total = 0;
-
-    void Place() {
-        outputsAt = srts + static_cast<std::size_t>(threads) * outputs * SrtBytes;
-        heap = (outputsAt + static_cast<std::size_t>(threads) * outputs * 4096u + 65535u) & ~static_cast<std::size_t>(65535);
-        heapBytes = (static_cast<std::size_t>(records) * stride + 65535u) & ~static_cast<std::size_t>(65535);
-        textures = heap + heapBytes * heaps;
-        storageAt = (textures + static_cast<std::size_t>((elements + Aliases - 1u) / Aliases) * TextureBytes + 65535u) & ~static_cast<std::size_t>(65535);
-        total = storageAt + static_cast<std::size_t>(threads) * storage * 1024u;
-    }
-};
-
-std::vector<std::uint32_t> BenchTableDispatch(const BenchBlock& block, const BenchLayout& layout, std::uint32_t thread, std::uint32_t output) {
-    const auto kernel = block.At(0);
-    const auto srt = block.At(layout.srts + (static_cast<std::size_t>(thread) * layout.outputs + output) * SrtBytes);
-    std::vector<std::uint32_t> words;
-    for (const auto& packet : {Pm4(0x76, {0x20c, static_cast<std::uint32_t>(kernel >> 8u)}), Pm4(0x76, {0x20d, static_cast<std::uint32_t>(kernel >> 40u) & 0xffu}), Pm4(0x76, {0x207, 64}), Pm4(0x76, {0x208, 1}), Pm4(0x76, {0x209, 1}), Pm4(0x76, {0x213, 2u << 1u}), Pm4(0x76, {0x240, static_cast<std::uint32_t>(srt)}), Pm4(0x76, {0x241, static_cast<std::uint32_t>(srt >> 32u)}), Pm4(0x15, {1, 1, 1, 0x41})}) {
-        words.insert(words.end(), packet.begin(), packet.end());
-    }
-    return words;
-}
-
-std::vector<std::uint32_t> BenchStoreDispatch(const BenchBlock& block, const BenchLayout& layout, std::uint32_t thread, std::uint32_t image) {
-    const auto kernel = block.At(layout.storeCode);
-    const auto texels = block.At(layout.storageAt + (static_cast<std::size_t>(thread) * layout.storage + image) * 1024u);
-    const std::array<std::uint32_t, 4> t{static_cast<std::uint32_t>(texels >> 8u), static_cast<std::uint32_t>((texels >> 40u) & 0xffu) | (22u << 20u) | ((63u & 3u) << 30u), (63u >> 2u) | (3u << 14u) | (1u << 31u), 0xfacu | (Type2D << 28u)};
-    std::vector<std::uint32_t> words;
-    for (const auto& packet : {Pm4(0x76, {0x20c, static_cast<std::uint32_t>(kernel >> 8u)}), Pm4(0x76, {0x20d, static_cast<std::uint32_t>(kernel >> 40u) & 0xffu}), Pm4(0x76, {0x207, 64}), Pm4(0x76, {0x208, 1}), Pm4(0x76, {0x209, 1}), Pm4(0x76, {0x213, 16u << 1u})}) {
-        words.insert(words.end(), packet.begin(), packet.end());
-    }
-    for (std::uint32_t slot = 0; slot < 16; ++slot) {
-        const auto packet = Pm4(0x76, {0x240 + slot, slot >= 4 && slot < 8 ? t[slot - 4] : 0u});
-        words.insert(words.end(), packet.begin(), packet.end());
-    }
-    const auto dispatch = Pm4(0x15, {1, 1, 1, 0x41});
-    words.insert(words.end(), dispatch.begin(), dispatch.end());
-    return words;
-}
-
-void BenchSubmit(std::uint32_t queue, std::vector<std::uint32_t>& words) {
-    Packet packet{words.data(), static_cast<std::uint32_t>(words.size()), 0, {}};
-    Require((queue == 0 ? sceAgcDriverSubmitDcb(&packet) : sceAgcDriverSubmitAcb(queue, &packet)) == 0, "image table bench: a submission failed");
-}
-
-int VillageBench() {
-    BenchLayout layout{BenchSetting("BENCH_RECORDS", 32768), BenchSetting("BENCH_STRIDE", 48), BenchSetting("BENCH_ELEMENTS", 4096), std::max(1u, BenchSetting("BENCH_THREADS", 3)), std::max(1u, BenchSetting("BENCH_OUTPUTS", 16)), BenchSetting("BENCH_STORAGE", 8), std::max(1u, BenchSetting("BENCH_HEAPS", 1))};
-    layout.Place();
-    const auto batch = std::max(1u, BenchSetting("BENCH_BATCH", 8));
-    const auto seconds = std::max(1u, BenchSetting("BENCH_SECONDS", 21));
-    const auto writeEvery = BenchSetting("BENCH_WRITE_EVERY", 0);
-    BenchBlock block(layout.total);
-    auto code = TableCode;
-    if (layout.stride != 32u) {
-        Require(layout.stride <= 64u, "image table bench: BENCH_STRIDE above 64 needs a literal multiply");
-        std::replace(code.begin(), code.end(), 0x8f108510u, 0x93108010u | (layout.stride << 8u));
-    }
-    std::memcpy(block.Bytes(0), code.data(), sizeof(code));
-    std::memcpy(block.Bytes(layout.storeCode), BenchStoreCode.data(), sizeof(BenchStoreCode));
-    for (std::uint32_t surface = 0; surface < (layout.elements + Aliases - 1u) / Aliases; ++surface) std::memset(block.Bytes(layout.textures + static_cast<std::size_t>(surface) * TextureBytes), static_cast<int>(surface), TextureBytes);
-    const auto setRecord = [&](std::uint32_t heap, std::uint32_t record, std::uint32_t element) {
-        const auto address = block.At(layout.textures + static_cast<std::size_t>(element / Aliases) * TextureBytes);
-        const std::array<std::uint32_t, 8> words{static_cast<std::uint32_t>(address >> 8u), static_cast<std::uint32_t>((address >> 40u) & 0xffu) | (Format8888UNorm << 20u) | (3u << 30u), 7u << 14u, 0xfacu | (Type2D << 28u), 0u, 0u, element % Aliases, 0u};
-        std::memcpy(block.Bytes(layout.heap + heap * layout.heapBytes + static_cast<std::size_t>(record) * layout.stride), words.data(), sizeof(words));
-    };
-    for (std::uint32_t heap = 0; heap < layout.heaps; ++heap) {
-        for (std::uint32_t record = 0; record < layout.records; ++record) setRecord(heap, record, record % layout.elements);
-    }
-    for (std::uint32_t thread = 0; thread < layout.threads; ++thread) {
-        for (std::uint32_t output = 0; output < layout.outputs; ++output) {
-            auto* srt = reinterpret_cast<std::uint32_t*>(block.Bytes(layout.srts + (static_cast<std::size_t>(thread) * layout.outputs + output) * SrtBytes));
-            std::fill(srt, srt + SrtBytes / 4u, 0u);
-            const auto heap = V(block.At(layout.heap + ((static_cast<std::size_t>(thread) * layout.outputs + output) % layout.heaps) * layout.heapBytes), layout.stride, layout.records);
-            const auto out = V(block.At(layout.outputsAt + (static_cast<std::size_t>(thread) * layout.outputs + output) * 4096u), 0u, 4096u);
-            std::copy(heap.begin(), heap.end(), srt);
-            std::copy(out.begin(), out.end(), srt + 12);
-        }
-    }
-    std::printf("VILLAGEBENCH heap watched %d, collect %llu, write-watched %d\n", AgcDriver::GuestMemory::Watched(block.At(layout.heap), static_cast<std::size_t>(layout.records) * layout.stride) ? 1 : 0, static_cast<unsigned long long>(AgcDriver::GuestMemory::CollectWrites(block.At(layout.heap), static_cast<std::size_t>(layout.records) * layout.stride)), AgcDriver::GuestMemory::WriteWatched() ? 1 : 0);
-    const auto perRound = std::max(1u, BenchSetting("BENCH_ROUND", 16));
-    std::atomic<std::uint64_t> dispatches{0}, stores{0}, writes{0};
-    std::vector<std::uint32_t> outputs(layout.threads, 0u), images(layout.threads, 0u);
-    std::uint32_t round = 0;
-    const auto started = std::chrono::steady_clock::now();
-    while (std::chrono::steady_clock::now() - started < std::chrono::seconds(seconds)) {
-        if (writeEvery != 0 && ++round % writeEvery == 0) {
-            setRecord(round % layout.heaps, (round * 7919u) % layout.records, (round * 31u) % layout.elements);
-            writes.fetch_add(1, std::memory_order_relaxed);
-        }
-        std::vector<std::thread> workers;
-        for (std::uint32_t thread = 0; thread < layout.threads; ++thread) {
-            workers.emplace_back([&, thread] {
-                const std::uint32_t queue = thread == 0 ? 0u : 0x20u + thread;
-                for (std::uint32_t submission = 0; submission < perRound; ++submission) {
-                    std::vector<std::uint32_t> words;
-                    for (std::uint32_t index = 0; index < batch; ++index) {
-                        const auto table = BenchTableDispatch(block, layout, thread, outputs[thread]);
-                        words.insert(words.end(), table.begin(), table.end());
-                        outputs[thread] = (outputs[thread] + 1u) % layout.outputs;
-                    }
-                    if (layout.storage != 0) {
-                        const auto store = BenchStoreDispatch(block, layout, thread, images[thread]);
-                        words.insert(words.end(), store.begin(), store.end());
-                        images[thread] = (images[thread] + 1u) % layout.storage;
-                        stores.fetch_add(1, std::memory_order_relaxed);
-                    }
-                    BenchSubmit(queue, words);
-                    dispatches.fetch_add(batch, std::memory_order_relaxed);
-                }
-            });
-        }
-        for (auto& worker : workers) worker.join();
-        AgcDriverWaitIdle_nid_postfix();
-    }
-    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
-    std::printf("VILLAGEBENCH records %u stride %u elements %u threads %u outputs %u storage %u heaps %u batch %u write-every %u: %llu table dispatches, %llu stores, %llu table writes in %.1f s (%.0f table dispatches/s)\n", layout.records, layout.stride, layout.elements, layout.threads, layout.outputs, layout.storage, layout.heaps, batch, writeEvery, static_cast<unsigned long long>(dispatches.load()), static_cast<unsigned long long>(stores.load()), static_cast<unsigned long long>(writes.load()), elapsed, static_cast<double>(dispatches.load()) / elapsed);
-    std::fflush(stdout);
-    AgcDriverShutdown_nid_postfix();
-    return 0;
-}
-}
-
-int main(int argc, char** argv) {
-    if (argc != 2 || std::string(argv[1]) != "--village-bench") {
-        std::puts("image table village bench: run with --village-bench");
-        return 0;
-    }
-    try {
-        {
-            const auto device = OpenVulkanTestDevice();
-            if (!device) return VulkanTestSkipped;
-        }
-        return VillageBench();
-    } catch (const std::exception& error) {
-        std::cerr << error.what() << '\n';
-        AgcDriverShutdown_nid_postfix();
-        return 1;
-    }
-}
```
