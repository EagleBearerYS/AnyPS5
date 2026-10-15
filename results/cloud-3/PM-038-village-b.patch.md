# PM-038: Village B side (v3 on test/village-fast-main-ports 4cd4260a9a)

Apply on a clean 4cd4260a9a with `git apply PM-038-village-b.patch.md` (git skips this preamble and the closing fence). Result: tree 49518e25f.

```diff
diff --git a/core/libs/prx/libSceAgcDriver/CMakeLists.txt b/core/libs/prx/libSceAgcDriver/CMakeLists.txt
index de5f6cffc..fb2105281 100644
--- a/core/libs/prx/libSceAgcDriver/CMakeLists.txt
+++ b/core/libs/prx/libSceAgcDriver/CMakeLists.txt
@@ -1449,6 +1449,8 @@ target_include_directories(agc_driver_alpha_to_coverage_tests PRIVATE $<TARGET_P
 
 target_include_directories(agc_driver_sample_multisample_target_tests PRIVATE $<TARGET_PROPERTY:shader_recompiler_agc_driver,INTERFACE_INCLUDE_DIRECTORIES>)
 
+target_include_directories(agc_driver_image_table_cache_bounds_tests PRIVATE $<TARGET_PROPERTY:shader_recompiler_agc_driver,INTERFACE_INCLUDE_DIRECTORIES>)
+
 if(BUILD_TESTING)
     add_test(NAME agc_driver_undithered_alpha_to_coverage_state COMMAND agc_driver_graphics_tests --undithered-alpha-to-coverage-state)
     set_tests_properties(agc_driver_undithered_alpha_to_coverage_state PROPERTIES
@@ -1566,6 +1568,13 @@ if(BUILD_TESTING)
                 "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
         endif()
     endforeach()
+    add_test(NAME agc_driver_image_table_vouched_lookups COMMAND agc_driver_image_table_cache_bounds_tests --vouched-lookups)
+    set_tests_properties(agc_driver_image_table_vouched_lookups PROPERTIES ENVIRONMENT "ANYPS5_NO_SHADER_CACHE=1;APS5_WRITE_WATCH_IMPORTS=watch" SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 60)
+    add_test(NAME agc_driver_image_table_vouch_queued_draws COMMAND agc_driver_image_table_cache_bounds_tests --queued-draws)
+    set_tests_properties(agc_driver_image_table_vouch_queued_draws PROPERTIES ENVIRONMENT "ANYPS5_NO_SHADER_CACHE=1;APS5_PIPELINED_DRAWS=1" SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 60)
+    if(WIN32)
+        set_tests_properties(agc_driver_image_table_vouched_lookups agc_driver_image_table_vouch_queued_draws PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
+    endif()
     add_test(NAME agc_driver_partially_resident_pending_stores COMMAND agc_driver_partially_resident_snapshot_tests --pending-stores)
     set_tests_properties(agc_driver_partially_resident_pending_stores PROPERTIES ENVIRONMENT "ANYPS5_NO_SHADER_CACHE=1" SKIP_RETURN_CODE 77 WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 30)
     if(WIN32)
diff --git a/core/libs/prx/libSceAgcDriver/Execution/include/Driver/Dispatch/DispatchCache.hpp b/core/libs/prx/libSceAgcDriver/Execution/include/Driver/Dispatch/DispatchCache.hpp
index 110894795..0f6d8d868 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/include/Driver/Dispatch/DispatchCache.hpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/include/Driver/Dispatch/DispatchCache.hpp
@@ -27,6 +27,7 @@ struct DispatchVariant {
 
     std::vector<std::pair<std::uint64_t, std::uint64_t>> runs;
     std::vector<std::uint32_t> words;
+    std::vector<SharedWords> shared;
     std::uint64_t forgetSerial = 0;
     std::shared_ptr<const ShaderRecompiler::RecompileResult> compiled;
 
diff --git a/core/libs/prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawCache.hpp b/core/libs/prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawCache.hpp
index cf7e9a6a9..b57600ec3 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawCache.hpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawCache.hpp
@@ -115,6 +115,7 @@ struct StageCapture {
     std::shared_ptr<const ShaderRecompiler::RecompileResult> compiled;
     std::shared_ptr<const ShaderRecompiler::ResourceCapture> capture;
     std::vector<ShaderRecompiler::MemoryRegion> regions;
+    std::vector<std::shared_ptr<const ShaderRecompiler::SrtGuestWords>> supplied;
     std::uint64_t forgetSerial = 0;
     std::uint32_t pushOffset = 0;
 };
diff --git a/core/libs/prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawPipeline.hpp b/core/libs/prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawPipeline.hpp
index d3fa4e773..d2d2556f1 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawPipeline.hpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawPipeline.hpp
@@ -34,6 +34,7 @@ public:
     void Enqueue(Commit commit, std::vector<Range> writes, std::uint64_t labelAddress = 0, std::vector<std::byte> labelBytes = {});
     void Drain(DrainReason reason, std::uint32_t opcode = 0x100);
     bool Busy() const { return outstanding.load(std::memory_order_acquire) != 0; }
+    std::size_t Outstanding() const { return outstanding.load(std::memory_order_acquire); }
     bool Overlaps(std::uint64_t address, std::size_t bytes);
     bool Overlaps(std::span<const Range> ranges);
     std::optional<std::uint64_t> PendingLabel(std::uint64_t address, std::size_t bytes);
diff --git a/core/libs/prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp b/core/libs/prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp
index f01ee64f4..c75c74413 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp
@@ -74,6 +74,7 @@ public:
     };
     DispatchCacheFootprint DispatchCacheSize();
     static std::uint64_t DispatchCacheBytes();
+    static ShaderMemory::PendingWrite QueryPendingSpan(std::uint64_t address, std::size_t bytes, std::span<std::byte> known);
 
 private:
     friend class SampledReadScope;
@@ -193,6 +194,7 @@ private:
     static std::uint64_t hookWaits();
     static ShaderMemory::HookWaitCounter hookWaitCounter();
     static bool validateSkipEnabled();
+    static bool vouchedValidation();
     static bool validateLegacy();
     static bool gateRetry();
     static bool validateSkipVerify();
@@ -227,10 +229,10 @@ private:
     void traceCapture(const char* what, std::uint64_t program, std::uint32_t queue, std::span<const ShaderRecompiler::MemoryRegion> regions, double waitedMs);
     void reportValidation(ValidateCounters& counters);
     bool captureStable(std::span<const ShaderRecompiler::MemoryRegion> captured);
-    bool validateCaptured(std::uint64_t program, std::uint32_t queue, std::span<const ShaderRecompiler::MemoryRegion> captured, const ShaderRecompiler::RecompileResult& compiled, bool inPlace, const PendingView& view, bool* unmapped = nullptr, std::optional<SampledReadScope>* sampling = nullptr, const DataMask* data = nullptr);
+    bool validateCaptured(std::uint64_t program, std::uint32_t queue, std::span<const ShaderRecompiler::MemoryRegion> captured, const ShaderRecompiler::RecompileResult& compiled, bool inPlace, const PendingView& view, bool* unmapped = nullptr, std::optional<SampledReadScope>* sampling = nullptr, const DataMask* data = nullptr, std::span<const VouchedRegions> vouched = {});
     static void appendEntryRegions(const DispatchVariant& variant, std::vector<ShaderRecompiler::MemoryRegion>& regions, const std::vector<std::uint32_t>* words = nullptr);
     static std::shared_ptr<const ShaderRecompiler::RecompileResult> patchDataWords(const DispatchVariant& variant, const std::vector<std::uint32_t>& words);
-    bool syncPendingRuns(std::uint64_t program, std::uint32_t queue, const ShaderRecompiler::RecompileResult& compiled, std::span<const ShaderRecompiler::MemoryRegion> regions, std::uint64_t& synced, PendingView& pending, std::optional<SampledReadScope>& sampling);
+    bool syncPendingRuns(std::uint64_t program, std::uint32_t queue, const ShaderRecompiler::RecompileResult& compiled, std::span<const ShaderRecompiler::MemoryRegion> regions, std::uint64_t& synced, PendingView& pending, std::optional<SampledReadScope>& sampling, std::span<const VouchedRegions> skipped = {});
     EntryOutcome validateVariant(std::uint64_t program, std::uint32_t queue, const DispatchVariant& variant, std::span<const ShaderRecompiler::MemoryRegion> regions, std::uint64_t& imagesFlushed, std::uint64_t& runsSynced, std::optional<SampledReadScope>& sampling, std::vector<std::pair<std::uint32_t, std::uint32_t>>* live = nullptr);
     static int waitTimeoutMs();
     static bool pollReapAll();
diff --git a/core/libs/prx/libSceAgcDriver/Execution/include/ShaderMemory.hpp b/core/libs/prx/libSceAgcDriver/Execution/include/ShaderMemory.hpp
index b1133d503..2ca03d7d9 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/include/ShaderMemory.hpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/include/ShaderMemory.hpp
@@ -17,6 +17,7 @@ namespace ShaderRecompiler {
 struct SourceHandle;
 class PreparedShaderInvocation;
 struct SrtWriteStamp;
+struct SrtGuestWords;
 }
 
 namespace AgcDriver {
@@ -57,7 +58,7 @@ public:
     // by the driver: the [capture] line charges the waits made inside a capture to it.
     using WaitedMsProvider = double (*)();
     static void SetWaitedMsProvider(WaitedMsProvider provider);
-    explicit ShaderMemory(std::span<const ShaderRecompiler::MemoryRegion> initial, PendingWriteQuery pendingWrite = nullptr, PendingWriteObserver observe = nullptr, HookWaitCounter hookWaits = nullptr);
+    explicit ShaderMemory(std::span<const ShaderRecompiler::MemoryRegion> initial, PendingWriteQuery pendingWrite = nullptr, PendingWriteObserver observe = nullptr, HookWaitCounter hookWaits = nullptr, PendingWriteQuery pendingSpan = nullptr);
     ~ShaderMemory();
     ShaderMemory(const ShaderMemory&) = delete;
     ShaderMemory& operator=(const ShaderMemory&) = delete;
@@ -77,6 +78,16 @@ public:
     [[nodiscard]] std::vector<ShaderRecompiler::MemoryRegion> TakeRecentRegions();
     // The driver's source handle memo outcomes, for the [capture] line (APS5_PROFILE_DRAW).
     static void CountHandleMemo(bool hit);
+    struct SupplyCounts {
+        std::uint64_t shared, copied, spilled;
+    };
+    static SupplyCounts Supplies();
+    [[nodiscard]] std::vector<std::shared_ptr<const ShaderRecompiler::SrtGuestWords>> SuppliedBlocks() const;
+    struct VouchCounts {
+        std::uint64_t calls, blocks, refused, words;
+    };
+    static VouchCounts Vouches();
+    static bool Vouched(PendingWriteQuery pending, std::uint64_t address, std::uint64_t bytes, const ShaderRecompiler::SrtWriteStamp& stamp);
 
 private:
     std::shared_ptr<const ShaderRecompiler::ResourceCapture> capture(const ShaderRecompiler::RecompileRequest& request, const ShaderRecompiler::SourceHandle* handle, const ShaderRecompiler::PreparedShaderInvocation* invocation);
@@ -109,16 +120,27 @@ private:
         bool lazy = false;
     };
 
+    struct Supplied {
+        std::shared_ptr<const ShaderRecompiler::SrtGuestWords> words;
+        std::uint64_t begin = 0;
+        std::uint64_t end = 0;
+        bool recent = true;
+    };
+
     static bool read(void* context, std::uint64_t address, std::uint32_t* value);
     static bool readWords(void* context, std::uint64_t address, std::uint32_t* values, std::uint64_t count);
     static bool accessible(void* context, std::uint64_t address, std::uint64_t bytes);
     static bool pending(void* context, std::uint64_t address, std::uint64_t bytes);
     static ShaderRecompiler::SrtWriteStamp stampWrites(void* context, std::uint64_t address, std::uint64_t bytes);
     static bool unchangedSince(void* context, std::uint64_t address, std::uint64_t bytes, const ShaderRecompiler::SrtWriteStamp& stamp);
-    static bool supplyWords(void* context, std::uint64_t address, const std::uint32_t* values, std::uint64_t count);
+    static bool supplyWords(void* context, const std::shared_ptr<const ShaderRecompiler::SrtGuestWords>& words);
     Page& page(std::uint64_t base);
     Page& insertPage(std::vector<std::pair<std::uint64_t, Page*>>::iterator position, std::uint64_t base);
     [[nodiscard]] bool overlapsInitial(std::uint64_t begin, std::uint64_t end) const;
+    bool place(std::uint64_t address, const std::uint32_t* values, std::uint64_t count, bool overwrite, bool recent);
+    void spill(std::size_t index);
+    void spillTouching(std::uint64_t begin, std::uint64_t end);
+    [[nodiscard]] std::vector<ShaderRecompiler::MemoryRegion> collect(bool recentOnly) const;
 
     // Regions given at construction (the registered shader's code and header), referenced as given:
     // the caller keeps them alive for as long as the capture is used.
@@ -126,9 +148,12 @@ private:
     std::vector<std::pair<std::uint64_t, Page*>> pages;
     std::uint64_t lastBase = ~std::uint64_t{0};
     Page* lastPage = nullptr;
+    std::vector<Supplied> supplied;
+    std::vector<std::shared_ptr<const ShaderRecompiler::SrtGuestWords>> spilled;
     PendingWriteQuery pendingWrite = nullptr;
     PendingWriteObserver observe = nullptr;
     HookWaitCounter hookWaits = nullptr;
+    PendingWriteQuery pendingSpan = nullptr;
 };
 
 // The positions, among a dispatch-cache variant's stored words, of the pure flat-SRT leaves a
@@ -146,6 +171,25 @@ struct DataWordPositionCounts {
 };
 DataWordPositionCounts DataWordPositions(std::span<const std::pair<std::uint64_t, std::uint64_t>> runs, std::span<const std::pair<std::uint32_t, std::uint64_t>> leaves, std::span<const std::uint64_t> otherReads, std::span<const std::uint32_t> words, std::span<const std::uint32_t> flattenedSrt, std::vector<std::uint32_t>& positions, std::vector<std::uint32_t>& slots);
 
+struct SharedWords {
+    std::size_t first = 0;
+    std::size_t count = 0;
+    std::size_t offset = 0;
+    std::weak_ptr<const ShaderRecompiler::SrtGuestWords> block;
+    std::size_t region = 0;
+    std::size_t regions = 0;
+};
+void AppendSharedWords(std::vector<SharedWords>& shared, std::size_t region, std::size_t first, std::span<const std::byte> bytes, std::span<const std::shared_ptr<const ShaderRecompiler::SrtGuestWords>> blocks);
+bool SameCapturedWords(std::span<const std::uint32_t> left, std::span<const SharedWords> leftShared, std::span<const std::uint32_t> right, std::span<const SharedWords> rightShared, std::size_t* compared = nullptr);
+struct VouchedRegions {
+    std::size_t begin = 0;
+    std::size_t end = 0;
+    std::size_t words = 0;
+    std::uint64_t low = 0;
+    std::uint64_t high = 0;
+};
+std::size_t VouchSharedRegions(std::span<const SharedWords> shared, std::size_t regions, ShaderMemory::PendingWriteQuery pending, std::vector<VouchedRegions>& vouched);
+
 }
 
 #endif
diff --git a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/DataHitVerification.cpp b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/DataHitVerification.cpp
index a13acf5c8..2b47c8cc5 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/DataHitVerification.cpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/DataHitVerification.cpp
@@ -12,7 +12,7 @@ void Driver::verifyDataHit(const ShaderSnapshot& snapshot, std::size_t codeOffse
         std::fflush(stderr);
         std::abort();
     };
-    auto verifyMemory = std::make_shared<ShaderMemory>(memory, &queryPendingWrite, &observePendingWrite, hookWaitCounter());
+    auto verifyMemory = std::make_shared<ShaderMemory>(memory, &queryPendingWrite, &observePendingWrite, hookWaitCounter(), &QueryPendingSpan);
     const auto invocation = InvocationFor(snapshot, codeOffset, request);
     const auto capture = verifyMemory->Capture(invocation);
     const auto regions = verifyMemory->Regions();
diff --git a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/Dispatch.cpp b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/Dispatch.cpp
index 767fbfdec..a50e7f115 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/Dispatch.cpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/Dispatch.cpp
@@ -246,7 +246,7 @@ void Driver::dispatch(QueueState& queue, std::span<const std::uint32_t> packet,
         require(keepVariant != nullptr && keepVariant->shader == registeredShader, "dispatch cache belongs to another registered shader");
         captureMs += phaseTiming.Elapsed();
     } else if (!fromAhead) {
-        shaderMemory = std::make_shared<ShaderMemory>(memory, &queryPendingWrite, &observePendingWrite, hookWaitCounter());
+        shaderMemory = std::make_shared<ShaderMemory>(memory, &queryPendingWrite, &observePendingWrite, hookWaitCounter(), &QueryPendingSpan);
         std::uint64_t forgetAtCapture = 0;
 
         static const bool dumpShaders = std::getenv("APS5_DUMP_SHADERS") != nullptr;
diff --git a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/DispatchCache.cpp b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/DispatchCache.cpp
index 812b138b7..97c5d143a 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/DispatchCache.cpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/DispatchCache.cpp
@@ -60,7 +60,7 @@ Driver::DispatchCacheFootprint Driver::DispatchCacheSize() {
 }
 
 std::uint64_t Driver::variantBytes(const DispatchVariant& variant) {
-    return sizeof(DispatchVariant) + variant.words.size() * sizeof(std::uint32_t) + variant.runs.size() * sizeof(std::pair<std::uint64_t, std::uint64_t>) + variant.captured.size() * sizeof(ShaderRecompiler::MemoryRegion) + (variant.vertexInfo != nullptr ? sizeof(ShaderRecompiler::ShaderVertexStageInfo) : 0);
+    return sizeof(DispatchVariant) + variant.words.size() * sizeof(std::uint32_t) + variant.shared.size() * sizeof(SharedWords) + variant.runs.size() * sizeof(std::pair<std::uint64_t, std::uint64_t>) + variant.captured.size() * sizeof(ShaderRecompiler::MemoryRegion) + (variant.vertexInfo != nullptr ? sizeof(ShaderRecompiler::ShaderVertexStageInfo) : 0);
 }
 
 void Driver::accountVariant(const DispatchVariant& variant, bool added) {
diff --git a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/DispatchInsert.cpp b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/DispatchInsert.cpp
index 89c1aeadc..e732e518e 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/DispatchInsert.cpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/DispatchInsert.cpp
@@ -22,15 +22,23 @@ void Driver::insertDispatch(std::uint64_t address, std::uint64_t key, bool noDis
         fresh->forgetSerial = forgetAtCapture;
 
         std::vector<ShaderRecompiler::MemoryRegion> srtRegions;
+        std::size_t capturedWords = 0;
         for (const auto& region : captured) {
             const bool registered = std::any_of(memory.begin(), memory.end(), [&](const auto& known) { return region.guestAddress >= known.guestAddress && region.guestAddress < known.guestAddress + known.bytes.size(); });
             if (registered) continue;
             srtRegions.push_back(region);
+            capturedWords += region.bytes.size() / sizeof(std::uint32_t);
+        }
+        const auto supplied = shaderMemory->SuppliedBlocks();
+        fresh->runs.reserve(srtRegions.size());
+        fresh->words.reserve(capturedWords);
+        for (const auto& region : srtRegions) {
             fresh->runs.emplace_back(region.guestAddress, region.guestAddress + region.bytes.size());
             const auto count = region.bytes.size() / sizeof(std::uint32_t);
             const auto offset = fresh->words.size();
             fresh->words.resize(offset + count);
             std::memcpy(fresh->words.data() + offset, region.bytes.data(), count * sizeof(std::uint32_t));
+            AppendSharedWords(fresh->shared, fresh->runs.size() - 1u, offset, region.bytes, supplied);
         }
 
         DataWordPositionCounts dataCounts;
@@ -91,7 +99,7 @@ void Driver::insertDispatch(std::uint64_t address, std::uint64_t key, bool noDis
                 dispatchOrder.push_front(key);
                 created->order = dispatchOrder.begin();
                 dispatchCache.emplace(key, std::move(created));
-            } else if (found->second != missedEntry && !found->second->variants.empty() && found->second->variants.front()->runs == fresh->runs && found->second->variants.front()->words == fresh->words) {
+            } else if (found->second != missedEntry && !found->second->variants.empty() && found->second->variants.front()->runs == fresh->runs && SameCapturedWords(found->second->variants.front()->words, found->second->variants.front()->shared, fresh->words, fresh->shared)) {
 
                 attachVariant = found->second->variants.front();
             } else {
diff --git a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/VariantValidation.cpp b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/VariantValidation.cpp
index 0f29b6ee1..d47eb4091 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/VariantValidation.cpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Dispatch/VariantValidation.cpp
@@ -1,6 +1,7 @@
 #include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
 #include "prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawPipeline.hpp"
 #include "prx/libSceAgcDriver/Execution/include/Driver/Synchronization/DeferredLabels.hpp"
+#include "ThreadOwned.hpp"
 #include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
 #include <cstdlib>
 #include <cstring>
@@ -27,10 +28,15 @@ std::shared_ptr<const ShaderRecompiler::RecompileResult> Driver::patchDataWords(
     return patched;
 }
 
-bool Driver::syncPendingRuns(std::uint64_t program, std::uint32_t queue, const ShaderRecompiler::RecompileResult& compiled, std::span<const ShaderRecompiler::MemoryRegion> regions, std::uint64_t& synced, PendingView& pending, std::optional<SampledReadScope>& sampling) {
+bool Driver::syncPendingRuns(std::uint64_t program, std::uint32_t queue, const ShaderRecompiler::RecompileResult& compiled, std::span<const ShaderRecompiler::MemoryRegion> regions, std::uint64_t& synced, PendingView& pending, std::optional<SampledReadScope>& sampling, std::span<const VouchedRegions> skipped) {
     using Policy = ShaderMemory::PendingWrite;
     static const bool profile = std::getenv("APS5_PROFILE_DRAW") != nullptr;
-    for (const auto& region : regions) {
+    for (std::size_t index = 0, range = 0; index < regions.size(); ++index) {
+        if (range < skipped.size() && index == skipped[range].begin) {
+            index = skipped[range++].end - 1u;
+            continue;
+        }
+        const auto& region = regions[index];
         if (!pending.Overlaps(region.guestAddress, region.bytes.size())) continue;
         if (!sampling) sampling.emplace(siteSampling() ? evidenceValidations : evidenceReads);
         std::uint64_t ValidateCounters::*reason = &ValidateCounters::syncedOff;
@@ -77,19 +83,44 @@ bool Driver::syncPendingRuns(std::uint64_t program, std::uint32_t queue, const S
     return true;
 }
 
+bool Driver::vouchedValidation() {
+    static const bool enabled = std::getenv("APS5_NO_VOUCHED_VALIDATION") == nullptr;
+    return enabled;
+}
+
 EntryOutcome Driver::validateVariant(std::uint64_t program, std::uint32_t queue, const DispatchVariant& variant, std::span<const ShaderRecompiler::MemoryRegion> regions, std::uint64_t& imagesFlushed, std::uint64_t& runsSynced, std::optional<SampledReadScope>& sampling, std::vector<std::pair<std::uint32_t, std::uint32_t>>* live) {
     if (live != nullptr) live->clear();
     if (DrawPipeline::Active() && DrawPipeline::Queue0().Busy() && DrawPipeline::Queue0().Overlaps(variant.runs)) DrawPipeline::Queue0().Drain(DrawPipeline::DrainReason::Capture);
     const bool masked = live != nullptr && !variant.dataPositions.empty();
     const DataMask mask{variant.dataPositions, live};
+    thread_local std::vector<VouchedRegions>* vouchedSlot = nullptr;
+    auto& vouched = ShaderRecompiler::ThreadOwned(vouchedSlot);
+    vouched.clear();
+    if (!variant.shared.empty() && vouchedValidation() && !validateLegacy() && variant.runs.size() == regions.size()) VouchSharedRegions(variant.shared, regions.size(), &QueryPendingSpan, vouched);
     PendingView pending;
     pending.Load();
-    if (!syncPendingRuns(program, queue, *variant.compiled, regions, runsSynced, pending, sampling)) return EntryOutcome::Differing;
+    std::erase_if(vouched, [&](const VouchedRegions& range) { return pending.Overlaps(range.low, static_cast<std::size_t>(range.high - range.low)); });
+    thread_local std::vector<std::pair<std::uint64_t, std::uint64_t>>* checkedSlot = nullptr;
+    auto& checked = ShaderRecompiler::ThreadOwned(checkedSlot);
+    checked.clear();
+    if (!vouched.empty()) {
+        for (std::size_t index = 0, range = 0; index < variant.runs.size(); ++index) {
+            if (range < vouched.size() && index == vouched[range].begin) {
+                checked.emplace_back(vouched[range].low, vouched[range].high);
+                index = vouched[range++].end - 1u;
+                continue;
+            }
+            checked.push_back(variant.runs[index]);
+        }
+    }
+    const std::span<const std::pair<std::uint64_t, std::uint64_t>> runs = vouched.empty() ? std::span<const std::pair<std::uint64_t, std::uint64_t>>(variant.runs) : std::span<const std::pair<std::uint64_t, std::uint64_t>>(checked);
+    if (!syncPendingRuns(program, queue, *variant.compiled, regions, runsSynced, pending, sampling, vouched)) return EntryOutcome::Differing;
     auto publish = Graphics::Recorder::PublishGeneration();
     auto pendingSerial = Graphics::StorageTexture::PendingSerial();
     auto forget = GuestMemory::ForgetSerial();
 
-    if (Graphics::StorageTexture::AnyPendingOverlaps(variant.runs) || Graphics::AnyShadowedOverlaps(variant.runs)) {
+    if (Graphics::StorageTexture::AnyPendingOverlaps(runs) || Graphics::AnyShadowedOverlaps(runs)) {
+        vouched.clear();
         for (const auto& region : regions) {
             if (Graphics::StorageTexture::FlushPending(region.guestAddress, region.bytes.size(), nullptr, "dispatch-cache entry")) ++imagesFlushed;
         }
@@ -102,7 +133,7 @@ EntryOutcome Driver::validateVariant(std::uint64_t program, std::uint32_t queue,
     }
     if ((forget & 1) != 0) return EntryOutcome::ForgetMoved;
     for (const auto& label : deferredLabels().labels) {
-        for (const auto& [begin, end] : variant.runs) {
+        for (const auto& [begin, end] : runs) {
             if (label.address < end && begin < label.address + label.size) return EntryOutcome::QueuedLabel;
         }
     }
@@ -114,7 +145,7 @@ EntryOutcome Driver::validateVariant(std::uint64_t program, std::uint32_t queue,
 
     pending.Load();
     bool unmapped = false;
-    if (!validateCaptured(program, queue, regions, *variant.compiled, true, pending, &unmapped, &sampling, masked ? &mask : nullptr)) return unmapped ? EntryOutcome::Inaccessible : EntryOutcome::Differing;
+    if (!validateCaptured(program, queue, regions, *variant.compiled, true, pending, &unmapped, &sampling, masked ? &mask : nullptr, vouched)) return unmapped ? EntryOutcome::Inaccessible : EntryOutcome::Differing;
     if (Graphics::Recorder::PublishGeneration() != publish) return EntryOutcome::PublishMoved;
     if (Graphics::StorageTexture::PendingSerial() != pendingSerial) return EntryOutcome::PendingMoved;
     if (GuestMemory::ForgetSerial() != forget) return EntryOutcome::ForgetMoved;
diff --git a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Draw/Draw.cpp b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Draw/Draw.cpp
index c471c28a0..4e4d6ffea 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Draw/Draw.cpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Draw/Draw.cpp
@@ -198,7 +198,7 @@ DrawVerdict Driver::draw(QueueState& queue, std::span<const std::uint32_t> packe
         for (std::size_t i = 0; i < programs.size(); ++i) decodeVertexInfo(i);
         phaseTiming.Phase(DrawRowDecode);
     }
-    ShaderMemory shaderMemory(memory, &queryPendingWrite, &observePendingWrite, hookWaitCounter());
+    ShaderMemory shaderMemory(memory, &queryPendingWrite, &observePendingWrite, hookWaitCounter(), &QueryPendingSpan);
     std::vector<ShaderRecompiler::RecompileResult> results;
     std::vector<std::shared_ptr<const ShaderRecompiler::RecompileResult>> compiledStages;
     compiledStages.reserve(programs.size());
diff --git a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Draw/DrawCache.cpp b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Draw/DrawCache.cpp
index e7b4e6638..0ada9bb06 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Draw/DrawCache.cpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Draw/DrawCache.cpp
@@ -125,7 +125,7 @@ void Driver::insertDrawEntry(std::uint64_t key, std::vector<std::shared_ptr<Disp
     for (std::size_t i = 0; i < fresh.size(); ++i) {
         if (fresh[i] == nullptr) continue;
         auto& variants = replacement->stages[i];
-        const auto present = std::find_if(variants.begin(), variants.end(), [&](const std::shared_ptr<DispatchVariant>& kept) { return kept->pushOffset == fresh[i]->pushOffset && kept->runs == fresh[i]->runs && kept->words == fresh[i]->words; });
+        const auto present = std::find_if(variants.begin(), variants.end(), [&](const std::shared_ptr<DispatchVariant>& kept) { return kept->pushOffset == fresh[i]->pushOffset && kept->runs == fresh[i]->runs && SameCapturedWords(kept->words, kept->shared, fresh[i]->words, fresh[i]->shared); });
         if (present != variants.end()) {
             ++counters.present;
             fresh[i] = *present;
diff --git a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Draw/DrawCapture.cpp b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Draw/DrawCapture.cpp
index 5c049b74a..c8e781544 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Draw/DrawCapture.cpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Draw/DrawCapture.cpp
@@ -55,6 +55,7 @@ std::shared_ptr<const ShaderRecompiler::RecompileResult> Driver::materializeDraw
     }();
 
     stageCapture.regions = shaderMemory.TakeRecentRegions();
+    stageCapture.supplied = shaderMemory.SuppliedBlocks();
     recompiled[i] = true;
     memory = shaderMemory.Regions();
 
@@ -156,12 +157,17 @@ void Driver::cacheDrawStages(bool useDrawEntries, bool drawHit, const Pm4::DrawP
             std::vector<ShaderRecompiler::MemoryRegion> regions(stageCapture.regions.begin(), stageCapture.regions.end());
             for (const auto& read : decodeReads[i]) regions.push_back({read.address, std::as_bytes(std::span(read.bytes))});
             std::stable_sort(regions.begin(), regions.end(), [](const ShaderRecompiler::MemoryRegion& a, const ShaderRecompiler::MemoryRegion& b) { return a.guestAddress < b.guestAddress; });
+            std::size_t capturedWords = 0;
+            for (const auto& region : regions) capturedWords += region.bytes.size() / sizeof(std::uint32_t);
+            variant->runs.reserve(regions.size());
+            variant->words.reserve(capturedWords);
             for (const auto& region : regions) {
                 variant->runs.emplace_back(region.guestAddress, region.guestAddress + region.bytes.size());
                 const auto count = region.bytes.size() / sizeof(std::uint32_t);
                 const auto offset = variant->words.size();
                 variant->words.resize(offset + count);
                 std::memcpy(variant->words.data() + offset, region.bytes.data(), count * sizeof(std::uint32_t));
+                AppendSharedWords(variant->shared, variant->runs.size() - 1u, offset, region.bytes, stageCapture.supplied);
             }
             if (drawStageDataHits() && stageCapture.capture != nullptr && !stageCapture.capture->readTrace.leaves.empty()) recordStageDataPositions(*variant, *stageCapture.capture, decodeReads[i]);
             if (traceDrawCache() && entry != nullptr && matched[i] == nullptr && i < entry->stages.size()) {
diff --git a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Memory/CaptureValidation.cpp b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Memory/CaptureValidation.cpp
index 2eb194129..46d3c6604 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Memory/CaptureValidation.cpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Memory/CaptureValidation.cpp
@@ -57,13 +57,23 @@ bool Driver::captureStable(std::span<const ShaderRecompiler::MemoryRegion> captu
     return stable;
 }
 
-bool Driver::validateCaptured(std::uint64_t program, std::uint32_t queue, std::span<const ShaderRecompiler::MemoryRegion> captured, const ShaderRecompiler::RecompileResult& compiled, bool inPlace, const PendingView& view, bool* unmapped, std::optional<SampledReadScope>* sampling, const DataMask* data) {
+bool Driver::validateCaptured(std::uint64_t program, std::uint32_t queue, std::span<const ShaderRecompiler::MemoryRegion> captured, const ShaderRecompiler::RecompileResult& compiled, bool inPlace, const PendingView& view, bool* unmapped, std::optional<SampledReadScope>* sampling, const DataMask* data, std::span<const VouchedRegions> vouched) {
     using Policy = ShaderMemory::PendingWrite;
 
     thread_local std::vector<std::size_t>* pendingSlot = nullptr;
     auto& pending = ShaderRecompiler::ThreadOwned(pendingSlot);
     pending.clear();
-    for (std::size_t i = 0; i < captured.size(); ++i) {
+    thread_local std::vector<VouchedRegions>* skippedSlot = nullptr;
+    auto& skipped = ShaderRecompiler::ThreadOwned(skippedSlot);
+    skipped.clear();
+    for (const auto& range : vouched) {
+        if (range.begin < range.end && range.end <= captured.size() && (skipped.empty() || skipped.back().end <= range.begin) && !view.Overlaps(range.low, static_cast<std::size_t>(range.high - range.low))) skipped.push_back(range);
+    }
+    for (std::size_t i = 0, range = 0; i < captured.size(); ++i) {
+        if (range < skipped.size() && i == skipped[range].begin) {
+            i = skipped[range++].end - 1u;
+            continue;
+        }
         const auto& region = captured[i];
         if (view.Overlaps(region.guestAddress, region.bytes.size())) pending.push_back(i);
     }
@@ -107,7 +117,12 @@ bool Driver::validateCaptured(std::uint64_t program, std::uint32_t queue, std::s
         observable.clear();
         reachedPending = 0;
         if (data != nullptr) data->live->clear();
-        for (std::size_t i = 0; i < captured.size(); ++i) {
+        for (std::size_t i = 0, range = 0; i < captured.size(); ++i) {
+            if (range < skipped.size() && i == skipped[range].begin) {
+                first += skipped[range].words;
+                i = skipped[range++].end - 1u;
+                continue;
+            }
             const auto& region = captured[i];
             const bool isPending = next < pending.size() && pending[next] == i;
             if (isPending) ++next;
diff --git a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Memory/WriteEvidence.cpp b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Memory/WriteEvidence.cpp
index fa2ca447d..596daf014 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Memory/WriteEvidence.cpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/src/Driver/Memory/WriteEvidence.cpp
@@ -215,4 +215,15 @@ ShaderMemory::PendingWrite Driver::queryPendingWrite(std::uint64_t address, std:
     return Get().classifyPendingWrite(address, bytes, reason, pending, known);
 }
 
+ShaderMemory::PendingWrite Driver::QueryPendingSpan(std::uint64_t address, std::size_t bytes, std::span<std::byte> known) {
+    if (DrawPipeline::Active()) {
+        auto& pipeline = DrawPipeline::Queue0();
+        if (pipeline.Busy() && pipeline.Overlaps(address, bytes)) return ShaderMemory::PendingWrite::Sync;
+    }
+    std::uint64_t ValidateCounters::*reason = nullptr;
+    PendingView pending;
+    pending.Load();
+    return Get().classifyPendingWrite(address, bytes, reason, pending, known);
+}
+
 }
diff --git a/core/libs/prx/libSceAgcDriver/Execution/src/GuestMemory.cpp b/core/libs/prx/libSceAgcDriver/Execution/src/GuestMemory.cpp
index ec3977df8..a3f209c87 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/src/GuestMemory.cpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/src/GuestMemory.cpp
@@ -904,6 +904,23 @@ struct WriteTracker {
         }
     }
 
+    void stampWatchedPart(std::uint64_t address, std::uint64_t end, std::uint32_t stampGeneration) {
+#ifdef _WIN32
+        address = std::max<std::uint64_t>(address, base);
+        end = std::min<std::uint64_t>(end, base + size);
+#else
+        end = std::min<std::uint64_t>(end, LeafCount * LeafBlocks * WriteBlockBytes);
+#endif
+        if (address >= end) return;
+        for (auto block = blockOf(address); block <= blockOf(end - 1); ++block) {
+            const auto first = std::max(address, blockBegin(block));
+            const auto stop = std::min<std::uint64_t>(end, blockBegin(block) + WriteBlockBytes);
+            if (!covers(first, 1) && !covers(stop - 1, 1)) continue;
+            stamp(block, stampGeneration, StampKind::Driver);
+            noteDriverStore(block, address, end, stampGeneration);
+        }
+    }
+
     void noteDriverStore(std::uint64_t block, std::uint64_t address, std::uint64_t end, std::uint32_t stampGeneration) {
         const auto begin = blockBegin(block);
         const auto from = static_cast<std::uint32_t>(std::max(address, begin) - begin);
@@ -1401,6 +1418,7 @@ std::uint64_t storeOwn(std::uint64_t address, std::size_t bytes, const std::func
         ++tracker.generation;
         if (!tracker.watched || !tracker.covers(stored.first, static_cast<std::size_t>(stored.second - stored.first))) {
             tracker.noteUnstampedStore(stored.first, stored.second);
+            if (tracker.watched) tracker.stampWatchedPart(stored.first, stored.second, tracker.generation);
             return 0;
         }
         for (auto block = tracker.blockOf(stored.first); block <= tracker.blockOf(stored.second - 1); ++block) {
@@ -1446,6 +1464,7 @@ std::uint64_t MarkWritten(std::uint64_t address, std::size_t bytes) {
     ++tracker.generation;
     if (!tracker.watched || !tracker.covers(address, bytes)) {
         tracker.noteUnstampedStore(address, address + bytes);
+        if (tracker.watched) tracker.stampWatchedPart(address, address + bytes, tracker.generation);
         return 0;
     }
     {
diff --git a/core/libs/prx/libSceAgcDriver/Execution/src/ShaderMemory.cpp b/core/libs/prx/libSceAgcDriver/Execution/src/ShaderMemory.cpp
index 02884484e..f22072e2d 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/src/ShaderMemory.cpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/src/ShaderMemory.cpp
@@ -6,6 +6,7 @@
 #include "Optimization/RequestMemoryView.hpp"
 #include "Optimization/ResourceMaterializer.hpp"
 #include "Optimization/ResourceProgram.hpp"
+#include "ThreadOwned.hpp"
 #include <algorithm>
 #include <atomic>
 #include <bit>
@@ -55,6 +56,13 @@ struct CaptureProfile {
     // The driver's per-shader source handle memo: captures served from it, and resolves it made.
     std::atomic<std::uint64_t> handleHits{0};
     std::atomic<std::uint64_t> handleMisses{0};
+    std::atomic<std::uint64_t> suppliedShared{0};
+    std::atomic<std::uint64_t> suppliedCopied{0};
+    std::atomic<std::uint64_t> suppliedSpilled{0};
+    std::atomic<std::uint64_t> vouchCalls{0};
+    std::atomic<std::uint64_t> vouchedBlocks{0};
+    std::atomic<std::uint64_t> refusedBlocks{0};
+    std::atomic<std::uint64_t> vouchedWords{0};
 };
 
 constexpr std::uint64_t WordWaitNanoseconds = 50000;
@@ -85,6 +93,28 @@ double WaitedMs() {
     return provider != nullptr ? provider() : 0.0;
 }
 
+using GuestRuns = std::vector<std::pair<std::uint64_t, std::uint32_t>>;
+
+std::uint64_t RunEnd(const std::pair<std::uint64_t, std::uint32_t>& run) {
+    return run.first + static_cast<std::uint64_t>(run.second) * sizeof(std::uint32_t);
+}
+
+bool RunsTouch(const GuestRuns& runs, std::uint64_t begin, std::uint64_t end) {
+    const auto after = std::upper_bound(runs.begin(), runs.end(), begin, [](std::uint64_t address, const std::pair<std::uint64_t, std::uint32_t>& run) { return address < run.first; });
+    if (after != runs.begin() && RunEnd(*std::prev(after)) > begin) return true;
+    return after != runs.end() && after->first < end;
+}
+
+bool RunsOverlap(const GuestRuns& left, const GuestRuns& right) {
+    std::size_t a = 0;
+    std::size_t b = 0;
+    while (a < left.size() && b < right.size()) {
+        if (left[a].first < RunEnd(right[b]) && right[b].first < RunEnd(left[a])) return true;
+        if (RunEnd(left[a]) <= RunEnd(right[b])) ++a;
+        else ++b;
+    }
+    return false;
+}
 template<typename TPage>
 std::vector<std::unique_ptr<TPage>>& pagePool() {
     struct PagePoolStorage {};
@@ -99,7 +129,7 @@ void ShaderMemory::SetWaitedMsProvider(WaitedMsProvider provider) {
     waitedMsProvider.store(provider, std::memory_order_release);
 }
 
-ShaderMemory::ShaderMemory(std::span<const ShaderRecompiler::MemoryRegion> regions, PendingWriteQuery pendingWrite, PendingWriteObserver observe, HookWaitCounter hookWaits) : pendingWrite(pendingWrite), observe(observe), hookWaits(hookWaits) {
+ShaderMemory::ShaderMemory(std::span<const ShaderRecompiler::MemoryRegion> regions, PendingWriteQuery pendingWrite, PendingWriteObserver observe, HookWaitCounter hookWaits, PendingWriteQuery pendingSpan) : pendingWrite(pendingWrite), observe(observe), hookWaits(hookWaits), pendingSpan(pendingSpan != nullptr ? pendingSpan : pendingWrite) {
     const ShaderRecompiler::RequestMemoryView validated(regions);
     for (const auto& region : regions) {
         initial.emplace(region.guestAddress, region.bytes);
@@ -211,6 +241,7 @@ bool ShaderMemory::read(void* context, std::uint64_t address, std::uint32_t* val
         }
         if (next != self.initial.end() && next->first - address < sizeof(*value)) throw std::runtime_error("AGC driver: shader memory read overlaps a snapshot boundary");
     }
+    if (!self.supplied.empty()) self.spillTouching(address, address + sizeof(*value));
     auto& page = self.page(address & ~static_cast<std::uint64_t>(PageBytes - 1));
     const auto index = static_cast<std::size_t>((address % PageBytes) / sizeof(*value));
     if (!page.valid.Test(index) && page.lazy) {
@@ -292,6 +323,7 @@ bool ShaderMemory::overlapsInitial(std::uint64_t begin, std::uint64_t end) const
 
 bool ShaderMemory::readWords(void* context, std::uint64_t address, std::uint32_t* values, std::uint64_t count) {
     auto& self = *static_cast<ShaderMemory*>(context);
+    if (!self.supplied.empty() && count != 0) self.spillTouching(address, count > (std::numeric_limits<std::uint64_t>::max() - address) / sizeof(std::uint32_t) ? std::numeric_limits<std::uint64_t>::max() : address + count * sizeof(std::uint32_t));
     while (count != 0) {
         const auto base = address & ~static_cast<std::uint64_t>(PageBytes - 1);
         const auto index = static_cast<std::size_t>((address % PageBytes) / sizeof(std::uint32_t));
@@ -360,47 +392,54 @@ ShaderRecompiler::SrtWriteStamp ShaderMemory::stampWrites(void* context, std::ui
     if (bytes == 0 || address > std::numeric_limits<std::uint64_t>::max() - bytes || self.overlapsInitial(address, address + bytes)) return {};
     const auto fetched = std::lower_bound(self.pages.begin(), self.pages.end(), address & ~static_cast<std::uint64_t>(PageBytes - 1), [](const std::pair<std::uint64_t, Page*>& entry, std::uint64_t base) { return entry.first < base; });
     if (fetched != self.pages.end() && fetched->first < address + bytes) return {};
-    if (self.pendingWrite != nullptr && self.pendingWrite(address, static_cast<std::size_t>(bytes), {}) != PendingWrite::None) return {};
     GuestMemory::FlushGpuWrites(address, static_cast<std::size_t>(bytes));
     const auto layout = GuestAllocations::GuestAllocationsGeneration_nid_postfix();
     const auto generation = GuestMemory::CollectWrites(address, static_cast<std::size_t>(bytes));
     if (generation == 0) return {};
+    if (self.pendingSpan != nullptr && self.pendingSpan(address, static_cast<std::size_t>(bytes), {}) != PendingWrite::None) return {};
     return {generation, layout};
 }
 
-bool ShaderMemory::unchangedSince(void* context, std::uint64_t address, std::uint64_t bytes, const ShaderRecompiler::SrtWriteStamp& stamp) {
-    const auto& self = *static_cast<const ShaderMemory*>(context);
+bool ShaderMemory::Vouched(PendingWriteQuery pending, std::uint64_t address, std::uint64_t bytes, const ShaderRecompiler::SrtWriteStamp& stamp) {
+    CaptureTotals().vouchCalls.fetch_add(1, std::memory_order_relaxed);
     if (stamp.generation == 0 || bytes == 0 || address > std::numeric_limits<std::uint64_t>::max() - bytes || stamp.layout != GuestAllocations::GuestAllocationsGeneration_nid_postfix()) return false;
-    if (self.pendingWrite != nullptr && self.pendingWrite(address, static_cast<std::size_t>(bytes), {}) != PendingWrite::None) return false;
+    if (pending != nullptr && pending(address, static_cast<std::size_t>(bytes), {}) != PendingWrite::None) return false;
     GuestMemory::FlushGpuWrites(address, static_cast<std::size_t>(bytes));
     if (GuestMemory::CollectWrites(address, static_cast<std::size_t>(bytes)) == 0) return false;
     return GuestMemory::UnchangedSince(address, static_cast<std::size_t>(bytes), stamp.generation);
 }
 
-bool ShaderMemory::supplyWords(void* context, std::uint64_t address, const std::uint32_t* values, std::uint64_t count) {
-    auto& self = *static_cast<ShaderMemory*>(context);
-    const auto bytes = count * sizeof(std::uint32_t);
-    if (count == 0 || address % sizeof(std::uint32_t) != 0 || count > std::numeric_limits<std::uint64_t>::max() / sizeof(std::uint32_t) || address > std::numeric_limits<std::uint64_t>::max() - bytes || self.overlapsInitial(address, address + bytes)) return false;
+bool ShaderMemory::unchangedSince(void* context, std::uint64_t address, std::uint64_t bytes, const ShaderRecompiler::SrtWriteStamp& stamp) {
+    return Vouched(static_cast<const ShaderMemory*>(context)->pendingSpan, address, bytes, stamp);
+}
+
+bool ShaderMemory::place(std::uint64_t address, const std::uint32_t* values, std::uint64_t count, bool overwrite, bool recent) {
     while (count != 0) {
         const auto base = address & ~static_cast<std::uint64_t>(PageBytes - 1);
         const auto index = static_cast<std::size_t>((address % PageBytes) / sizeof(std::uint32_t));
         const auto words = static_cast<std::size_t>(std::min<std::uint64_t>(count, PageWords - index));
-        const auto found = std::lower_bound(self.pages.begin(), self.pages.end(), base, [](const std::pair<std::uint64_t, Page*>& entry, std::uint64_t value) { return entry.first < value; });
-        Page* existing = found != self.pages.end() && found->first == base ? found->second : nullptr;
-        if (existing == nullptr && !GuestMemory::Accessible(reinterpret_cast<const void*>(base), PageBytes)) return false;
-        auto& page = existing != nullptr ? *existing : self.insertPage(found, base);
-        if (page.wordwise) return false;
-        if (page.valid.None()) {
+        const auto found = std::lower_bound(pages.begin(), pages.end(), base, [](const std::pair<std::uint64_t, Page*>& entry, std::uint64_t value) { return entry.first < value; });
+        Page* existing = found != pages.end() && found->first == base ? found->second : nullptr;
+        if (existing == nullptr && !overwrite && !GuestMemory::Accessible(reinterpret_cast<const void*>(base), PageBytes)) return false;
+        auto& page = existing != nullptr ? *existing : insertPage(found, base);
+        if (page.wordwise && !overwrite) return false;
+        const auto head = index / BlockWords * BlockWords;
+        const auto tail = (index + words + BlockWords - 1) / BlockWords * BlockWords;
+        const bool headOpen = page.lazy && !page.valid.Test(head);
+        const bool tailOpen = page.lazy && !page.valid.Test(tail - 1);
+        if (overwrite || page.valid.None()) {
             std::memcpy(page.words.data() + index, values, words * sizeof(std::uint32_t));
         } else {
             for (std::size_t word = 0; word < words; ++word) {
                 if (!page.valid.Test(index + word)) page.words[index + word] = values[word];
             }
         }
+        if (headOpen && head < index) std::memcpy(&page.words[head], reinterpret_cast<const void*>(base + head * sizeof(std::uint32_t)), (index - head) * sizeof(std::uint32_t));
+        if (tailOpen && index + words < tail) std::memcpy(&page.words[index + words], reinterpret_cast<const void*>(base + (index + words) * sizeof(std::uint32_t)), (tail - index - words) * sizeof(std::uint32_t));
+        for (auto word = page.lazy ? head : index; word < (page.lazy ? tail : index + words); ++word) page.valid.Set(word);
         for (auto word = index; word < index + words; ++word) {
-            page.valid.Set(word);
             page.read.Set(word);
-            page.recent.Set(word);
+            if (recent) page.recent.Set(word);
         }
         address += static_cast<std::uint64_t>(words) * sizeof(std::uint32_t);
         values += words;
@@ -409,6 +448,87 @@ bool ShaderMemory::supplyWords(void* context, std::uint64_t address, const std::
     return true;
 }
 
+void ShaderMemory::spill(std::size_t index) {
+    auto entry = std::move(supplied[index]);
+    supplied.erase(supplied.begin() + static_cast<std::ptrdiff_t>(index));
+    std::size_t at = 0;
+    for (const auto& [address, count] : entry.words->runs) {
+        static_cast<void>(place(address, entry.words->words.data() + at, count, true, entry.recent));
+        at += count;
+    }
+    CaptureTotals().suppliedSpilled += entry.words->words.size();
+    spilled.push_back(std::move(entry.words));
+}
+
+void ShaderMemory::spillTouching(std::uint64_t begin, std::uint64_t end) {
+    for (std::size_t index = supplied.size(); index-- > 0;) {
+        const auto& entry = supplied[index];
+        if (entry.begin < end && begin < entry.end && RunsTouch(entry.words->runs, begin, end)) spill(index);
+    }
+}
+
+bool ShaderMemory::supplyWords(void* context, const std::shared_ptr<const ShaderRecompiler::SrtGuestWords>& words) {
+    auto& self = *static_cast<ShaderMemory*>(context);
+    if (words == nullptr || words->runs.empty()) return false;
+    std::uint64_t total = 0;
+    std::uint64_t end = 0;
+    if (words->ordered && words->low == words->runs.front().first && words->low < words->high) {
+        total = words->words.size();
+        end = words->high;
+    } else {
+        for (const auto& [address, count] : words->runs) {
+            const auto bytes = static_cast<std::uint64_t>(count) * sizeof(std::uint32_t);
+            if (count == 0 || address % sizeof(std::uint32_t) != 0 || address < end || address > std::numeric_limits<std::uint64_t>::max() - bytes) return false;
+            end = address + bytes;
+            total += count;
+        }
+    }
+    const auto begin = words->runs.front().first;
+    if (total != words->words.size() || self.overlapsInitial(begin, end)) return false;
+    auto& totals = CaptureTotals();
+    for (auto& entry : self.supplied) {
+        if (entry.words != words) continue;
+        entry.recent = true;
+        totals.suppliedShared += total;
+        return true;
+    }
+    const auto fetched = std::lower_bound(self.pages.begin(), self.pages.end(), begin & ~static_cast<std::uint64_t>(PageBytes - 1), [](const std::pair<std::uint64_t, Page*>& entry, std::uint64_t base) { return entry.first < base; });
+    const bool paged = fetched != self.pages.end() && fetched->first < end;
+    const bool overlapping = std::any_of(self.supplied.begin(), self.supplied.end(), [&](const Supplied& entry) { return entry.begin < end && begin < entry.end && RunsOverlap(entry.words->runs, words->runs); });
+    if (paged || overlapping) {
+        for (std::size_t index = self.supplied.size(); index-- > 0;) {
+            const auto& entry = self.supplied[index];
+            if (entry.begin < end && begin < entry.end && RunsOverlap(entry.words->runs, words->runs)) self.spill(index);
+        }
+        std::size_t at = 0;
+        for (const auto& [address, count] : words->runs) {
+            if (!self.place(address, words->words.data() + at, count, false, true)) return false;
+            at += count;
+        }
+        totals.suppliedCopied += total;
+        return true;
+    }
+    const auto spanBegin = begin & ~static_cast<std::uint64_t>(PageBytes - 1);
+    const auto spanEnd = (end + PageBytes - 1) & ~static_cast<std::uint64_t>(PageBytes - 1);
+    if (!GuestMemory::Accessible(reinterpret_cast<const void*>(spanBegin), static_cast<std::size_t>(spanEnd - spanBegin))) {
+        std::uint64_t first = std::numeric_limits<std::uint64_t>::max();
+        std::uint64_t past = 0;
+        for (const auto& [address, count] : words->runs) {
+            const auto from = address & ~static_cast<std::uint64_t>(PageBytes - 1);
+            const auto to = (address + static_cast<std::uint64_t>(count) * sizeof(std::uint32_t) + PageBytes - 1) & ~static_cast<std::uint64_t>(PageBytes - 1);
+            if (from > past || first == std::numeric_limits<std::uint64_t>::max()) {
+                if (first != std::numeric_limits<std::uint64_t>::max() && !GuestMemory::Accessible(reinterpret_cast<const void*>(first), static_cast<std::size_t>(past - first))) return false;
+                first = from;
+            }
+            past = std::max(past, to);
+        }
+        if (!GuestMemory::Accessible(reinterpret_cast<const void*>(first), static_cast<std::size_t>(past - first))) return false;
+    }
+    self.supplied.push_back({words, begin, end, true});
+    totals.suppliedShared += total;
+    return true;
+}
+
 ShaderMemory::KnownValueCounts ShaderMemory::KnownValues() {
     auto& totals = CaptureTotals();
     return {totals.wordsKnown.load(std::memory_order_relaxed), totals.wordsKnownVerified.load(std::memory_order_relaxed), totals.wordsKnownMismatches.load(std::memory_order_relaxed)};
@@ -419,6 +539,24 @@ void ShaderMemory::CountHandleMemo(bool hit) {
     (hit ? CaptureTotals().handleHits : CaptureTotals().handleMisses).fetch_add(1, std::memory_order_relaxed);
 }
 
+ShaderMemory::VouchCounts ShaderMemory::Vouches() {
+    auto& totals = CaptureTotals();
+    return {totals.vouchCalls.load(std::memory_order_relaxed), totals.vouchedBlocks.load(std::memory_order_relaxed), totals.refusedBlocks.load(std::memory_order_relaxed), totals.vouchedWords.load(std::memory_order_relaxed)};
+}
+
+ShaderMemory::SupplyCounts ShaderMemory::Supplies() {
+    auto& totals = CaptureTotals();
+    return {totals.suppliedShared.load(std::memory_order_relaxed), totals.suppliedCopied.load(std::memory_order_relaxed), totals.suppliedSpilled.load(std::memory_order_relaxed)};
+}
+
+std::vector<std::shared_ptr<const ShaderRecompiler::SrtGuestWords>> ShaderMemory::SuppliedBlocks() const {
+    std::vector<std::shared_ptr<const ShaderRecompiler::SrtGuestWords>> blocks;
+    blocks.reserve(supplied.size() + spilled.size());
+    for (const auto& entry : supplied) blocks.push_back(entry.words);
+    blocks.insert(blocks.end(), spilled.begin(), spilled.end());
+    return blocks;
+}
+
 std::shared_ptr<const ShaderRecompiler::ResourceCapture> ShaderMemory::Capture(const ShaderRecompiler::RecompileRequest& request, const ShaderRecompiler::SourceHandle* handle) {
     return capture(request, handle, nullptr);
 }
@@ -475,34 +613,159 @@ std::shared_ptr<const ShaderRecompiler::ResourceCapture> ShaderMemory::capture(c
     return capture;
 }
 
-std::vector<ShaderRecompiler::MemoryRegion> ShaderMemory::Regions() const {
+std::vector<ShaderRecompiler::MemoryRegion> ShaderMemory::collect(bool recentOnly) const {
+    struct Cursor {
+        const ShaderRecompiler::SrtGuestWords* words;
+        std::size_t run;
+        std::size_t at;
+    };
+    std::vector<Cursor> cursors;
+    std::size_t runs = 0;
+    for (const auto& entry : supplied) {
+        if (recentOnly && !entry.recent) continue;
+        cursors.push_back({entry.words.get(), 0u, 0u});
+        runs += entry.words->runs.size() + entry.words->words.size() / PageWords;
+    }
     std::vector<ShaderRecompiler::MemoryRegion> result;
-    result.reserve(initial.size() + pages.size());
-    auto next = initial.begin();
-    // Both maps are ordered by address and never overlap, so a merge keeps the result sorted.
+    result.reserve((recentOnly ? 0u : initial.size()) + pages.size() + runs);
+    auto next = recentOnly ? initial.end() : initial.begin();
+    const auto emitBefore = [&](std::uint64_t address) {
+        while (true) {
+            Cursor* lowest = nullptr;
+            for (auto& cursor : cursors) {
+                if (cursor.run < cursor.words->runs.size() && (lowest == nullptr || cursor.words->runs[cursor.run].first < lowest->words->runs[lowest->run].first)) lowest = &cursor;
+            }
+            const auto run = lowest != nullptr ? lowest->words->runs[lowest->run].first : std::numeric_limits<std::uint64_t>::max();
+            if (next != initial.end() && next->first < address && next->first < run) {
+                result.push_back({next->first, next->second});
+                ++next;
+            } else if (lowest != nullptr && run < address) {
+                const auto count = lowest->words->runs[lowest->run].second;
+                const auto words = std::span(lowest->words->words).subspan(lowest->at, count);
+                for (std::size_t done = 0; done < count;) {
+                    const auto at = run + done * sizeof(std::uint32_t);
+                    const auto piece = std::min<std::size_t>(count - done, (PageBytes - static_cast<std::size_t>(at % PageBytes)) / sizeof(std::uint32_t));
+                    result.push_back({at, std::as_bytes(words.subspan(done, piece))});
+                    done += piece;
+                }
+                lowest->at += count;
+                ++lowest->run;
+            } else {
+                return;
+            }
+        }
+    };
     for (const auto& [base, pagePointer] : pages) {
         const auto& page = *pagePointer;
-        while (next != initial.end() && next->first < base) {
-            result.push_back({next->first, next->second});
-            ++next;
-        }
-        page.read.ForEachRun([&](std::size_t first, std::size_t end) { result.push_back({base + first * sizeof(std::uint32_t), std::as_bytes(std::span(page.words).subspan(first, end - first))}); });
+        const auto& marks = recentOnly ? page.recent : page.read;
+        if (marks.None()) continue;
+        marks.ForEachRun([&](std::size_t first, std::size_t end) {
+            const auto address = base + first * sizeof(std::uint32_t);
+            emitBefore(address);
+            result.push_back({address, std::as_bytes(std::span(page.words).subspan(first, end - first))});
+        });
     }
-    for (; next != initial.end(); ++next) result.push_back({next->first, next->second});
+    emitBefore(std::numeric_limits<std::uint64_t>::max());
     return result;
 }
 
+std::vector<ShaderRecompiler::MemoryRegion> ShaderMemory::Regions() const {
+    return collect(false);
+}
+
 std::vector<ShaderRecompiler::MemoryRegion> ShaderMemory::TakeRecentRegions() {
-    std::vector<ShaderRecompiler::MemoryRegion> result;
-    for (auto& [base, pagePointer] : pages) {
-        auto& page = *pagePointer;
-        if (page.recent.None()) continue;
-        page.recent.ForEachRun([&](std::size_t first, std::size_t end) { result.push_back({base + first * sizeof(std::uint32_t), std::as_bytes(std::span(page.words).subspan(first, end - first))}); });
-        page.recent.Reset();
-    }
+    auto result = collect(true);
+    for (auto& [base, page] : pages) page->recent.Reset();
+    for (auto& entry : supplied) entry.recent = false;
     return result;
 }
 
+void AppendSharedWords(std::vector<SharedWords>& shared, std::size_t region, std::size_t first, std::span<const std::byte> bytes, std::span<const std::shared_ptr<const ShaderRecompiler::SrtGuestWords>> blocks) {
+    const auto address = reinterpret_cast<std::uintptr_t>(bytes.data());
+    const auto count = bytes.size() / sizeof(std::uint32_t);
+    if (count == 0) return;
+    for (const auto& block : blocks) {
+        if (block == nullptr || block->words.empty()) continue;
+        const auto begin = reinterpret_cast<std::uintptr_t>(block->words.data());
+        const auto end = begin + block->words.size() * sizeof(std::uint32_t);
+        if (address < begin || address >= end || end - address < count * sizeof(std::uint32_t) || (address - begin) % sizeof(std::uint32_t) != 0) continue;
+        const auto offset = static_cast<std::size_t>((address - begin) / sizeof(std::uint32_t));
+        if (!shared.empty()) {
+            auto& last = shared.back();
+            if (last.first + last.count == first && last.offset + last.count == offset && last.region + last.regions == region && !last.block.owner_before(block) && !block.owner_before(last.block)) {
+                last.count += count;
+                ++last.regions;
+                return;
+            }
+        }
+        shared.push_back({first, count, offset, block, region, 1u});
+        return;
+    }
+}
+
+bool SameCapturedWords(std::span<const std::uint32_t> left, std::span<const SharedWords> leftShared, std::span<const std::uint32_t> right, std::span<const SharedWords> rightShared, std::size_t* compared) {
+    if (compared != nullptr) *compared = 0;
+    if (left.size() != right.size()) return false;
+    std::size_t checked = 0;
+    const auto equal = [&](std::size_t from, std::size_t to) {
+        checked += to - from;
+        return std::equal(left.begin() + static_cast<std::ptrdiff_t>(from), left.begin() + static_cast<std::ptrdiff_t>(to), right.begin() + static_cast<std::ptrdiff_t>(from));
+    };
+    std::size_t at = 0;
+    std::size_t next = 0;
+    bool same = true;
+    for (const auto& run : leftShared) {
+        while (next < rightShared.size() && rightShared[next].first < run.first) ++next;
+        if (next == rightShared.size()) break;
+        const auto& other = rightShared[next];
+        const bool placed = other.first == run.first && other.count == run.count && other.offset == run.offset && run.first >= at && run.first <= left.size() && run.count <= left.size() - run.first;
+        if (!placed || run.block.expired() || run.block.owner_before(other.block) || other.block.owner_before(run.block)) continue;
+        if (!equal(at, run.first)) {
+            same = false;
+            break;
+        }
+        at = run.first + run.count;
+    }
+    same = same && equal(at, left.size());
+    if (compared != nullptr) *compared = checked;
+    return same;
+}
+
+std::size_t VouchSharedRegions(std::span<const SharedWords> shared, std::size_t regions, ShaderMemory::PendingWriteQuery pending, std::vector<VouchedRegions>& vouched) {
+    vouched.clear();
+    if (shared.empty() || regions == 0) return 0;
+    thread_local std::vector<std::pair<const ShaderRecompiler::SrtGuestWords*, bool>>* blocksSlot = nullptr;
+    auto& blocks = ShaderRecompiler::ThreadOwned(blocksSlot);
+    blocks.clear();
+    auto& totals = CaptureTotals();
+    std::size_t words = 0;
+    for (const auto& entry : shared) {
+        if (entry.regions == 0 || entry.region > regions || entry.regions > regions - entry.region || (!vouched.empty() && entry.region < vouched.back().end)) continue;
+        const auto block = entry.block.lock();
+        if (block == nullptr || block->low >= block->high) continue;
+        auto known = std::find_if(blocks.begin(), blocks.end(), [&](const auto& seen) { return seen.first == block.get(); });
+        if (known == blocks.end()) {
+            const bool good = ShaderMemory::Vouched(pending, block->low, block->high - block->low, ShaderRecompiler::LoadStamp(*block));
+            (good ? totals.vouchedBlocks : totals.refusedBlocks).fetch_add(1, std::memory_order_relaxed);
+            known = blocks.insert(blocks.end(), {block.get(), good});
+        }
+        if (!known->second) continue;
+        if (!vouched.empty() && vouched.back().end == entry.region) {
+            auto& last = vouched.back();
+            last.end += entry.regions;
+            last.words += entry.count;
+            last.low = std::min(last.low, block->low);
+            last.high = std::max(last.high, block->high);
+        } else {
+            vouched.push_back({entry.region, entry.region + entry.regions, entry.count, block->low, block->high});
+        }
+        words += entry.count;
+    }
+    blocks.clear();
+    totals.vouchedWords.fetch_add(words, std::memory_order_relaxed);
+    return words;
+}
+
 DataWordPositionCounts DataWordPositions(std::span<const std::pair<std::uint64_t, std::uint64_t>> runs, std::span<const std::pair<std::uint32_t, std::uint64_t>> leaves, std::span<const std::uint64_t> otherReads, std::span<const std::uint32_t> words, std::span<const std::uint32_t> flattenedSrt, std::vector<std::uint32_t>& positions, std::vector<std::uint32_t>& slots) {
     DataWordPositionCounts counts;
     positions.clear();
diff --git a/core/libs/prx/libSceAgcDriver/Execution/src/VulkanDevice.cpp b/core/libs/prx/libSceAgcDriver/Execution/src/VulkanDevice.cpp
index eb3cf6b4d..c0b8bf1e7 100644
--- a/core/libs/prx/libSceAgcDriver/Execution/src/VulkanDevice.cpp
+++ b/core/libs/prx/libSceAgcDriver/Execution/src/VulkanDevice.cpp
@@ -2007,10 +2007,10 @@ int VulkanDevice::WriteLabelOnGpu(std::uint64_t address, std::span<const std::by
         // a title misbehaves with this path.
         if (drain) return 2;
         recordStore();
-        GuestMemory::MarkWritten(address, bytes.size());
         // Commands() opened the batch the store went into, so the completion is appended to that
         // batch and runs at its reap, after the write-backs of every batch before it.
         recorder.AfterCompletions(address, bytes, stamp, queue, true);
+        GuestMemory::MarkWritten(address, bytes.size());
         return 5;
     }
     recordStore();
@@ -2464,6 +2464,8 @@ VulkanDevice::CopyOutcome VulkanDevice::CopyBuffer(std::uint64_t destination, st
     recorder.EndGpuTiming(classTiming, bytes);
     recorder.MarkCovered(copiedAccess);
     recorder.NotePendingRead(source, bytes, Graphics::Recorder::ReadKind::CopySource);
+    GuestMemory::MarkWritten(destination, bytes);
+    recorder.NotePendingWrite(destination, bytes);
     // The destination's bytes after the transfer are the source's now when nothing recorded,
     // pending or still to land can change the source before the transfer reads it (a CPU store
     // to it in between is a hardware race too): the caller's ring entry carries them, stamped
diff --git a/core/libs/prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp b/core/libs/prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp
index 75499af74..ebb2adfdd 100644
--- a/core/libs/prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp
+++ b/core/libs/prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp
@@ -64,6 +64,13 @@ struct PipelineSetLayouts {
     std::span<const std::uint32_t> fragmentKey;
 };
 
+struct TableDescriptors {
+    std::uint32_t binding = 0;
+    std::vector<std::pair<std::uint32_t, std::uint32_t>> runs;
+    std::vector<VkDescriptorImageInfo> images;
+    std::shared_ptr<const std::vector<std::shared_ptr<Texture>>> textures;
+};
+
 class DescriptorCache {
 public:
     explicit DescriptorCache(const Context& context);
@@ -79,9 +86,13 @@ public:
         VkDescriptorPool pool = VK_NULL_HANDLE;
         std::size_t poolIndex = 0;
         bool updateAfterBind = false;
+        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
+        std::vector<VkDescriptorPoolSize> sizes;
+        std::optional<std::uint32_t> variableCount;
+        std::shared_ptr<const TableDescriptors> table;
     };
     SetAllocation Allocate(VkDescriptorSetLayout layout, std::span<const VkDescriptorPoolSize> sizes, bool updateAfterBind = false, std::optional<std::uint32_t> variableCount = std::nullopt);
-    void Free(const SetAllocation& allocation) noexcept;
+    void Free(const SetAllocation& allocation, std::shared_ptr<const TableDescriptors> table = {}) noexcept;
     SetAllocation AllocateRewritten(VkDescriptorSetLayout layout, std::span<const VkDescriptorPoolSize> sizes, bool updateAfterBind = false);
     void Recycle(VkDescriptorSetLayout layout, const SetAllocation& allocation) noexcept;
     // APS5_PROFILE_DRAW counters: layouts served from the map / created, sets allocated, pools opened.
@@ -91,6 +102,8 @@ public:
         std::uint64_t sets = 0;
         std::uint64_t pools = 0;
         std::uint64_t dedicatedPools = 0;
+        std::uint64_t keptSets = 0;
+        std::uint64_t reusedSets = 0;
     };
     Stats Counters() const;
 
@@ -110,6 +123,7 @@ private:
     std::vector<Pool> updateAfterBindPools;
     std::vector<VkDescriptorPool> dedicated;
     std::unordered_map<VkDescriptorSetLayout, std::vector<SetAllocation>> recycled;
+    std::vector<SetAllocation> kept;
     Stats stats;
 };
 
@@ -120,6 +134,9 @@ using ContentKeyDiff = std::array<bool, static_cast<std::size_t>(ContentKeyCompo
 struct ImageTableReuseCounts {
     std::uint64_t reused = 0;
     std::uint64_t resolved = 0;
+    std::uint64_t replayed = 0;
+    std::uint64_t writesKept = 0;
+    std::uint64_t writesMade = 0;
 };
 ImageTableReuseCounts ImageTableReuses();
 
@@ -507,6 +524,16 @@ private:
     bool splitSets = false;
     std::vector<std::uint32_t> fragmentLayoutKey;
     SetObjects fragment;
+    struct TableWrites {
+        bool recorded = false;
+        bool written = false;
+        std::uint32_t binding = 0;
+        std::size_t firstImage = 0;
+        std::size_t images = 0;
+        std::vector<std::pair<std::uint32_t, std::uint32_t>> runs;
+    };
+    std::array<TableWrites, 2> tableWrites;
+    std::shared_ptr<const TableDescriptors> keptTable(std::size_t set, std::shared_ptr<const std::vector<std::shared_ptr<Texture>>>& held) noexcept;
     std::uint32_t setOf(const Binding& binding) const;
     std::vector<Allocation> allocations;
     std::vector<VkDescriptorBufferInfo> descriptorBuffers;
diff --git a/core/libs/prx/libSceAgcDriver/Graphics/src/ShaderResources.cpp b/core/libs/prx/libSceAgcDriver/Graphics/src/ShaderResources.cpp
index c44d4a88e..c1d2f9e3c 100644
--- a/core/libs/prx/libSceAgcDriver/Graphics/src/ShaderResources.cpp
+++ b/core/libs/prx/libSceAgcDriver/Graphics/src/ShaderResources.cpp
@@ -342,7 +342,10 @@ struct TextureCounters {
     // ShaderResources::refreshOwnObjects).
     std::atomic<std::uint64_t> ownRefreshes{0};
     std::atomic<std::uint64_t> tablesReused{0};
+    std::atomic<std::uint64_t> tablesReplayed{0};
     std::atomic<std::uint64_t> tablesResolved{0};
+    std::atomic<std::uint64_t> tableWritesKept{0};
+    std::atomic<std::uint64_t> tableWritesMade{0};
     // Snapshot hits over memory outside the write watch: kept by an unchanged UntrackedHint key, or
     // by a byte compare.
     std::atomic<std::uint64_t> untrackedKept{0};
@@ -1377,6 +1380,11 @@ bool TableReuseEnabled() {
     return !disabled;
 }
 
+bool TableReplay() {
+    static const bool disabled = std::getenv("APS5_NO_TABLE_REPLAY") != nullptr;
+    return !disabled;
+}
+
 struct TableState {
     std::uint64_t epoch = 0;
     std::uint64_t unwatched = 0;
@@ -1410,6 +1418,10 @@ struct ReusedTable {
     std::vector<ReusedTableElement> elements;
     std::vector<GuestMemory::UnchangedQuery> queries;
     std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
+    std::vector<std::uint32_t> imageSamplers;
+    std::vector<std::uint32_t> samplers;
+    std::vector<std::string> bound;
+    bool replayable = false;
 };
 
 constexpr std::size_t ReusedTableCount = 4;
@@ -1919,6 +1931,7 @@ void ShaderResources::buildComplete() {
             images.reserve(imageCount);
             auto& writes = descriptorWrites;
             writes.reserve(bindings.size());
+            std::vector<std::pair<std::size_t, std::size_t>> keptWrites;
             const auto absentImage = [this](std::uint32_t binding) {
                 constexpr auto bindingCount = static_cast<std::uint32_t>(ShaderRecompiler::RuntimeAbi::Binding::Count);
                 const auto view = context.nullDescriptors ? VK_NULL_HANDLE : context.placeholderImages->View(binding % bindingCount);
@@ -1926,6 +1939,13 @@ void ShaderResources::buildComplete() {
             };
             for (const auto& binding : bindings) {
                 if (binding.flags != 0) {
+                    const auto set = setOf(binding);
+                    const bool sampled = binding.layout.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
+                    const auto& allocation = set == 0 ? cacheAllocation : fragment.cacheAllocation;
+                    const bool recorded = sampled && set < tableWrites.size() && allocation.layout != VK_NULL_HANDLE;
+                    std::vector<std::pair<std::uint32_t, std::uint32_t>> runs;
+                    const auto firstWrite = writes.size();
+                    const auto firstImage = images.size();
                     for (std::size_t first = 0; first < binding.imageAllocations.size();) {
                         if (binding.imageAllocations[first] == std::numeric_limits<std::size_t>::max()) {
                             ++first;
@@ -1934,7 +1954,7 @@ void ShaderResources::buildComplete() {
                         auto last = first;
                         while (last < binding.imageAllocations.size() && binding.imageAllocations[last] != std::numeric_limits<std::size_t>::max()) ++last;
                         VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
-                        write.dstSet = setOf(binding) == 0 ? _set : fragment.set;
+                        write.dstSet = set == 0 ? _set : fragment.set;
                         write.dstBinding = binding.layout.binding;
                         write.dstArrayElement = static_cast<std::uint32_t>(first);
                         write.descriptorCount = static_cast<std::uint32_t>(last - first);
@@ -1945,9 +1965,20 @@ void ShaderResources::buildComplete() {
                             if (binding.layout.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER) images.push_back({samplers[index]->Handle(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED});
                             else images.push_back({VK_NULL_HANDLE, textureFirstLayer[index] ? textures[index]->FirstLayerView() : textures[index]->View(), textures[index]->Layout()});
                         }
+                        if (recorded) runs.emplace_back(write.dstArrayElement, write.descriptorCount);
                         writes.push_back(write);
                         first = last;
                     }
+                    if (!recorded) continue;
+                    const auto& held = allocation.table;
+                    auto& counters = TextureCounts();
+                    if (held != nullptr && held->binding == binding.layout.binding && held->runs == runs && std::equal(held->images.begin(), held->images.end(), images.begin() + static_cast<std::ptrdiff_t>(firstImage), images.end(), [](const VkDescriptorImageInfo& a, const VkDescriptorImageInfo& b) { return a.sampler == b.sampler && a.imageView == b.imageView && a.imageLayout == b.imageLayout; })) {
+                        keptWrites.emplace_back(firstWrite, writes.size());
+                        counters.tableWritesKept.fetch_add(1, std::memory_order_relaxed);
+                    } else {
+                        counters.tableWritesMade.fetch_add(1, std::memory_order_relaxed);
+                    }
+                    tableWrites[set] = {true, false, binding.layout.binding, firstImage, images.size() - firstImage, std::move(runs)};
                     continue;
                 }
                 VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
@@ -1987,7 +2018,22 @@ void ShaderResources::buildComplete() {
                 }
                 writes.push_back(write);
             }
-            context.Resolved(&DeviceFunctions::updateDescriptorSets, "vkUpdateDescriptorSets")(context.device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
+            if (keptWrites.empty()) {
+                context.Resolved(&DeviceFunctions::updateDescriptorSets, "vkUpdateDescriptorSets")(context.device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
+            } else {
+                std::vector<VkWriteDescriptorSet> made;
+                made.reserve(writes.size());
+                std::size_t next = 0;
+                for (std::size_t index = 0; index < writes.size(); ++index) {
+                    if (next < keptWrites.size() && index == keptWrites[next].first) {
+                        index = keptWrites[next++].second - 1u;
+                        continue;
+                    }
+                    made.push_back(writes[index]);
+                }
+                if (!made.empty()) context.Resolved(&DeviceFunctions::updateDescriptorSets, "vkUpdateDescriptorSets")(context.device, static_cast<std::uint32_t>(made.size()), made.data(), 0, nullptr);
+            }
+            for (auto& table : tableWrites) table.written = table.recorded;
         }
         for (const auto& allocation : allocations) {
             if (allocation.adjustment == 0) continue;
@@ -3357,12 +3403,26 @@ ResourceCache& SharedResourceCache() {
     return *cache;
 }
 
-DescriptorCache::DescriptorCache(const Context& context) : context(context), destroyLayout(context.Function<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")), destroyPool(context.Function<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")), resetPool(context.Function<PFN_vkResetDescriptorPool>("vkResetDescriptorPool")) {}
+namespace {
+std::size_t KeptSets() {
+    static const std::size_t count = [] {
+        const char* text = std::getenv("APS5_KEPT_DESCRIPTOR_SETS");
+        return text != nullptr ? static_cast<std::size_t>(std::strtoull(text, nullptr, 10)) : std::size_t{8};
+    }();
+    return count;
+}
+
+}
+
+DescriptorCache::DescriptorCache(const Context& context) : context(context), destroyLayout(context.Function<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")), destroyPool(context.Function<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")), resetPool(context.Function<PFN_vkResetDescriptorPool>("vkResetDescriptorPool")) {
+    kept.reserve(KeptSets() + 1u);
+}
 
 DescriptorCache::~DescriptorCache() {
     for (const auto& pool : pools) destroyPool(context.device, pool.handle, nullptr);
     for (const auto& pool : updateAfterBindPools) destroyPool(context.device, pool.handle, nullptr);
     for (const auto pool : dedicated) destroyPool(context.device, pool, nullptr);
+    for (const auto& set : kept) destroyPool(context.device, set.pool, nullptr);
     for (const auto& [key, layout] : layouts) destroyLayout(context.device, layout, nullptr);
 }
 
@@ -3425,6 +3485,17 @@ DescriptorCache::SetAllocation DescriptorCache::Allocate(VkDescriptorSetLayout l
     allocation.pSetLayouts = &layout;
     if (!fits) {
         dedicated.reserve(dedicated.size() + 1);
+        const auto same = std::find_if(kept.begin(), kept.end(), [&](const SetAllocation& set) {
+            return set.layout == layout && set.updateAfterBind == updateAfterBind && set.variableCount == variableCount && std::equal(set.sizes.begin(), set.sizes.end(), sizes.begin(), sizes.end(), [](const VkDescriptorPoolSize& a, const VkDescriptorPoolSize& b) { return a.type == b.type && a.descriptorCount == b.descriptorCount; });
+        });
+        if (same != kept.end()) {
+            auto reused = std::move(*same);
+            kept.erase(same);
+            dedicated.push_back(reused.pool);
+            ++stats.reusedSets;
+            ++stats.sets;
+            return reused;
+        }
         const auto pool = CreateSetPool(context, sizes, updateAfterBind);
         allocation.descriptorPool = pool;
         VkDescriptorSet set = VK_NULL_HANDLE;
@@ -3436,7 +3507,7 @@ DescriptorCache::SetAllocation DescriptorCache::Allocate(VkDescriptorSetLayout l
         dedicated.push_back(pool);
         ++stats.dedicatedPools;
         ++stats.sets;
-        return {set, pool};
+        return {set, pool, 0, updateAfterBind, layout, std::vector<VkDescriptorPoolSize>(sizes.begin(), sizes.end()), variableCount, {}};
     }
     auto& chain = updateAfterBind ? updateAfterBindPools : pools;
     for (std::size_t index = chain.size(); index != 0; --index) {
@@ -3498,12 +3569,22 @@ void DescriptorCache::Recycle(VkDescriptorSetLayout layout, const SetAllocation&
     Free(allocation);
 }
 
-void DescriptorCache::Free(const SetAllocation& allocation) noexcept {
+void DescriptorCache::Free(const SetAllocation& allocation, std::shared_ptr<const TableDescriptors> table) noexcept {
     if (allocation.set == VK_NULL_HANDLE || allocation.pool == VK_NULL_HANDLE) return;
     std::lock_guard lock(mutex);
     if (const auto found = std::find(dedicated.begin(), dedicated.end(), allocation.pool); found != dedicated.end()) {
         dedicated.erase(found);
-        destroyPool(context.device, allocation.pool, nullptr);
+        if (KeptSets() == 0u || allocation.layout == VK_NULL_HANDLE) {
+            destroyPool(context.device, allocation.pool, nullptr);
+            return;
+        }
+        if (kept.size() >= KeptSets()) {
+            destroyPool(context.device, kept.back().pool, nullptr);
+            kept.pop_back();
+        }
+        kept.insert(kept.begin(), allocation);
+        kept.front().table = std::move(table);
+        ++stats.keptSets;
         return;
     }
     auto& chain = allocation.updateAfterBind ? updateAfterBindPools : pools;
@@ -4159,7 +4240,20 @@ void ShaderResources::resolveImageTables() {
         const auto stageSamplers = std::span<const std::shared_ptr<Sampler>>(samplers).subspan(table.firstSampler, table.samplerCount);
         table.elementTextures.assign(binding.count, std::numeric_limits<std::size_t>::max());
         const auto capacityMessage = [&] { return "the image table exceeds the device's capacity of " + std::to_string(context.imageTableCapacity) + " images"; };
-        const auto bind = [&](std::uint32_t element, std::span<const std::uint32_t> words, std::shared_ptr<Texture> texture, const GuestTextureResource& resource, std::uint64_t guestBytes, bool firstLayer) {
+        std::vector<std::uint32_t> samplerKey;
+        for (std::uint32_t sampler = 0; sampler < table.samplerCount; ++sampler) {
+            const auto& source = samplerSources.at(table.firstSampler + sampler);
+            samplerKey.insert(samplerKey.end(), source.words.begin(), source.words.end());
+            samplerKey.push_back((source.compareEnable ? 1u : 0u) | (source.unnormalized ? 2u : 0u) | (source.integerBorder ? 4u : 0u));
+        }
+        const auto place = [&](std::uint32_t element, std::shared_ptr<Texture> texture, const GuestTextureResource& resource, std::uint64_t guestBytes, bool firstLayer) {
+            textures.push_back(std::move(texture));
+            textureFirstLayer.push_back(firstLayer);
+            describedRanges.push_back({"texture", resource.baseAddress, guestBytes, resource.width, resource.height, resource.format, static_cast<int>(resource.tileMode), resource.dccAddress});
+            table.elementTextures[element] = textures.size() - 1u;
+            item.imageAllocations[static_cast<std::size_t>(table.elementBase) + element] = textures.size() - 1u;
+        };
+        const auto bind = [&](std::uint32_t element, std::span<const std::uint32_t> words, std::shared_ptr<Texture> texture, const GuestTextureResource& resource, std::uint64_t guestBytes, bool firstLayer) -> std::string {
             try {
                 RequireFilterMinmax(context, texture->ViewFormat(), binding.imageSamplers.at(element), stageSamplers);
                 RequireBorderSwizzle(resource.bcSwizzle, binding.imageSamplers.at(element), stageSamplers);
@@ -4176,17 +4270,16 @@ void ShaderResources::resolveImageTables() {
                 }
             } catch (const std::runtime_error& error) {
                 table.map[faults + element] = rejectTableElement(table, words, error.what());
-                return;
+                return error.what();
             }
-            textures.push_back(std::move(texture));
-            textureFirstLayer.push_back(firstLayer);
-            describedRanges.push_back({"texture", resource.baseAddress, guestBytes, resource.width, resource.height, resource.format, static_cast<int>(resource.tileMode), resource.dccAddress});
-            table.elementTextures[element] = textures.size() - 1u;
-            item.imageAllocations[static_cast<std::size_t>(table.elementBase) + element] = textures.size() - 1u;
+            place(element, std::move(texture), resource, guestBytes, firstLayer);
+            return {};
         };
         const auto state = CurrentTableState();
         std::vector<std::shared_ptr<Texture>> cached;
         if (auto* reused = FindReusedTable(context.device, binding, state); reused != nullptr && TableCurrent(*reused) && CachedTableTextures(*reused, cached)) {
+            const bool replay = TableReplay() && reused->replayable && reused->imageSamplers == binding.imageSamplers && reused->samplers == samplerKey;
+            if (!replay) reused->bound.assign(binding.count, {});
             auto next = cached.begin();
             for (std::uint32_t element = 0; element < binding.count; ++element) {
                 const auto words = std::span<const std::uint32_t>(binding.guestDescriptor).subspan(static_cast<std::size_t>(element) * 8u, 8u);
@@ -4198,13 +4291,26 @@ void ShaderResources::resolveImageTables() {
                     table.map[faults + element] = rejectTableElement(table, words, entry.message);
                 } else {
                     CompletePendingCpuWrites(context, entry.resource.baseAddress, static_cast<std::size_t>(entry.guestBytes));
-                    bind(element, words, std::move(*next++), entry.resource, entry.guestBytes, entry.firstLayer);
+                    if (!replay) {
+                        reused->bound[element] = bind(element, words, std::move(*next++), entry.resource, entry.guestBytes, entry.firstLayer);
+                    } else if (!reused->bound[element].empty()) {
+                        ++next;
+                        table.map[faults + element] = rejectTableElement(table, words, reused->bound[element]);
+                    } else {
+                        place(element, std::move(*next++), entry.resource, entry.guestBytes, entry.firstLayer);
+                    }
                 }
             }
-            counters.tablesReused.fetch_add(1, std::memory_order_relaxed);
+            if (!replay) {
+                reused->imageSamplers = binding.imageSamplers;
+                reused->samplers = samplerKey;
+                reused->replayable = static_cast<std::uint64_t>(table.elementBase) + binding.count <= context.imageTableCapacity;
+            }
+            (replay ? counters.tablesReplayed : counters.tablesReused).fetch_add(1, std::memory_order_relaxed);
             continue;
         }
         ReusedTable fresh;
+        fresh.bound.assign(binding.count, {});
         bool reusable = state.epoch != 0;
         for (std::uint32_t element = 0; element < binding.count; ++element) {
             const auto words = std::span<const std::uint32_t>(binding.guestDescriptor).subspan(static_cast<std::size_t>(element) * 8u, 8u);
@@ -4252,7 +4358,7 @@ void ShaderResources::resolveImageTables() {
                     fresh.ranges.emplace_back(resource.baseAddress, resource.baseAddress + guestBytes);
                 }
             }
-            bind(element, words, std::move(texture), resource, guestBytes, firstLayer);
+            fresh.bound[element] = bind(element, words, std::move(texture), resource, guestBytes, firstLayer);
         }
         counters.tablesResolved.fetch_add(1, std::memory_order_relaxed);
         if (reusable && CurrentTableState() == state) {
@@ -4261,6 +4367,9 @@ void ShaderResources::resolveImageTables() {
                 fresh.device = context.device;
                 fresh.state = state;
                 fresh.pendingSerial = serial;
+                fresh.imageSamplers = binding.imageSamplers;
+                fresh.samplers = std::move(samplerKey);
+                fresh.replayable = static_cast<std::uint64_t>(table.elementBase) + binding.count <= context.imageTableCapacity;
                 RememberTable(std::move(fresh), binding);
             }
         }
@@ -4270,7 +4379,7 @@ void ShaderResources::resolveImageTables() {
 
 ImageTableReuseCounts ImageTableReuses() {
     const auto& counters = TextureCounts();
-    return {counters.tablesReused.load(std::memory_order_relaxed), counters.tablesResolved.load(std::memory_order_relaxed)};
+    return {counters.tablesReused.load(std::memory_order_relaxed) + counters.tablesReplayed.load(std::memory_order_relaxed), counters.tablesResolved.load(std::memory_order_relaxed), counters.tablesReplayed.load(std::memory_order_relaxed), counters.tableWritesKept.load(std::memory_order_relaxed), counters.tableWritesMade.load(std::memory_order_relaxed)};
 }
 
 void ShaderResources::finishImageTables() {
@@ -4379,12 +4488,29 @@ ShaderResources::~ShaderResources() {
     release();
 }
 
+std::shared_ptr<const TableDescriptors> ShaderResources::keptTable(std::size_t set, std::shared_ptr<const std::vector<std::shared_ptr<Texture>>>& held) noexcept {
+    const auto& table = tableWrites[set];
+    if (!table.written || KeptSets() == 0u || table.firstImage + table.images > descriptorImages.size()) return nullptr;
+    try {
+        if (held == nullptr) held = std::make_shared<const std::vector<std::shared_ptr<Texture>>>(std::move(textures));
+        auto kept = std::make_shared<TableDescriptors>();
+        kept->binding = table.binding;
+        kept->runs = table.runs;
+        kept->images.assign(descriptorImages.begin() + static_cast<std::ptrdiff_t>(table.firstImage), descriptorImages.begin() + static_cast<std::ptrdiff_t>(table.firstImage + table.images));
+        kept->textures = held;
+        return kept;
+    } catch (...) {
+        return nullptr;
+    }
+}
+
 void ShaderResources::release() noexcept {
     // A set from the cache's pool chain goes back to it; a dedicated pool dies with its set.
-    if (cacheAllocation.pool && context.descriptorCache != nullptr) context.descriptorCache->Free(cacheAllocation);
+    std::shared_ptr<const std::vector<std::shared_ptr<Texture>>> held;
+    if (cacheAllocation.pool && context.descriptorCache != nullptr) context.descriptorCache->Free(cacheAllocation, keptTable(0, held));
     if (pool) context.Function<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(context.device, pool, nullptr);
     if (_layout && ownsLayout) context.Function<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(context.device, _layout, nullptr);
-    if (fragment.cacheAllocation.pool && context.descriptorCache != nullptr) context.descriptorCache->Free(fragment.cacheAllocation);
+    if (fragment.cacheAllocation.pool && context.descriptorCache != nullptr) context.descriptorCache->Free(fragment.cacheAllocation, keptTable(1, held));
     if (fragment.pool) context.Function<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(context.device, fragment.pool, nullptr);
     if (fragment.layout && fragment.ownsLayout) context.Function<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(context.device, fragment.layout, nullptr);
     cacheAllocation = {};
@@ -4392,6 +4518,7 @@ void ShaderResources::release() noexcept {
     _set = VK_NULL_HANDLE;
     _layout = VK_NULL_HANDLE;
     fragment = {};
+    tableWrites = {};
 }
 
 std::uint32_t ShaderResources::setOf(const Binding& binding) const {
diff --git a/core/libs/prx/libSceAgcDriver/Graphics/src/Texture.cpp b/core/libs/prx/libSceAgcDriver/Graphics/src/Texture.cpp
index b44f0fbcd..1c70ccc04 100644
--- a/core/libs/prx/libSceAgcDriver/Graphics/src/Texture.cpp
+++ b/core/libs/prx/libSceAgcDriver/Graphics/src/Texture.cpp
@@ -1148,28 +1148,48 @@ namespace {
 class PendingList {
 public:
     using iterator = std::vector<StorageTexture*>::iterator;
+    using Ranges = std::vector<std::pair<std::uint64_t, std::uint64_t>>;
     iterator begin() { return textures.begin(); }
     iterator end() { return textures.end(); }
     void push_back(StorageTexture* texture) {
         textures.push_back(texture);
-        ++version;
+        changed();
     }
     iterator erase(iterator it) {
-        ++version;
-        return textures.erase(it);
+        const auto next = textures.erase(it);
+        changed();
+        return next;
     }
     void remove(const StorageTexture* texture) {
-        if (std::erase(textures, texture) != 0) ++version;
+        if (std::erase(textures, texture) != 0) changed();
     }
     bool MayOverlap(std::uint64_t address, std::size_t bytes) {
-        if (textures.empty() || bytes == 0) return false;
         if (indexed != version) rebuild();
+        return !textures.empty() && Overlaps(ranges, address, bytes);
+    }
+    bool SurelyClear(std::uint64_t address, std::size_t bytes) const {
+        const auto current = published.load(std::memory_order_acquire);
+        return current != nullptr && current->version == changes.load(std::memory_order_acquire) && !Overlaps(current->ranges, address, bytes);
+    }
+
+private:
+    struct Snapshot {
+        std::uint64_t version = 0;
+        Ranges ranges;
+    };
+
+    static bool Overlaps(const Ranges& ranges, std::uint64_t address, std::size_t bytes) {
+        if (bytes == 0) return false;
         const auto end = bytes > std::numeric_limits<std::uint64_t>::max() - address ? std::numeric_limits<std::uint64_t>::max() : address + bytes;
         const auto it = std::upper_bound(ranges.begin(), ranges.end(), address, [](std::uint64_t value, const std::pair<std::uint64_t, std::uint64_t>& range) { return value < range.second; });
         return it != ranges.end() && it->first < end;
     }
 
-private:
+    void changed() {
+        ++version;
+        changes.store(version, std::memory_order_release);
+    }
+
     void rebuild() {
         ranges.clear();
         for (const auto* texture : textures) ranges.emplace_back(texture->Descriptor().baseAddress, texture->Descriptor().baseAddress + texture->GuestBytes());
@@ -1181,11 +1201,14 @@ private:
         }
         ranges.resize(merged);
         indexed = version;
+        published.store(std::make_shared<const Snapshot>(Snapshot{version, ranges}), std::memory_order_release);
     }
     std::vector<StorageTexture*> textures;
-    std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
+    Ranges ranges;
     std::uint64_t version = 0;
     std::uint64_t indexed = ~std::uint64_t{0};
+    std::atomic<std::uint64_t> changes{0};
+    std::atomic<std::shared_ptr<const Snapshot>> published;
 };
 
 // Storage images whose results have not reached guest memory yet.
@@ -3596,6 +3619,7 @@ std::vector<std::shared_ptr<StorageTexture>> StorageTexture::overlappingPending(
     // FlushPending's listing; the images stay alive across the caller's scan as there.
     std::vector<std::shared_ptr<StorageTexture>> overlapping;
     auto& pending = Pending();
+    if (pending.textures.SurelyClear(address, bytes)) return overlapping;
     std::lock_guard lock(pending.mutex);
     if (!pending.textures.MayOverlap(address, bytes)) return overlapping;
     const auto* exempt = refreshing;
diff --git a/core/libs/prx/libSceAgcDriver/tests/Graphics.cpp b/core/libs/prx/libSceAgcDriver/tests/Graphics.cpp
index 4a69933a7..c5e5916dc 100644
--- a/core/libs/prx/libSceAgcDriver/tests/Graphics.cpp
+++ b/core/libs/prx/libSceAgcDriver/tests/Graphics.cpp
@@ -4031,19 +4031,27 @@ void descriptorCacheTests() {
         Require(mock.poolSizes.size() == 2 && mock.poolSizes[0].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER && mock.poolSizes[0].descriptorCount == 4097 && mock.poolSizes[1].type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE && mock.poolSizes[1].descriptorCount == 3, "the dedicated pool is not sized to its set");
         Require(cache.Counters().pools == 0 && cache.Counters().sets == 1, "an oversized set was counted in the chain pools");
         cache.Free(dedicated);
-        Require(mock.live == live, "freeing an oversized set did not destroy its pool");
+        Require(mock.live == live + 1 && cache.Counters().keptSets == 1, "freeing an oversized set did not keep its set and pool");
+        const auto again = cache.Allocate(large, oversized);
+        Require(again.set == dedicated.set && again.pool == dedicated.pool && mock.live == live + 1 && cache.Counters().reusedSets == 1 && cache.Counters().dedicatedPools == 1, "an oversized set of the kept set's layout and sizes did not take the kept set");
+        cache.Free(again);
+        const std::array<VkDescriptorPoolSize, 2> otherSizes{{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4098}, {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 3}}};
+        const auto other = cache.Allocate(large, otherSizes);
+        Require(other.pool != dedicated.pool && mock.live == live + 2 && cache.Counters().dedicatedPools == 2, "an oversized set of other sizes took a kept set");
+        cache.Free(other);
+        Require(mock.live == live + 2 && cache.Counters().keptSets == 3, "freeing a second oversized set did not keep it");
         const std::array<VkDescriptorPoolSize, 1> fitting{{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4096}}};
         const auto chained = cache.Allocate(layout(4096), fitting);
         Require(chained.set != VK_NULL_HANDLE && mock.poolMaxSets == 1024 && mock.poolFlags == 0 && cache.Counters().pools == 1, "a set the chain pool holds left the chain");
         cache.Free(chained);
-        Require(mock.descriptorPools.at(chained.pool).resets == 1 && mock.live == live + 2, "a chain pool was not reset after its last set");
+        Require(mock.descriptorPools.at(chained.pool).resets == 1 && mock.live == live + 4, "a chain pool was not reset after its last set");
         const std::array<VkDescriptorPoolSize, 1> regular{{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4096}}};
         const auto first = cache.Allocate(layout(4096), regular);
         const auto oversizedMixed = cache.Allocate(large, oversized);
         const auto second = cache.Allocate(layout(4096), regular);
         Require(first.pool == chained.pool && second.pool == chained.pool && oversizedMixed.pool != chained.pool && mock.descriptorPools.at(chained.pool).resets == 1, "an oversized set disturbed the chain pool");
         cache.Free(oversizedMixed);
-        Require(mock.live == live + 2 && mock.descriptorPools.count(oversizedMixed.pool) == 0 && mock.descriptorPools.at(chained.pool).resets == 1, "freeing an oversized set between chain sets touched the chain pool");
+        Require(oversizedMixed.pool == dedicated.pool && mock.live == live + 4 && mock.descriptorPools.count(oversizedMixed.pool) == 1 && mock.descriptorPools.at(chained.pool).resets == 1, "freeing an oversized set between chain sets touched the chain pool");
         cache.Free(first);
         Require(mock.descriptorPools.at(chained.pool).resets == 1, "a chain pool reset while a set was live");
         cache.Free(second);
@@ -4054,6 +4062,15 @@ void descriptorCacheTests() {
         expectFailure([&] { cache.Allocate(large, storageImages); }, "descriptor set exceeds the device's per-set descriptor limit");
         const std::array<VkDescriptorPoolSize, 1> uniformBuffers{{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}}};
         expectFailure([&] { cache.Allocate(large, uniformBuffers); }, "descriptor set uses an unsupported descriptor type 6");
+        const auto keptBefore = mock.live;
+        std::vector<AgcDriver::Graphics::DescriptorCache::SetAllocation> spread;
+        for (std::uint32_t index = 0; index < 10; ++index) {
+            const std::array<VkDescriptorPoolSize, 1> sized{{{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4100 + index}}};
+            spread.push_back(cache.Allocate(large, sized));
+        }
+        Require(mock.live == keptBefore + 10, "ten oversized sets of new sizes did not get ten pools");
+        for (const auto& set : spread) cache.Free(set);
+        Require(mock.live == keptBefore - 2 + 8, "freeing more oversized sets than the cache keeps did not destroy the oldest pools");
     }
     Require(mock.live == 0, "the descriptor cache leaked a pool or layout");
     mock = MockVulkan{};
diff --git a/core/libs/prx/libSceAgcDriver/tests/ShaderDiskCache.cpp b/core/libs/prx/libSceAgcDriver/tests/ShaderDiskCache.cpp
index 0b8ce3301..307007f7f 100644
--- a/core/libs/prx/libSceAgcDriver/tests/ShaderDiskCache.cpp
+++ b/core/libs/prx/libSceAgcDriver/tests/ShaderDiskCache.cpp
@@ -1984,6 +1984,10 @@ std::filesystem::path planEntryPath(PlanFixture& fixture) {
 int runPlanLoadingProcess(std::string_view mode) {
     if (mode == "--load-plan-switch") setEnvironment("APS5_TRACE_IMAGE_TABLE", "1");
     if (mode == "--load-plan-off") setEnvironment("APS5_NO_PERF_DISK_PLANS", "1");
+    if (mode == "--load-plan-reuse-off") {
+        setEnvironment("APS5_NO_TABLE_SNAPSHOT_REUSE", "1");
+        setEnvironment("APS5_NO_TABLE_RESOLUTION_REUSE", "1");
+    }
     auto fixtures = planFixtures();
     const auto count = fixtures.size();
     std::vector<std::shared_ptr<const IrResourcePlan>> plans;
@@ -1999,6 +2003,8 @@ int runPlanLoadingProcess(std::string_view mode) {
     }
     if (mode == "--load-plan") {
         require(builds == 0u && totals.planHits == count && totals.planMisses == 0u && totals.planLoadFailures == 0u && totals.hits == count && totals.misses == 0u, "the second process ran the front end" + counts);
+    } else if (mode == "--load-plan-reuse-off") {
+        require(builds == 0u && totals.planHits == count && totals.planMisses == 0u && totals.planLoadFailures == 0u && totals.hits == count && totals.misses == 0u, "turning the image table reuse off changed the stored plan or variant keys" + counts);
     } else if (mode == "--load-plan-off") {
         require(builds == count && totals.planHits == 0u && totals.planMisses == 0u && totals.planWrites == 0u && totals.hits == count && totals.misses == 0u, "APS5_NO_PERF_DISK_PLANS=1 used the stored plans or missed the stored variants" + counts);
     } else {
@@ -2018,6 +2024,8 @@ int runPlanLoadingProcess(std::string_view mode) {
         const auto after = ShaderDiskCache::Totals();
         require(FrontEndBuilds() == 1u && after.planHits == count && after.planMisses == 1u, "a changed code word reused a stored plan");
         std::cout << "second process prepared " << count << " shaders from stored plans without running the front end\n";
+    } else if (mode == "--load-plan-reuse-off") {
+        std::cout << "with the image table reuse off, a process loaded all " << count << " stored plans and variants with the same output\n";
     } else if (mode == "--load-plan-off") {
         std::cout << "APS5_NO_PERF_DISK_PLANS=1 ran the front end for all " << count << " shaders with the same output\n";
     } else {
@@ -2061,6 +2069,7 @@ void verifyPlanAcrossProcesses(const char* self) {
     };
     run("--load-plan");
     run("--load-plan-off");
+    run("--load-plan-reuse-off");
     std::vector<std::byte> entry;
     const auto damaged = planEntryPath(fixtures.front());
     require(ReadWholeFile(damaged, entry), "cannot read a stored plan entry");
diff --git a/core/libs/prx/libSceAgcDriver/tests/ShaderMemory.cpp b/core/libs/prx/libSceAgcDriver/tests/ShaderMemory.cpp
index 92cc7b1bc..3ff679870 100644
--- a/core/libs/prx/libSceAgcDriver/tests/ShaderMemory.cpp
+++ b/core/libs/prx/libSceAgcDriver/tests/ShaderMemory.cpp
@@ -40,6 +40,8 @@
 #include <memory>
 #include <stdexcept>
 #include <span>
+#include <atomic>
+#include <thread>
 #include <string>
 #include <string_view>
 #include <tuple>
@@ -1063,7 +1065,7 @@ std::array<std::uint32_t, 8> TableSelected(const ShaderRecompiler::ImageTableSna
     namespace Abi = ShaderRecompiler::ImageTableAbi;
     const auto& column = tables.tables.at(table);
     require(column.records == 1u && column.keys == 1u && column.codes.size() == 1u && (column.codes[0] & Abi::PoisonFlag) == 0u, message);
-    return tables.words.at(column.codes[0]).dwords;
+    return column.words.at(column.codes[0]).dwords;
 }
 
 void verifyImageTableKeys() {
@@ -1245,21 +1247,52 @@ void verifyTableSnapshotReuse() {
         ImageTableSnapshot tables;
         bool covered = false;
         ImageTableReuseCounts reuse;
+        AgcDriver::ShaderMemory::SupplyCounts supplied{};
+        const std::byte* tableBytes = nullptr;
+        std::size_t tableRegionBytes = 0;
+        std::vector<std::pair<std::uint64_t, std::size_t>> tableRegions;
+    };
+    const auto sortedDisjoint = [](const std::vector<MemoryRegion>& regions) {
+        for (std::size_t index = 1; index < regions.size(); index++) {
+            if (regions[index - 1].guestAddress + regions[index - 1].bytes.size() > regions[index].guestAddress) return false;
+        }
+        return true;
+    };
+    const auto holds = [](const std::vector<MemoryRegion>& regions, const std::uint32_t* table, std::size_t bytes, const std::uint32_t* expected) {
+        const auto begin = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(table));
+        std::size_t found = 0;
+        for (const auto& region : regions) {
+            const auto from = std::max(region.guestAddress, begin);
+            const auto to = std::min(region.guestAddress + region.bytes.size(), begin + bytes);
+            if (from >= to) continue;
+            if (std::memcmp(region.bytes.data() + (from - region.guestAddress), reinterpret_cast<const std::byte*>(expected) + (from - begin), static_cast<std::size_t>(to - from)) != 0) return false;
+            found += static_cast<std::size_t>(to - from);
+        }
+        return found == bytes;
     };
     const auto capture = [&](const RecompileRequest& request, const std::uint32_t* table, std::size_t bytes, AgcDriver::ShaderMemory::PendingWriteQuery pending) {
         const auto before = ResourceMaterializer::TableReuse();
+        const auto suppliedBefore = AgcDriver::ShaderMemory::Supplies();
         Outcome outcome;
         AgcDriver::ShaderMemory memory({}, pending);
         outcome.tables = memory.Capture(request)->snapshot.tables;
         const auto regions = memory.Regions();
+        const auto suppliedAfter = AgcDriver::ShaderMemory::Supplies();
+        outcome.supplied = {suppliedAfter.shared - suppliedBefore.shared, suppliedAfter.copied - suppliedBefore.copied, suppliedAfter.spilled - suppliedBefore.spilled};
+        for (const auto& region : regions) {
+            if (region.guestAddress < reinterpret_cast<std::uintptr_t>(table) || region.guestAddress >= reinterpret_cast<std::uintptr_t>(table) + bytes) continue;
+            if (outcome.tableBytes == nullptr) outcome.tableBytes = region.bytes.data();
+            if (region.bytes.data() == outcome.tableBytes + outcome.tableRegionBytes) outcome.tableRegionBytes += region.bytes.size();
+            outcome.tableRegions.emplace_back(region.guestAddress, region.bytes.size());
+        }
         outcome.covered = TableCovered(regions, table, bytes) && std::all_of(regions.begin(), regions.end(), [&](const MemoryRegion& region) {
             return region.guestAddress < reinterpret_cast<std::uintptr_t>(table) || region.guestAddress >= reinterpret_cast<std::uintptr_t>(table) + bytes || std::memcmp(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(region.guestAddress)), region.bytes.data(), region.bytes.size()) == 0;
         });
         const auto after = ResourceMaterializer::TableReuse();
-        outcome.reuse = {after.unwritten - before.unwritten, after.compared - before.compared, after.wordsRead - before.wordsRead};
+        outcome.reuse = {after.unwritten - before.unwritten, after.compared - before.compared, after.wordsRead - before.wordsRead, after.shared - before.shared, after.columns - before.columns};
         return outcome;
     };
-    const auto recordWords = [](const Outcome& outcome, std::uint32_t record) { return outcome.tables.words.at(outcome.tables.tables.at(0).codes.at(record)).dwords; };
+    const auto recordWords = [](const Outcome& outcome, std::uint32_t record) { return outcome.tables.tables.at(0).words.at(outcome.tables.tables.at(0).codes.at(record)).dwords; };
     const auto heldRecords = [&](const Outcome& outcome, const std::uint32_t* table, std::uint32_t records) {
         for (std::uint32_t record = 0; record < records; record++) {
             std::array<std::uint32_t, 8> words{};
@@ -1268,6 +1301,16 @@ void verifyTableSnapshotReuse() {
         }
         return true;
     };
+    const auto sharesColumn = [](const Outcome& left, const Outcome& right) {
+        const auto& a = left.tables.tables.at(0);
+        const auto& b = right.tables.tables.at(0);
+        return a.codes.Shares(b.codes) && a.words.Shares(b.words) && a.readRanges.Shares(b.readRanges);
+    };
+    const auto sharedColumnOf = [](const std::shared_ptr<const ResourceCapture>& capture, const Outcome& outcome) {
+        const auto& a = capture->snapshot.tables.tables.at(0);
+        const auto& b = outcome.tables.tables.at(0);
+        return a.codes.Shares(b.codes) && a.words.Shares(b.words) && a.readRanges.Shares(b.readRanges);
+    };
 
     auto* heap = AllocateWatchedTable(Bytes);
     fill(heap, Records);
@@ -1288,9 +1331,44 @@ void verifyTableSnapshotReuse() {
 
     const auto fresh = capture(request, heap, Bytes, nullptr);
     require(fresh.reuse.unwritten == 0u && fresh.reuse.compared == 0u && fresh.reuse.wordsRead == Records * 8u && heldRecords(fresh, heap, Records) && fresh.covered, "table snapshot reuse: the first snapshot is wrong");
+    require(fresh.reuse.columns == 1u && fresh.reuse.shared == 0u, "table snapshot reuse: the first snapshot did not build one column");
+    const std::vector<std::uint32_t> original(heap, heap + Bytes / sizeof(std::uint32_t));
     const auto unchanged = capture(request, heap, Bytes, nullptr);
     expectUnwritten(unchanged, "an unchanged table");
     require(unchanged.tables == fresh.tables, "table snapshot reuse: a reused snapshot differs from the snapshot it reuses");
+    const auto reusedAgain = capture(request, heap, Bytes, nullptr);
+    expectUnwritten(reusedAgain, "an unchanged table reused again");
+    require(sharesColumn(unchanged, fresh) && sharesColumn(reusedAgain, fresh) && unchanged.reuse.shared == 1u && reusedAgain.reuse.shared == 1u && unchanged.reuse.columns == 0u && reusedAgain.reuse.columns == 0u, "table snapshot reuse: an unchanged table reused twice did not share one column without a copy");
+    if (watched) {
+        const auto sharedWords = [&](const Outcome& outcome) { return outcome.supplied.shared == Records * 8u && outcome.supplied.copied == 0u && outcome.supplied.spilled == 0u && outcome.tableRegionBytes == Bytes; };
+        require(sharedWords(unchanged) && sharedWords(reusedAgain) && unchanged.tableBytes != nullptr && unchanged.tableBytes == reusedAgain.tableBytes && fresh.tableBytes != unchanged.tableBytes && fresh.supplied.shared == 0u, "table snapshot reuse: the captured words of an unchanged table were copied instead of shared");
+    }
+    require(fresh.tableRegions.size() == Bytes / 4096u && unchanged.tableRegions == fresh.tableRegions && reusedAgain.tableRegions == fresh.tableRegions, "table snapshot reuse: a reused table's words are not reported in the regions a read of them reports");
+    {
+        AgcDriver::ShaderMemory stages({});
+        const auto suppliedBefore = AgcDriver::ShaderMemory::Supplies();
+        const auto vertex = stages.Capture(request);
+        const auto vertexRecent = stages.TakeRecentRegions();
+        const auto pixel = stages.Capture(request);
+        const auto pixelRecent = stages.TakeRecentRegions();
+        const auto both = stages.Regions();
+        require(sharedColumnOf(vertex, fresh) && sharedColumnOf(pixel, fresh) && sortedDisjoint(vertexRecent) && sortedDisjoint(pixelRecent) && sortedDisjoint(both) && holds(vertexRecent, heap, Bytes, original.data()) && holds(pixelRecent, heap, Bytes, original.data()) && holds(both, heap, Bytes, original.data()), "table snapshot reuse: two stages of one shader memory did not each capture the shared table once");
+        alignas(4096) static TableGuest halfGuest;
+        FillTableSrt(halfGuest, heap, 32u, Records / 2u);
+        const auto halfUserData = TableUserData(halfGuest);
+        const auto half = stages.Capture(TableRequest(TableWholeCode, halfUserData));
+        const auto all = stages.Regions();
+        const auto suppliedAfter = AgcDriver::ShaderMemory::Supplies();
+        require(half->snapshot.tables.tables.at(0).keys == Records / 2u && sortedDisjoint(all) && holds(all, heap, Bytes, original.data()), "table snapshot reuse: a read over supplied words reported them twice or changed them");
+        if (watched) require(suppliedAfter.shared - suppliedBefore.shared == 2u * Records * 8u && suppliedAfter.copied == suppliedBefore.copied && suppliedAfter.spilled - suppliedBefore.spilled == Records * 8u, "table snapshot reuse: a second stage copied the shared words, or a read over them did not move them into pages once");
+    }
+    AgcDriver::ShaderMemory heldMemory({});
+    const auto heldCapture = heldMemory.Capture(request);
+    const auto freshColumn = [&](const Outcome& outcome, const std::string& what) {
+        require(outcome.reuse.columns == 1u && outcome.reuse.shared == 0u && !sharesColumn(outcome, fresh) && !(outcome.tables == fresh.tables) && outcome.supplied.shared == 0u, ("table snapshot reuse: " + what + ": the snapshot shares the old column or holds its words").c_str());
+        require(heldRecords(unchanged, original.data(), Records) && heldRecords(reusedAgain, original.data(), Records) && sharesColumn(reusedAgain, fresh), ("table snapshot reuse: " + what + ": the old column changed under the snapshots sharing it").c_str());
+        require(sharedColumnOf(heldCapture, fresh) && holds(heldMemory.Regions(), heap, Bytes, original.data()), ("table snapshot reuse: " + what + ": the words a live shader memory captured before it changed").c_str());
+    };
 
     const auto changeRecord = [&](std::uint32_t record, std::uint32_t replacement, const std::function<void(std::uint64_t, const std::array<std::uint32_t, 8>&)>& store) {
         const auto words = texture(replacement);
@@ -1300,23 +1378,27 @@ void verifyTableSnapshotReuse() {
     const auto cpuStore = capture(request, heap, Bytes, nullptr);
     expectReread(cpuStore, "a CPU store");
     require(heldRecords(cpuStore, heap, Records) && cpuStore.covered && recordWords(cpuStore, 77u) == texture(2000u), "table snapshot reuse: the snapshot after a CPU store misses the stored T#");
+    freshColumn(cpuStore, "a CPU store");
     expectUnwritten(capture(request, heap, Bytes, nullptr), "the table after a CPU store");
 
     changeRecord(1234u, 2001u, [](std::uint64_t at, const std::array<std::uint32_t, 8>& words) { AgcDriver::GuestMemory::Write(at, std::as_bytes(std::span(words)), 4); });
     const auto driverStore = capture(request, heap, Bytes, nullptr);
     expectReread(driverStore, "a driver store (CP write, DMA copy or fill)");
     require(heldRecords(driverStore, heap, Records) && recordWords(driverStore, 1234u) == texture(2001u), "table snapshot reuse: the snapshot after a driver store misses the stored T#");
+    freshColumn(driverStore, "a driver store");
     expectUnwritten(capture(request, heap, Bytes, nullptr), "the table after a driver store");
 
     changeRecord(2048u, 2002u, [](std::uint64_t at, const std::array<std::uint32_t, 8>& words) { AgcDriver::GuestMemory::StoreOwnBytes(at, std::as_bytes(std::span(words))); });
     const auto gpuStore = capture(request, heap, Bytes, nullptr);
     expectReread(gpuStore, "a GPU result stored by the driver");
     require(heldRecords(gpuStore, heap, Records) && recordWords(gpuStore, 2048u) == texture(2002u), "table snapshot reuse: the snapshot after a stored GPU result misses the stored T#");
+    freshColumn(gpuStore, "a GPU result stored by the driver");
     expectUnwritten(capture(request, heap, Bytes, nullptr), "the table after a stored GPU result");
 
     AgcDriver::GuestMemory::MarkWritten(address + 3000u * 32u, 32u);
     const auto marked = capture(request, heap, Bytes, nullptr);
     require(marked.reuse.unwritten == 0u && marked.reuse.compared == 1u && heldRecords(marked, heap, Records) && marked.tables == gpuStore.tables, "table snapshot reuse: a GPU write into an import (MarkWritten) did not make the table compared again");
+    require(sharesColumn(marked, gpuStore) && marked.reuse.columns == 0u, "table snapshot reuse: a compared table did not share the column it matched");
 
     pendingTableWord = address + 500u * 32u;
     pendingTableValue = 0x0badf00du;
@@ -1324,9 +1406,12 @@ void verifyTableSnapshotReuse() {
     const auto known = capture(request, heap, Bytes, &PendingTableWrite);
     expectReread(known, "a pending GPU write with a known value");
     require(recordWords(known, 500u)[0] == pendingTableValue && recordWords(known, 499u) == recordWords(gpuStore, 499u), "table snapshot reuse: the snapshot under a pending GPU write misses its known value");
+    freshColumn(known, "a pending GPU write with a known value");
+    require(!sharesColumn(known, gpuStore), "table snapshot reuse: a pending GPU write's snapshot shares the column read before it");
     pendingTableKnown = false;
     const auto synced = capture(request, heap, Bytes, &PendingTableWrite);
     require(synced.reuse.unwritten == 0u && heldRecords(synced, heap, Records), "table snapshot reuse: a table under a pending GPU write was reused without a read");
+    require(!sharesColumn(synced, known) && recordWords(known, 500u)[0] == pendingTableValue, "table snapshot reuse: a synced read shares or changed the column of the known value it replaced");
     pendingTableWord = 0;
     const auto settled = capture(request, heap, Bytes, nullptr);
     require(settled.reuse.unwritten == 0u && heldRecords(settled, heap, Records), "table snapshot reuse: a snapshot taken under a pending GPU write was reused without a read");
@@ -1341,6 +1426,7 @@ void verifyTableSnapshotReuse() {
     const auto deferred = capture(request, heap, Bytes, nullptr);
     AgcDriver::GuestMemory::SetFlushHook(nullptr);
     require(deferredTableStored && deferred.reuse.unwritten == 0u && heldRecords(deferred, heap, Records) && recordWords(deferred, 3500u) == texture(2003u), "table snapshot reuse: a GPU result a read would store first was not stored and read before the table was reused");
+    freshColumn(deferred, "a GPU result stored by the flush hook");
     expectUnwritten(capture(request, heap, Bytes, nullptr), "the table after a GPU result stored by the flush hook");
 
     static std::uint8_t* const unrelated = new (std::align_val_t{65536}) std::uint8_t[65536]();
@@ -1355,6 +1441,119 @@ void verifyTableSnapshotReuse() {
     }
     require(remapped.reuse.unwritten == 0u && remapped.reuse.compared == 1u && heldRecords(remapped, heap, Records), "table snapshot reuse: a table was reused without a read after a guest mapping changed");
 
+    auto& flipped = heap[100u * 8u + 1u];
+    const auto keptWord = flipped;
+    flipped ^= 0x100u;
+    const auto changed = capture(request, heap, Bytes, nullptr);
+    flipped = keptWord;
+    const auto restored = capture(request, heap, Bytes, nullptr);
+    const auto& remappedColumn = remapped.tables.tables.at(0);
+    const auto& changedColumn = changed.tables.tables.at(0);
+    const auto& restoredColumn = restored.tables.tables.at(0);
+    require(changed.reuse.columns == 1u && !changedColumn.words.Shares(remappedColumn.words) && !(changedColumn.words == remappedColumn.words) && changedColumn.words.Hash() != remappedColumn.words.Hash(), "table snapshot reuse: a changed table matched the column read before the change");
+    require(restored.reuse.columns == 1u && !sharesColumn(restored, remapped) && !sharesColumn(restored, changed) && restored.tables == remapped.tables && restoredColumn.codes.Hash() == remappedColumn.codes.Hash() && restoredColumn.words.Hash() == remappedColumn.words.Hash() && !(restored.tables == changed.tables), "table snapshot reuse: a table written back to its old words is not a new column equal to the old one");
+    const SharedValues<std::uint32_t> sealed(std::vector<std::uint32_t>{1u, 2u, 3u});
+    const auto alias = sealed;
+    const SharedValues<std::uint32_t> equal(std::vector<std::uint32_t>{1u, 2u, 3u});
+    const SharedValues<std::uint32_t> unequal(std::vector<std::uint32_t>{1u, 2u, 4u});
+    require(alias.Shares(sealed) && alias == sealed && !equal.Shares(sealed) && equal == sealed && equal.Hash() == sealed.Hash() && !unequal.Shares(sealed) && !(unequal == sealed) && unequal.Hash() != sealed.Hash() && SharedValues<std::uint32_t>{}.Shares(SharedValues<std::uint32_t>(std::vector<std::uint32_t>{})), "table snapshot reuse: shared values compare by identity before their contents");
+
+    struct CapturedVariant {
+        std::vector<std::pair<std::uint64_t, std::uint64_t>> runs;
+        std::vector<std::uint32_t> words;
+        std::vector<AgcDriver::SharedWords> shared;
+    };
+    const auto captureVariant = [](const RecompileRequest& request) {
+        CapturedVariant variant;
+        AgcDriver::ShaderMemory memory({});
+        static_cast<void>(memory.Capture(request));
+        const auto supplied = memory.SuppliedBlocks();
+        for (const auto& region : memory.TakeRecentRegions()) {
+            variant.runs.emplace_back(region.guestAddress, region.guestAddress + region.bytes.size());
+            const auto offset = variant.words.size();
+            variant.words.resize(offset + region.bytes.size() / sizeof(std::uint32_t));
+            std::memcpy(variant.words.data() + offset, region.bytes.data(), region.bytes.size());
+            AgcDriver::AppendSharedWords(variant.shared, variant.runs.size() - 1u, offset, region.bytes, supplied);
+        }
+        return variant;
+    };
+    const auto sameVariant = [](const CapturedVariant& left, const CapturedVariant& right, std::size_t& compared) { return left.runs == right.runs && AgcDriver::SameCapturedWords(left.words, left.shared, right.words, right.shared, &compared); };
+    const auto sameBlock = [](const CapturedVariant& left, const CapturedVariant& right) { return left.shared.size() == 1u && right.shared.size() == 1u && !left.shared[0].block.owner_before(right.shared[0].block) && !right.shared[0].block.owner_before(left.shared[0].block); };
+    const auto tableWords = static_cast<std::size_t>(Records) * 8u;
+    const auto keptVariant = captureVariant(request);
+    const auto freshVariant = captureVariant(request);
+    std::size_t comparedWords = 0;
+    require(sameVariant(keptVariant, freshVariant, comparedWords) && keptVariant.words == freshVariant.words, "draw cache identity: two captures of an unchanged table did not match");
+    if (watched) require(sameBlock(keptVariant, freshVariant) && keptVariant.shared[0].count == tableWords && comparedWords == keptVariant.words.size() - tableWords, "draw cache identity: two captures sharing a table column compared its words instead of its identity");
+    else require(keptVariant.shared.empty() && comparedWords == keptVariant.words.size(), "draw cache identity: captures of an unwatched table were not compared word by word");
+
+    alignas(16) static std::array<std::uint32_t, 4> variantBuffer{};
+    const auto variantBufferV = TableV(variantBuffer.data(), 0u, static_cast<std::uint32_t>(sizeof(variantBuffer)));
+    const std::array<std::uint32_t, 4> variantKeptV{guest.srt[12], guest.srt[13], guest.srt[14], guest.srt[15]};
+    std::copy(variantBufferV.begin(), variantBufferV.end(), guest.srt.begin() + 12);
+    const auto otherSrtVariant = captureVariant(request);
+    std::copy(variantKeptV.begin(), variantKeptV.end(), guest.srt.begin() + 12);
+    require(keptVariant.runs == otherSrtVariant.runs && !sameVariant(keptVariant, otherSrtVariant, comparedWords), "draw cache identity: a capture with other SRT words matched through the table column it shares");
+    if (watched) require(sameBlock(keptVariant, otherSrtVariant), "draw cache identity: a capture with other SRT words did not share the unchanged table column");
+
+    auto& variantWord = heap[9u * 8u + 1u];
+    const auto variantKeptWord = variantWord;
+    variantWord ^= 0x100u;
+    const auto writtenVariant = captureVariant(request);
+    variantWord = variantKeptWord;
+    require(keptVariant.runs == writtenVariant.runs && !sameVariant(keptVariant, writtenVariant, comparedWords) && comparedWords == keptVariant.words.size(), "draw cache identity: a capture after a CPU store matched the capture before it");
+    const auto rereadVariant = captureVariant(request);
+    const auto rewrittenVariant = captureVariant(request);
+    require(sameVariant(keptVariant, rereadVariant, comparedWords) && comparedWords == keptVariant.words.size() && sameVariant(keptVariant, rewrittenVariant, comparedWords) && comparedWords == keptVariant.words.size(), "draw cache identity: a table written back to its old words did not match by its words alone");
+    if (watched) require(writtenVariant.shared.empty() && rereadVariant.shared.empty() && rewrittenVariant.shared.size() == 1u && !sameBlock(keptVariant, rewrittenVariant) && keptVariant.shared[0].block.expired(), "draw cache identity: a new column with the old words was taken for the replaced column");
+
+    const auto identityBlock = std::make_shared<const SrtGuestWords>(SrtGuestWords{{{0x1000u, 4u}}, {1u, 2u, 3u, 4u}});
+    const auto twinBlock = std::make_shared<const SrtGuestWords>(SrtGuestWords{{{0x1000u, 4u}}, {1u, 2u, 3u, 4u}});
+    const std::vector<std::shared_ptr<const SrtGuestWords>> identityBlocks{identityBlock, twinBlock};
+    const auto identityBytes = std::as_bytes(std::span(identityBlock->words));
+    const std::vector<std::uint32_t> copiedWords(identityBlock->words);
+    std::vector<AgcDriver::SharedWords> whole, tail, twin, copied, misaligned, nothing;
+    AgcDriver::AppendSharedWords(whole, 0u, 1u, identityBytes, identityBlocks);
+    AgcDriver::AppendSharedWords(tail, 0u, 2u, identityBytes.subspan(4u), identityBlocks);
+    AgcDriver::AppendSharedWords(twin, 0u, 1u, std::as_bytes(std::span(twinBlock->words)), identityBlocks);
+    AgcDriver::AppendSharedWords(copied, 0u, 1u, std::as_bytes(std::span(copiedWords)), identityBlocks);
+    AgcDriver::AppendSharedWords(misaligned, 0u, 1u, identityBytes.subspan(2u, 8u), identityBlocks);
+    AgcDriver::AppendSharedWords(nothing, 0u, 1u, identityBytes.subspan(0u, 0u), identityBlocks);
+    require(whole.size() == 1u && whole[0].first == 1u && whole[0].count == 4u && whole[0].offset == 0u && tail.size() == 1u && tail[0].first == 2u && tail[0].count == 3u && tail[0].offset == 1u && twin.size() == 1u && twin[0].block.lock() == twinBlock && copied.empty() && misaligned.empty() && nothing.empty(), "draw cache identity: words were credited to a block they were not copied from");
+    std::vector<AgcDriver::SharedWords> joined, gapped, crossed, skipped;
+    AgcDriver::AppendSharedWords(joined, 0u, 1u, identityBytes.subspan(0u, 8u), identityBlocks);
+    AgcDriver::AppendSharedWords(joined, 1u, 3u, identityBytes.subspan(8u), identityBlocks);
+    AgcDriver::AppendSharedWords(gapped, 0u, 1u, identityBytes.subspan(0u, 4u), identityBlocks);
+    AgcDriver::AppendSharedWords(gapped, 1u, 3u, identityBytes.subspan(4u, 4u), identityBlocks);
+    AgcDriver::AppendSharedWords(gapped, 2u, 4u, identityBytes.subspan(12u, 4u), identityBlocks);
+    AgcDriver::AppendSharedWords(crossed, 0u, 1u, identityBytes.subspan(0u, 4u), identityBlocks);
+    AgcDriver::AppendSharedWords(crossed, 1u, 2u, std::as_bytes(std::span(twinBlock->words)).subspan(4u, 4u), identityBlocks);
+    AgcDriver::AppendSharedWords(skipped, 0u, 1u, identityBytes.subspan(0u, 8u), identityBlocks);
+    AgcDriver::AppendSharedWords(skipped, 2u, 3u, identityBytes.subspan(8u), identityBlocks);
+    require(skipped.size() == 2u && skipped[0].region == 0u && skipped[0].regions == 1u && skipped[1].region == 2u && skipped[1].regions == 1u, "draw cache identity: words of regions that are not consecutive were joined");
+    require(joined.size() == 1u && joined[0].first == 1u && joined[0].count == 4u && joined[0].offset == 0u && joined[0].region == 0u && joined[0].regions == 2u && gapped.size() == 3u && gapped[1].first == 3u && gapped[1].offset == 1u && gapped[2].first == 4u && gapped[2].offset == 3u && crossed.size() == 2u && crossed[1].block.lock() == twinBlock, "draw cache identity: consecutive words of one block were not joined, or words of two places were");
+    const std::vector<std::uint32_t> leftWords{9u, 1u, 2u, 3u, 4u};
+    auto rightWords = leftWords;
+    std::size_t identityCompared = 0;
+    require(AgcDriver::SameCapturedWords(leftWords, whole, rightWords, whole, &identityCompared) && identityCompared == 1u, "draw cache identity: words copied from one block were compared");
+    require(AgcDriver::SameCapturedWords(leftWords, whole, rightWords, twin, &identityCompared) && identityCompared == leftWords.size(), "draw cache identity: equal words from two blocks did not match by their words");
+    rightWords[0] = 8u;
+    require(!AgcDriver::SameCapturedWords(leftWords, whole, rightWords, whole, &identityCompared) && identityCompared == 1u, "draw cache identity: a shared block hid a difference outside it");
+    rightWords = {9u, 1u, 2u, 3u, 5u};
+    const std::vector<AgcDriver::SharedWords> unowned{{1u, 4u, 0u, {}}};
+    const std::vector<AgcDriver::SharedWords> shifted{{1u, 4u, 1u, identityBlock}};
+    const std::vector<AgcDriver::SharedWords> beyond{{3u, 4u, 0u, identityBlock}};
+    auto doomed = std::make_shared<const SrtGuestWords>(SrtGuestWords{{{0x2000u, 4u}}, {1u, 2u, 3u, 4u}});
+    std::vector<AgcDriver::SharedWords> dead;
+    AgcDriver::AppendSharedWords(dead, 0u, 1u, std::as_bytes(std::span(doomed->words)), std::span<const std::shared_ptr<const SrtGuestWords>>(&doomed, 1u));
+    doomed.reset();
+    require(!AgcDriver::SameCapturedWords(leftWords, whole, rightWords, twin, &identityCompared) && identityCompared == leftWords.size(), "draw cache identity: different words from two blocks matched");
+    require(!AgcDriver::SameCapturedWords(leftWords, unowned, rightWords, unowned, &identityCompared) && identityCompared == leftWords.size(), "draw cache identity: runs without a block vouched for words");
+    require(!AgcDriver::SameCapturedWords(leftWords, whole, rightWords, shifted, &identityCompared) && identityCompared == leftWords.size(), "draw cache identity: runs at other offsets of one block vouched for words");
+    require(!AgcDriver::SameCapturedWords(leftWords, beyond, rightWords, beyond, &identityCompared) && identityCompared == leftWords.size(), "draw cache identity: a run past the words vouched for them");
+    require(dead.size() == 1u && dead[0].block.expired() && !AgcDriver::SameCapturedWords(leftWords, dead, rightWords, dead, &identityCompared) && identityCompared == leftWords.size(), "draw cache identity: a released block vouched for words");
+    require(!AgcDriver::SameCapturedWords(leftWords, whole, std::vector<std::uint32_t>(leftWords.begin(), leftWords.end() - 1), {}, &identityCompared) && identityCompared == 0u, "draw cache identity: captures of different sizes matched");
+
     const auto compiled = CompileTable(request);
     const auto& map = TableMap(*compiled.result);
     const auto codes = TableCodes(map, 0);
@@ -1365,14 +1564,36 @@ void verifyTableSnapshotReuse() {
         require((codes[key] & Abi::PoisonFlag) == 0u && std::equal(heap + static_cast<std::size_t>(key) * 8u, heap + static_cast<std::size_t>(key) * 8u + 8u, images->guestDescriptor.begin() + static_cast<std::ptrdiff_t>(element) * 8), ("table snapshot reuse: key " + std::to_string(key) + " selects the wrong record of a reused table").c_str());
     }
 
+    const auto resolvedBefore = ResourceMaterializer::TableReuse();
+    alignas(16) static std::array<std::uint32_t, 4> otherOutput{};
+    const auto otherOutputV = TableV(otherOutput.data(), 0u, static_cast<std::uint32_t>(sizeof(otherOutput)));
+    const std::array<std::uint32_t, 4> outputV{guest.srt[12], guest.srt[13], guest.srt[14], guest.srt[15]};
+    std::copy(otherOutputV.begin(), otherOutputV.end(), guest.srt.begin() + 12);
+    const auto otherBuffer = CompileTable(request);
+    std::copy(outputV.begin(), outputV.end(), guest.srt.begin() + 12);
+    const auto resolvedShared = ResourceMaterializer::TableReuse();
+    require(otherBuffer.result != compiled.result && otherBuffer.result->variantId == compiled.result->variantId && resolvedShared.resolutionsShared - resolvedBefore.resolutionsShared == 1u && resolvedShared.resolutions == resolvedBefore.resolutions && TableMap(*otherBuffer.result) == map && TableBinding(*otherBuffer.result, DescriptorRole::ImageTable)->guestDescriptor == images->guestDescriptor, "table snapshot reuse: a result for another buffer did not reuse the unchanged table's resolution");
+    const auto storedWord = heap[5u * 8u];
+    heap[5u * 8u] = heap[6u * 8u];
+    const auto restoredMap = map;
+    const auto storedTable = CompileTable(request);
+    heap[5u * 8u] = storedWord;
+    const auto resolvedStored = ResourceMaterializer::TableReuse();
+    require(resolvedStored.resolutionsShared == resolvedShared.resolutionsShared && resolvedStored.resolutions - resolvedShared.resolutions == 1u && TableMap(*storedTable.result) != restoredMap, "table snapshot reuse: a table changed by a CPU store took the resolution of its old words");
+    const auto& storedMap = TableMap(*storedTable.result);
+    const auto storedCodes = TableCodes(storedMap, 0);
+    const auto* storedImages = TableBinding(*storedTable.result, DescriptorRole::ImageTable);
+    const auto storedElement = storedMap.at(storedCodes.at(5u) + Abi::ImageRecordElement);
+    require(storedImages != nullptr && std::equal(heap + 6u * 8u, heap + 6u * 8u + 1u, storedImages->guestDescriptor.begin() + static_cast<std::ptrdiff_t>(storedElement) * 8) && std::equal(heap + 5u * 8u + 1u, heap + 5u * 8u + 8u, storedImages->guestDescriptor.begin() + static_cast<std::ptrdiff_t>(storedElement) * 8 + 1), "table snapshot reuse: the resolution after a CPU store does not hold the stored record");
+
     constexpr std::uint32_t SmallRecords = 64;
-    std::vector<std::vector<std::uint32_t>> small(17, std::vector<std::uint32_t>(SmallRecords * 8u + 1024u));
-    std::vector<TableGuest> smallGuests(small.size());
-    std::vector<std::array<std::uint32_t, 2>> smallUserData(small.size());
+    std::vector<std::vector<std::uint32_t>> smallStorage(17, std::vector<std::uint32_t>(SmallRecords * 8u + 1024u));
+    std::vector<TableGuest> smallGuests(smallStorage.size());
+    std::vector<std::array<std::uint32_t, 2>> smallUserData(smallStorage.size());
     std::vector<RecompileRequest> smallRequests;
     std::vector<std::uint32_t*> smallTables;
-    for (std::size_t index = 0; index < small.size(); index++) {
-        auto* table = reinterpret_cast<std::uint32_t*>((reinterpret_cast<std::uintptr_t>(small[index].data()) + 4095u) & ~std::uintptr_t{4095});
+    for (std::size_t index = 0; index < smallStorage.size(); index++) {
+        auto* table = reinterpret_cast<std::uint32_t*>((reinterpret_cast<std::uintptr_t>(smallStorage[index].data()) + 4095u) & ~std::uintptr_t{4095});
         fill(table, SmallRecords);
         FillTableSrt(smallGuests[index], table, 32u, SmallRecords);
         smallTables.push_back(table);
@@ -1386,13 +1607,325 @@ void verifyTableSnapshotReuse() {
     smallTables[0][5u * 8u] ^= 0x10u;
     const auto unwatchedStore = capture(smallRequests[0], smallTables[0], smallBytes, nullptr);
     require(unwatchedStore.reuse.compared == 0u && heldRecords(unwatchedStore, smallTables[0], SmallRecords), "table snapshot reuse: a store into a table outside the write watch was not seen");
-    for (std::size_t index = 1; index < small.size(); index++) static_cast<void>(capture(smallRequests[index], smallTables[index], smallBytes, nullptr));
+    for (std::size_t index = 1; index < smallStorage.size(); index++) static_cast<void>(capture(smallRequests[index], smallTables[index], smallBytes, nullptr));
     const auto evicted = capture(smallRequests[0], smallTables[0], smallBytes, nullptr);
     const auto kept = capture(smallRequests.back(), smallTables.back(), smallBytes, nullptr);
     require(evicted.reuse.unwritten == 0u && evicted.reuse.compared == 0u && kept.reuse.compared == 1u, "table snapshot reuse: the memo kept more than 16 columns");
+
+    constexpr std::uint32_t PagedRecords = 512;
+    constexpr std::size_t PagedBytes = PagedRecords * 32u;
+    auto* paged = AllocateWatchedTable(PagedBytes);
+    fill(paged, PagedRecords);
+    alignas(4096) static TableGuest pagedGuest;
+    FillTableSrt(pagedGuest, paged, 32u, PagedRecords);
+    const auto pagedUserData = TableUserData(pagedGuest);
+    const auto pagedRequest = TableRequest(TableWholeCode, pagedUserData);
+    const auto pagedFresh = capture(pagedRequest, paged, PagedBytes, nullptr);
+    const auto pagedReused = capture(pagedRequest, paged, PagedBytes, nullptr);
+    require(pagedFresh.reuse.columns == 1u && sharesColumn(pagedReused, pagedFresh) && heldRecords(pagedReused, paged, PagedRecords), "table snapshot reuse: an unchanged paged table did not share its column");
+    const auto hidden = reinterpret_cast<std::uintptr_t>(paged) + 4096u;
+    GuestArena::GuestArenaSetProtection_nid_postfix(hidden, 4096u, 0x01u);
+    GuestAllocations::GuestAllocationsInvalidate_nid_postfix(hidden, 4096u);
+    const auto pagedUnmapped = capture(pagedRequest, paged, PagedBytes, nullptr);
+    GuestArena::GuestArenaSetProtection_nid_postfix(hidden, 4096u, 0x04u);
+    GuestAllocations::GuestAllocationsInvalidate_nid_postfix(hidden, 4096u);
+    const auto unmappedCode = Abi::PoisonCode(static_cast<std::uint32_t>(Abi::PoisonReason::Unmapped));
+    const auto& hiddenCodes = pagedUnmapped.tables.tables.at(0).codes;
+    require(pagedUnmapped.reuse.unwritten == 0u && pagedUnmapped.reuse.shared == 0u && pagedUnmapped.reuse.columns == 1u && pagedUnmapped.supplied.shared == 0u && hiddenCodes.at(127u) != unmappedCode && hiddenCodes.at(128u) == unmappedCode && hiddenCodes.at(255u) == unmappedCode && hiddenCodes.at(256u) != unmappedCode, "table snapshot reuse: the records of an unmapped page were not read again as Unmapped poison");
+    require(heldRecords(pagedReused, paged, PagedRecords) && sharesColumn(pagedReused, pagedFresh), "table snapshot reuse: unmapping a page changed the column read before it");
+    const auto pagedBack = capture(pagedRequest, paged, PagedBytes, nullptr);
+    require(pagedBack.reuse.unwritten == 0u && pagedBack.reuse.compared == 1u && sharesColumn(pagedBack, pagedFresh) && heldRecords(pagedBack, paged, PagedRecords), "table snapshot reuse: the column read before a page was unmapped was not compared and shared once the page came back");
     std::cout << "table snapshot reuse " << (watched ? "with" : "without") << " write watching passed\n";
 }
 
+std::atomic<int> tableRacePhase{0};
+std::uint64_t tableRaceBegin = 0;
+std::uint64_t tableRaceBytes = 0;
+
+AgcDriver::ShaderMemory::PendingWrite RacingTableWrite(std::uint64_t address, std::size_t bytes, std::span<std::byte>) {
+    if (tableRaceBytes != 0u && address == tableRaceBegin && bytes >= tableRaceBytes) {
+        tableRaceBytes = 0;
+        tableRacePhase.store(1, std::memory_order_release);
+        while (tableRacePhase.load(std::memory_order_acquire) != 2) std::this_thread::yield();
+    }
+    return AgcDriver::ShaderMemory::PendingWrite::None;
+}
+
+void verifyTableStampRace() {
+    using namespace ShaderRecompiler;
+    constexpr std::uint32_t Records = 4096;
+    constexpr std::uint32_t Stride = 48;
+    constexpr std::size_t Bytes = static_cast<std::size_t>(Records) * Stride;
+    constexpr std::size_t RecordWords = Stride / 4u;
+    const auto texture = [](std::uint32_t index) { return TableT(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(0x100000000ull + index * 256ull)), TableFormat8888UNorm, TableType2D, 1u + index % 5u); };
+    auto* heap = AllocateWatchedTable(Bytes);
+    for (std::uint32_t record = 0; record < Records; record++) {
+        const auto words = texture(record % 1024u);
+        std::copy(words.begin(), words.end(), heap + record * RecordWords);
+        std::fill_n(heap + record * RecordWords + 8u, 4u, 0x5a5a5a5au);
+    }
+    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(heap));
+    if (AgcDriver::GuestMemory::CollectWrites(address, Bytes) == 0) {
+        std::cout << "table stamp race without write watching skipped\n";
+        return;
+    }
+    alignas(4096) static TableGuest guest;
+    FillTableSrt(guest, heap, Stride, Records);
+    const auto userData = TableUserData(guest);
+    auto wideCode = TableWholeCode;
+    std::replace(wideCode.begin(), wideCode.end(), 0x8f108510u, 0x9310b010u);
+    const auto request = TableRequest(wideCode, userData);
+    const auto capture = [&](AgcDriver::ShaderMemory::PendingWriteQuery pending) {
+        const auto before = ResourceMaterializer::TableReuse();
+        AgcDriver::ShaderMemory memory({}, pending);
+        static_cast<void>(memory.Capture(request));
+        const auto after = ResourceMaterializer::TableReuse();
+        return ImageTableReuseCounts{after.unwritten - before.unwritten, after.compared - before.compared, after.wordsRead - before.wordsRead, after.shared - before.shared, after.columns - before.columns};
+    };
+    tableRacePhase.store(0, std::memory_order_relaxed);
+    tableRaceBegin = address;
+    tableRaceBytes = Bytes - 16u;
+    std::thread writer([&] {
+        while (tableRacePhase.load(std::memory_order_acquire) != 1) std::this_thread::yield();
+        AgcDriver::GuestMemory::MarkWritten(address + 5u * Stride, 32u);
+        tableRacePhase.store(2, std::memory_order_release);
+    });
+    const auto first = capture(&RacingTableWrite);
+    writer.join();
+    require(tableRacePhase.load(std::memory_order_relaxed) == 2 && first.columns == 1u, "table stamp race: the racing store did not run while the table was stamped");
+    const auto next = capture(nullptr);
+    require(next.unwritten == 0u && next.compared == 1u, "table stamp race: a GPU write stamped while the table was stamped, before it was noted pending, was hidden by the table's stamp");
+    std::cout << "table stamp race with write watching passed\n";
+}
+
+void verifyTableVouch() {
+    using namespace ShaderRecompiler;
+    constexpr std::uint32_t Records = 4096;
+    constexpr std::uint32_t Stride = 48;
+    constexpr std::size_t Bytes = static_cast<std::size_t>(Records) * Stride;
+    constexpr std::size_t RecordWords = Stride / 4u;
+    const auto texture = [](std::uint32_t index) { return TableT(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(0x100000000ull + index * 256ull)), TableFormat8888UNorm, TableType2D, 1u + index % 5u); };
+    const auto fill = [&](std::uint32_t* heap) {
+        for (std::uint32_t record = 0; record < Records; record++) {
+            const auto words = texture(record % 1024u);
+            std::copy(words.begin(), words.end(), heap + record * RecordWords);
+            std::fill_n(heap + record * RecordWords + 8u, 4u, 0x5a5a5a5au);
+        }
+    };
+    struct Captured {
+        ImageTableReuseCounts reuse;
+        std::uint64_t calls = 0;
+        std::vector<std::uint64_t> addresses;
+        std::vector<std::size_t> counts;
+        std::vector<std::uint32_t> words;
+        std::vector<AgcDriver::SharedWords> shared;
+        std::vector<std::shared_ptr<const SrtGuestWords>> blocks;
+    };
+    const auto capture = [](const RecompileRequest& request, AgcDriver::ShaderMemory::PendingWriteQuery pending) {
+        Captured captured;
+        const auto before = ResourceMaterializer::TableReuse();
+        const auto calls = AgcDriver::ShaderMemory::Vouches().calls;
+        AgcDriver::ShaderMemory memory({}, pending);
+        static_cast<void>(memory.Capture(request));
+        captured.calls = AgcDriver::ShaderMemory::Vouches().calls - calls;
+        captured.blocks = memory.SuppliedBlocks();
+        for (const auto& region : memory.TakeRecentRegions()) {
+            const auto offset = captured.words.size();
+            captured.addresses.push_back(region.guestAddress);
+            captured.counts.push_back(region.bytes.size() / sizeof(std::uint32_t));
+            captured.words.resize(offset + region.bytes.size() / sizeof(std::uint32_t));
+            std::memcpy(captured.words.data() + offset, region.bytes.data(), region.bytes.size());
+            AgcDriver::AppendSharedWords(captured.shared, captured.addresses.size() - 1u, offset, region.bytes, captured.blocks);
+        }
+        const auto after = ResourceMaterializer::TableReuse();
+        captured.reuse = {after.unwritten - before.unwritten, after.compared - before.compared, after.wordsRead - before.wordsRead, after.shared - before.shared, after.columns - before.columns};
+        return captured;
+    };
+    const auto vouch = [](const Captured& captured, AgcDriver::ShaderMemory::PendingWriteQuery pending, std::vector<std::uint8_t>& vouched) {
+        std::vector<AgcDriver::VouchedRegions> ranges;
+        const auto words = AgcDriver::VouchSharedRegions(captured.shared, captured.addresses.size(), pending, ranges);
+        vouched.assign(captured.addresses.size(), 0u);
+        std::size_t covered = 0;
+        for (const auto& range : ranges) {
+            std::fill(vouched.begin() + static_cast<std::ptrdiff_t>(range.begin), vouched.begin() + static_cast<std::ptrdiff_t>(range.end), std::uint8_t{1});
+            for (auto index = range.begin; index < range.end; index++) covered += captured.counts[index];
+        }
+        return covered == words ? words : std::size_t{0};
+    };
+
+    auto* heap = AllocateWatchedTable(Bytes);
+    fill(heap);
+    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(heap));
+    const bool watched = AgcDriver::GuestMemory::CollectWrites(address, Bytes) != 0;
+    alignas(4096) static TableGuest guest;
+    FillTableSrt(guest, heap, Stride, Records);
+    const auto userData = TableUserData(guest);
+    auto wideCode = TableWholeCode;
+    std::replace(wideCode.begin(), wideCode.end(), 0x8f108510u, 0x9310b010u);
+    const auto request = TableRequest(wideCode, userData);
+    const auto tableWords = static_cast<std::size_t>(Records) * 8u;
+    const auto onlyTable = [&](const Captured& captured, const std::vector<std::uint8_t>& vouched) {
+        if (vouched.size() != captured.addresses.size()) return false;
+        for (std::size_t index = 0; index < vouched.size(); index++) {
+            if ((captured.addresses[index] >= address && captured.addresses[index] < address + Bytes) != (vouched[index] != 0u)) return false;
+        }
+        return true;
+    };
+
+    const auto fresh = capture(request, nullptr);
+    require(fresh.reuse.columns == 1u && fresh.reuse.wordsRead == tableWords, "table vouch: the first snapshot did not read the strided table once");
+    const auto unchanged = capture(request, nullptr);
+    if (watched) {
+        require(unchanged.reuse.unwritten == 1u && unchanged.reuse.wordsRead == 0u, "table vouch: an unwritten strided table was read again");
+        require(unchanged.calls == 1u, ("table vouch: an unwritten table of " + std::to_string(Records) + " strided records was vouched for in " + std::to_string(unchanged.calls) + " calls instead of one").c_str());
+        require(unchanged.blocks.size() == 1u && unchanged.blocks[0]->ordered && unchanged.blocks[0]->runs.size() == Records && unchanged.blocks[0]->low == address && unchanged.blocks[0]->high == address + static_cast<std::uint64_t>(Records - 1u) * Stride + 32u, "table vouch: the strided table's words were not kept as one ordered block over the span of its records");
+    } else {
+        require(unchanged.reuse.compared == 1u && unchanged.calls == 0u, "table vouch: an unchanged unwatched strided table was not compared");
+    }
+
+    std::vector<std::uint8_t> vouched;
+    const auto vouchesBefore = AgcDriver::ShaderMemory::Vouches();
+    const auto unchangedWords = vouch(unchanged, nullptr, vouched);
+    const auto vouchesAfter = AgcDriver::ShaderMemory::Vouches();
+    if (watched) {
+        require(unchangedWords == tableWords && onlyTable(unchanged, vouched), "table vouch: the regions of an unchanged shared table were not vouched for, or other regions were");
+        require(vouchesAfter.calls - vouchesBefore.calls == 1u && vouchesAfter.blocks - vouchesBefore.blocks == 1u && vouchesAfter.refused == vouchesBefore.refused && vouchesAfter.words - vouchesBefore.words == tableWords, "table vouch: one shared block was not vouched for once");
+    } else {
+        require(unchangedWords == 0u && unchanged.shared.empty() && std::none_of(vouched.begin(), vouched.end(), [](std::uint8_t flag) { return flag != 0u; }), "table vouch: an unwatched table's regions were vouched for");
+    }
+    if (!watched) {
+        std::cout << "table vouch without write watching passed\n";
+        return;
+    }
+    const auto refused = [&](const std::string& what, AgcDriver::ShaderMemory::PendingWriteQuery pending) {
+        const auto before = AgcDriver::ShaderMemory::Vouches();
+        const auto words = vouch(unchanged, pending, vouched);
+        const auto after = AgcDriver::ShaderMemory::Vouches();
+        require(words == 0u && std::none_of(vouched.begin(), vouched.end(), [](std::uint8_t flag) { return flag != 0u; }) && after.refused - before.refused == 1u, ("table vouch: " + what + ": the shared table was still vouched for").c_str());
+    };
+
+    pendingTableKnown = false;
+    pendingTableWord = address + 100u * Stride;
+    refused("a pending GPU write into a record", &PendingTableWrite);
+    pendingTableWord = address + 100u * Stride + 40u;
+    refused("a pending GPU write between two records", &PendingTableWrite);
+    pendingTableWord = address + Bytes + 4096u;
+    require(vouch(unchanged, &PendingTableWrite, vouched) == tableWords, "table vouch: a pending GPU write outside the table refused it");
+    pendingTableWord = 0;
+
+    constexpr std::size_t UnrelatedBytes = 65536;
+    void* unrelated = ::operator new(UnrelatedBytes, std::align_val_t{UnrelatedBytes});
+    {
+        GuestAllocations::Mutation mutation;
+        mutation.Add(unrelated, UnrelatedBytes, true, true, false);
+    }
+    refused("a changed guest mapping", nullptr);
+    {
+        GuestAllocations::Mutation mutation;
+        mutation.Remove(unrelated);
+    }
+    ::operator delete(unrelated, std::align_val_t{UnrelatedBytes});
+
+    const auto rewrite = [&](const std::string& what, const std::function<void()>& store, bool gap) {
+        auto held = capture(request, nullptr);
+        if (held.reuse.unwritten == 0u) held = capture(request, nullptr);
+        std::vector<std::uint8_t> heldVouched;
+        require(held.reuse.unwritten == 1u && vouch(held, nullptr, heldVouched) == tableWords, ("table vouch: " + what + ": the table was not vouched for before the store").c_str());
+        store();
+        const auto before = AgcDriver::ShaderMemory::Vouches();
+        require(vouch(held, nullptr, heldVouched) == 0u && AgcDriver::ShaderMemory::Vouches().refused - before.refused == 1u, ("table vouch: " + what + ": the table captured before the store was still vouched for").c_str());
+        const auto after = capture(request, nullptr);
+        if (gap) {
+            require(after.reuse.unwritten == 0u && after.reuse.compared == 1u && after.reuse.columns == 0u, ("table vouch: " + what + ": the table was not compared and kept").c_str());
+            require(vouch(held, nullptr, heldVouched) == tableWords, ("table vouch: " + what + ": a capture sharing the compared table was not vouched for again").c_str());
+            require(capture(request, nullptr).reuse.unwritten == 1u, ("table vouch: " + what + ": the compared table was not reused unwritten after").c_str());
+        } else {
+            require(after.reuse.unwritten == 0u && after.reuse.compared == 0u && after.reuse.columns == 1u, ("table vouch: " + what + ": the table was not read again").c_str());
+            require(vouch(held, nullptr, heldVouched) == 0u, ("table vouch: " + what + ": the replaced table was vouched for after it was read again").c_str());
+        }
+    };
+    std::uint32_t replacement = 3000u;
+    const auto record = [&](std::uint32_t index) { return address + static_cast<std::uint64_t>(index) * Stride; };
+    rewrite("a CPU store into a record", [&] { const auto words = texture(replacement++); std::memcpy(heap + 7u * RecordWords, words.data(), sizeof(words)); }, false);
+    rewrite("a driver store (CP write, DMA copy or fill)", [&] { const auto words = texture(replacement++); AgcDriver::GuestMemory::Write(record(1234u), std::as_bytes(std::span(words)), 4); }, false);
+    rewrite("a GPU result stored by the driver", [&] { const auto words = texture(replacement++); AgcDriver::GuestMemory::StoreOwnBytes(record(2048u), std::as_bytes(std::span(words))); }, false);
+    rewrite("a CPU store between two records", [&] { heap[300u * RecordWords + 9u] ^= 0x10u; }, true);
+    rewrite("a GPU write into an import (MarkWritten)", [&] { AgcDriver::GuestMemory::MarkWritten(record(3000u) + 32u, 16u); }, true);
+
+    {
+        auto held = capture(request, nullptr);
+        std::vector<std::uint8_t> heldVouched;
+        require(held.reuse.unwritten == 1u && !held.blocks.empty() && vouch(held, nullptr, heldVouched) == tableWords, "table vouch: the held table was not vouched for");
+        held.blocks.clear();
+        heap[11u * RecordWords + 2u] ^= 0x10u;
+        const auto replaced = capture(request, nullptr);
+        heap[11u * RecordWords + 2u] ^= 0x10u;
+        const auto before = AgcDriver::ShaderMemory::Vouches();
+        require(replaced.reuse.columns == 1u && held.shared.size() == 1u && held.shared[0].block.expired() && vouch(held, nullptr, heldVouched) == 0u && AgcDriver::ShaderMemory::Vouches().calls == before.calls, "table vouch: a released table block was vouched for");
+        static_cast<void>(capture(request, nullptr));
+    }
+
+    std::cout << "table vouch with write watching passed\n";
+}
+
+std::uint64_t spanQueryBegin = 0;
+std::uint64_t spanQueryEnd = 0;
+std::uint64_t spanQueriesToPages = 0;
+bool spanQueryQueued = false;
+
+AgcDriver::ShaderMemory::PendingWrite PageQuery(std::uint64_t address, std::size_t bytes, std::span<std::byte>) {
+    if (address <= spanQueryBegin && address + bytes >= spanQueryEnd) ++spanQueriesToPages;
+    return AgcDriver::ShaderMemory::PendingWrite::None;
+}
+
+AgcDriver::ShaderMemory::PendingWrite QueuedSpanQuery(std::uint64_t address, std::size_t bytes, std::span<std::byte>) {
+    return spanQueryQueued && address < spanQueryEnd && spanQueryBegin < address + bytes ? AgcDriver::ShaderMemory::PendingWrite::Sync : AgcDriver::ShaderMemory::PendingWrite::None;
+}
+
+void verifyTableSpanQuery() {
+    using namespace ShaderRecompiler;
+    constexpr std::uint32_t Records = 4096;
+    constexpr std::uint32_t Stride = 48;
+    constexpr std::size_t Bytes = static_cast<std::size_t>(Records) * Stride;
+    constexpr std::size_t RecordWords = Stride / 4u;
+    auto* heap = AllocateWatchedTable(Bytes);
+    for (std::uint32_t record = 0; record < Records; record++) {
+        const auto words = TableT(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(0x100000000ull + (record % 1024u) * 256ull)), TableFormat8888UNorm, TableType2D, 1u + record % 5u);
+        std::copy(words.begin(), words.end(), heap + record * RecordWords);
+        std::fill_n(heap + record * RecordWords + 8u, 4u, 0x5a5a5a5au);
+    }
+    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(heap));
+    if (AgcDriver::GuestMemory::CollectWrites(address, Bytes) == 0) {
+        std::cout << "table span query without write watching skipped\n";
+        return;
+    }
+    alignas(4096) static TableGuest guest;
+    FillTableSrt(guest, heap, Stride, Records);
+    auto wideCode = TableWholeCode;
+    std::replace(wideCode.begin(), wideCode.end(), 0x8f108510u, 0x9310b010u);
+    const auto request = TableRequest(wideCode, TableUserData(guest));
+    spanQueryBegin = address;
+    spanQueryEnd = address + static_cast<std::uint64_t>(Records - 1u) * Stride + 32u;
+    spanQueriesToPages = 0;
+    spanQueryQueued = false;
+    const auto capture = [&] {
+        const auto before = ResourceMaterializer::TableReuse();
+        AgcDriver::ShaderMemory memory({}, &PageQuery, nullptr, nullptr, &QueuedSpanQuery);
+        static_cast<void>(memory.Capture(request));
+        const auto after = ResourceMaterializer::TableReuse();
+        return ImageTableReuseCounts{after.unwritten - before.unwritten, after.compared - before.compared, after.wordsRead - before.wordsRead, after.shared - before.shared, after.columns - before.columns};
+    };
+    require(capture().columns == 1u, "table span query: the first capture did not read the table");
+    require(capture().unwritten == 1u, "table span query: the unwritten table was not reused through the span query");
+    spanQueryQueued = true;
+    const auto queued = capture();
+    require(queued.unwritten == 0u && queued.compared == 1u && queued.columns == 0u, "table span query: a write queued over the table's span did not make the capture compare the table");
+    spanQueryQueued = false;
+    require(capture().unwritten == 1u, "table span query: the table was not reused unwritten once the queued write was gone");
+    require(spanQueriesToPages == 0u, ("table span query: " + std::to_string(spanQueriesToPages) + " whole-span checks went to the page query, which waits for queued draws").c_str());
+    std::cout << "table span query passed\n";
+}
+
 void verifyImageTables() {
     using namespace ShaderRecompiler;
     namespace Abi = ImageTableAbi;
@@ -1429,7 +1962,7 @@ void verifyImageTables() {
     const auto& snapshot = tables.tables[0];
     require(snapshot.base == static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(heap.data())) && snapshot.size == 128u && snapshot.records == 4u && snapshot.keys == 4u && snapshot.fault == 0u && !snapshot.outside, "image tables: the column snapshot is wrong");
     require(snapshot.codes.size() == 4u && snapshot.codes[0] == snapshot.codes[3] && snapshot.codes[0] != snapshot.codes[1] && snapshot.codes[2] == Abi::PoisonCode(static_cast<std::uint32_t>(Abi::PoisonReason::OutsideDomain)), "image tables: the narrowed codes are wrong");
-    require(tables.words.at(snapshot.codes[0]).dwords == heap[0] && tables.words.at(snapshot.codes[1]).dwords == heap[1], "image tables: the snapshot words are wrong");
+    require(snapshot.words.at(snapshot.codes[0]).dwords == heap[0] && snapshot.words.at(snapshot.codes[1]).dwords == heap[1], "image tables: the snapshot words are wrong");
     for (const auto& material : materials) require(TableCovered(compiled.regions, &material[1], sizeof(std::uint32_t)), "image tables: a material key was not captured");
     for (const auto entry : {0u, 1u, 3u}) require(TableCovered(compiled.regions, heap[entry].data(), 32u), "image tables: a table entry was not captured");
     const auto& result = *compiled.result;
@@ -1523,7 +2056,7 @@ void verifyImageTables() {
     const auto partial = CompileTable(request);
     const auto& partialTables = partial.capture->snapshot.tables;
     require(partialTables.tables[0].keys == 4u && partialTables.tables[0].size == 100u, "image tables: a partial table has the wrong record count");
-    const auto& tail = partialTables.words.at(partialTables.tables[0].codes.at(3));
+    const auto& tail = partialTables.tables[0].words.at(partialTables.tables[0].codes.at(3));
     require(tail.dwords[0] == heap[3][0] && std::all_of(tail.dwords.begin() + 1, tail.dwords.end(), [](std::uint32_t word) { return word == 0u; }), "image tables: the straddling record's tail is not zero");
     const auto& partialMap = TableMap(*partial.result);
     require((partialMap.at(TableCodes(partialMap, 0).at(3) + Abi::ImageRecordFlags) & Abi::PoisonFlag) != 0u, "image tables: a straddling record was bound");
@@ -1686,7 +2219,7 @@ void verifyImageTableSrtKey() {
     const auto compiled = CompileTable(request);
     const auto& tables = compiled.capture->snapshot.tables;
     const auto& snapshot = tables.tables.at(0);
-    require(snapshot.records == 1u && snapshot.keys == 1u && snapshot.codes.size() == 1u && (snapshot.codes[0] & Abi::PoisonFlag) == 0u && tables.words.at(snapshot.codes[0]).dwords == heap[2], "image table SRT key: the snapshot does not hold the selected record alone");
+    require(snapshot.records == 1u && snapshot.keys == 1u && snapshot.codes.size() == 1u && (snapshot.codes[0] & Abi::PoisonFlag) == 0u && snapshot.words.at(snapshot.codes[0]).dwords == heap[2], "image table SRT key: the snapshot does not hold the selected record alone");
     const auto* images = TableBinding(*compiled.result, DescriptorRole::ImageTable);
     require(images != nullptr && images->count == 1u, "image table SRT key: the binding holds other entries than the selected one");
     require(TableCovered(compiled.regions, &guest.srt[16], sizeof(std::uint32_t)) && TableCovered(compiled.regions, &heap[2], sizeof(heap[2])), "image table SRT key: the key or the selected record was not captured");
@@ -1694,7 +2227,7 @@ void verifyImageTableSrtKey() {
     guest.srt[16] = 3u;
     const auto moved = CompileTable(request);
     const auto& movedTables = moved.capture->snapshot.tables;
-    require(movedTables.words.at(movedTables.tables.at(0).codes.at(0)).dwords == heap[3] && TableCovered(moved.regions, &heap[3], sizeof(heap[3])) && !TableCovered(moved.regions, &heap[2], sizeof(std::uint32_t)) && moved.result->spirv == compiled.result->spirv && moved.key == compiled.key, "image table SRT key: a changed key did not move the selected record alone");
+    require(movedTables.tables.at(0).words.at(movedTables.tables.at(0).codes.at(0)).dwords == heap[3] && TableCovered(moved.regions, &heap[3], sizeof(heap[3])) && !TableCovered(moved.regions, &heap[2], sizeof(std::uint32_t)) && moved.result->spirv == compiled.result->spirv && moved.key == compiled.key, "image table SRT key: a changed key did not move the selected record alone");
 }
 
 void verifyImageTableMaskedOffset() {
@@ -1852,7 +2385,7 @@ void verifyPointerImageTable() {
     const auto& tables = compiled.capture->snapshot.tables;
     const auto& snapshot = tables.tables.at(0);
     require(snapshot.base == base && snapshot.size == 0u && snapshot.keys == Entries && snapshot.records == Entries && !snapshot.outside && snapshot.fault == 0u, "pointer image table: the column snapshot is wrong");
-    require(snapshot.codes[0] == snapshot.codes[1] && snapshot.codes[2] == snapshot.codes[255] && snapshot.codes[0] != snapshot.codes[2] && tables.words.at(snapshot.codes[2]).dwords[0] == root[(Palette + 2u * 32u) / 4u], "pointer image table: the snapshot words are wrong");
+    require(snapshot.codes[0] == snapshot.codes[1] && snapshot.codes[2] == snapshot.codes[255] && snapshot.codes[0] != snapshot.codes[2] && snapshot.words.at(snapshot.codes[2]).dwords[0] == root[(Palette + 2u * 32u) / 4u], "pointer image table: the snapshot words are wrong");
     require(tables.ranges == std::vector<std::pair<std::uint64_t, std::uint64_t>>{{base + Palette, Entries * 32u}}, "pointer image table: the snapshot range is wrong");
     require(snapshot.readRanges == tables.ranges && compiled.result->imageTableReadRanges.size() == 1u && compiled.result->imageTableReadRanges[0] == snapshot.readRanges, "pointer image table: the consumed ranges did not retain table ownership");
     require(TableCovered(compiled.regions, root + Palette / 4u, Entries * 32u) && !TableTouched(compiled.regions, root + (Palette + Entries * 32u) / 4u, Committed - Palette - Entries * 32u), "pointer image table: the captured regions are wrong");
@@ -1973,7 +2506,7 @@ void verifyPointerTableOffsetCarry() {
     const auto compiled = CompileTable(request);
     const auto& snapshot = compiled.capture->snapshot.tables.tables.at(0);
     require(snapshot.keys == 256u && snapshot.codes[7] == Abi::PoisonCode(static_cast<std::uint32_t>(Abi::PoisonReason::OffsetCarry)), "pointer table offset carry: the record whose second load wraps past 32 bits is not OffsetCarry poison");
-    require((snapshot.codes[6] & Abi::PoisonFlag) == 0u && (snapshot.codes[8] & Abi::PoisonFlag) == 0u && compiled.capture->snapshot.tables.words.at(snapshot.codes[6]).dwords[0] == root[0x100000010ull / 4u] && compiled.capture->snapshot.tables.words.at(snapshot.codes[8]).dwords[0] == root[0x50u / 4u] && root[0x100000010ull / 4u] != root[0x50u / 4u], "pointer table offset carry: the records on either side of the carry were not resolved");
+    require((snapshot.codes[6] & Abi::PoisonFlag) == 0u && (snapshot.codes[8] & Abi::PoisonFlag) == 0u && snapshot.words.at(snapshot.codes[6]).dwords[0] == root[0x100000010ull / 4u] && snapshot.words.at(snapshot.codes[8]).dwords[0] == root[0x50u / 4u] && root[0x100000010ull / 4u] != root[0x50u / 4u], "pointer table offset carry: the records on either side of the carry were not resolved");
     require(!TableTouched(compiled.regions, root + 0x100000030ull / 4u, 0x20u), "pointer table offset carry: the record whose second load wraps was read");
 #ifdef _WIN32
     VirtualFree(block, 0, MEM_RELEASE);
@@ -5304,6 +5837,9 @@ int main(int argc, char** argv) {
         }
         if (argc == 2 && std::string_view(argv[1]) == "--table-snapshot-reuse") {
             verifyTableSnapshotReuse();
+            verifyTableStampRace();
+            verifyTableVouch();
+            verifyTableSpanQuery();
             return 0;
         }
         if (argc == 2 && std::string_view(argv[1]) == "--srt-read-patching") {
diff --git a/core/libs/prx/libSceAgcDriver/tests/WriteTracking.cpp b/core/libs/prx/libSceAgcDriver/tests/WriteTracking.cpp
index 73a60b7f4..546d712f8 100644
--- a/core/libs/prx/libSceAgcDriver/tests/WriteTracking.cpp
+++ b/core/libs/prx/libSceAgcDriver/tests/WriteTracking.cpp
@@ -183,6 +183,39 @@ void CheckNewestStamp() {
     Require(NewestStamp(base + 3 * Block - 8, Block + 8) > partial, "a CPU store into the watched part of a partly watched range did not advance its stamp");
 }
 
+void CheckPartlyWatchedStore() {
+    void* memory = AllocateWatched(3 * Block);
+    const auto base = reinterpret_cast<std::uint64_t>(memory);
+    const auto stored = base + Block;
+    const auto unwatched = base + 2 * Block;
+    std::memset(memory, 0x11, 3 * Block);
+    BumpCollectEpoch();
+    Unwatch(unwatched, Block);
+    const auto before = CollectWrites(base, 2 * Block);
+    Require(before != 0 && UnchangedSince(base, 2 * Block, before), "the watched blocks were not collected");
+    Require(MarkWritten(unwatched - 64, 128) == 0, "a driver store into partly watched memory is trusted");
+    Require(!UnchangedSince(stored, Block, before), "a driver store into partly watched memory left its watched block unchanged");
+    Require(StoredOver(unwatched - 64, 64, before), "a driver store into partly watched memory is not seen over its watched bytes");
+    Require(!StoredOver(stored, Block - 64, before), "a driver store into partly watched memory is seen over watched bytes it did not store");
+    Require(UnchangedSince(base, Block, before), "a driver store into partly watched memory stamped a block it did not store into");
+    const auto beforeOwn = TrackerGeneration();
+    Require(StoreOwnBytes(unwatched - 64, 128, [&] { std::memset(reinterpret_cast<void*>(unwatched - 64), 0x22, 128); }) == 0, "an own store into partly watched memory is trusted");
+    Require(!UnchangedSince(stored, Block, beforeOwn), "an own store into partly watched memory left its watched block unchanged");
+    Require(UnchangedSince(base, Block, beforeOwn), "an own store into partly watched memory stamped a block it did not store into");
+#ifndef _WIN32
+    void* split = AllocateWatched(Block);
+    const auto splitBase = reinterpret_cast<std::uint64_t>(split);
+    const auto watchedHalf = splitBase + Block / 2;
+    std::memset(split, 0x11, Block);
+    Unwatch(splitBase, Block / 2);
+    const auto beforeSplit = CollectWrites(watchedHalf, Block / 2);
+    Require(beforeSplit != 0 && UnchangedSince(watchedHalf, Block / 2, beforeSplit), "the watched half of a split block was not collected");
+    Require(MarkWritten(splitBase + 64, 128) == 0 && UnchangedSince(watchedHalf, Block / 2, beforeSplit), "a driver store into the unwatched half of a split block stamped its watched half");
+    Require(MarkWritten(watchedHalf - 64, 128) == 0, "a driver store across the halves of a split block is trusted");
+    Require(!UnchangedSince(watchedHalf, Block / 2, beforeSplit), "a driver store across the halves of a split block left its watched half unchanged");
+#endif
+}
+
 #ifdef _WIN32
 void CheckDeferredSharedImports() {
     constexpr std::size_t bytes = 4 * Block;
@@ -814,6 +847,7 @@ int main() {
         CheckOwnStore();
         CheckUnwatch();
         CheckNewestStamp();
+        CheckPartlyWatchedStore();
 #ifdef _WIN32
         CheckDeferredSharedImports();
         CheckPrivateMappingReuse();
diff --git a/core/libs/prx/libSceAgcDriver/tests/execution/ImageTableCacheBounds.cpp b/core/libs/prx/libSceAgcDriver/tests/execution/ImageTableCacheBounds.cpp
index d95da537b..6a69d7206 100644
--- a/core/libs/prx/libSceAgcDriver/tests/execution/ImageTableCacheBounds.cpp
+++ b/core/libs/prx/libSceAgcDriver/tests/execution/ImageTableCacheBounds.cpp
@@ -1,5 +1,10 @@
 #include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
 #include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
+#include "prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawPipeline.hpp"
+#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
+#include "prx/libSceAgcDriver/Execution/include/ShaderMemory.hpp"
+#include "Optimization/ResourceMaterializer.hpp"
+#include "Optimization/SrtWalker.hpp"
 #include "prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp"
 #include "prx/libSceAgcDriver/Submit/include/Dcb.hpp"
 #include "prx/libc/include/GuestAllocations.hpp"
@@ -16,6 +21,8 @@
 #endif
 #include <algorithm>
 #include <array>
+#include <atomic>
+#include <chrono>
 #include <cstddef>
 #include <cstdint>
 #include <cstdio>
@@ -23,7 +30,10 @@
 #include <cstring>
 #include <initializer_list>
 #include <iostream>
+#include <span>
 #include <string>
+#include <thread>
+#include <tuple>
 #include <vector>
 
 namespace {
@@ -47,6 +57,12 @@ constexpr std::size_t HeapOffset = OutputOffset + 4096;
 constexpr std::size_t TexturesOffset = HeapOffset + static_cast<std::size_t>(Records) * Stride;
 constexpr std::size_t BlockBytes = (TexturesOffset + static_cast<std::size_t>(Surfaces) * TextureBytes + 65535) & ~static_cast<std::size_t>(65535);
 
+constexpr std::uint32_t WideStride = 48;
+constexpr std::uint32_t WideRecords = 32768;
+constexpr std::size_t WideKernelOffset = 2048;
+constexpr std::size_t WideHeapOffset = 65536;
+constexpr std::array<std::size_t, 3> WideSlots{SrtSlots - 1u, SrtSlots - 2u, SrtSlots - 3u};
+constexpr std::array<std::uint32_t, 17> WideTableCode{0xf4080100u, 0xfa000000u, 0xf4080200u, 0xfa000010u, 0xf4080300u, 0xfa000020u, 0xf4080700u, 0xfa000030u, 0x7e200500u, 0x9310b010u, 0xf42c0502u, 0x20000000u, 0xf09c0f08u, 0x00450000u, 0xe0700000u, 0x80070000u, 0xbf810000u};
 constexpr std::array<std::uint32_t, 17> TableCode{0xf4080100u, 0xfa000000u, 0xf4080200u, 0xfa000010u, 0xf4080300u, 0xfa000020u, 0xf4080700u, 0xfa000030u, 0x7e200500u, 0x8f108510u, 0xf42c0502u, 0x20000000u, 0xf09c0f08u, 0x00450000u, 0xe0700000u, 0x80070000u, 0xbf810000u};
 
 class GuestBlock {
@@ -97,10 +113,9 @@ std::vector<std::uint32_t> Pm4(std::uint32_t opcode, std::initializer_list<std::
     return words;
 }
 
-void Dispatch(const GuestBlock& block, std::size_t slot) {
-    const auto kernel = block.At(0);
+void Dispatch(const GuestBlock& block, std::size_t slot, std::size_t kernelOffset = 0, std::vector<std::uint32_t> words = {}) {
+    const auto kernel = block.At(kernelOffset);
     const auto srt = block.At(SrtOffset + slot * SrtBytes);
-    std::vector<std::uint32_t> words;
     for (const auto& packet : {Pm4(0x76, {0x20c, static_cast<std::uint32_t>(kernel >> 8u)}), Pm4(0x76, {0x20d, static_cast<std::uint32_t>(kernel >> 40u) & 0xffu}), Pm4(0x76, {0x207, 64}), Pm4(0x76, {0x208, 1}), Pm4(0x76, {0x209, 1}), Pm4(0x76, {0x213, 2u << 1u}), Pm4(0x76, {0x240, static_cast<std::uint32_t>(srt)}), Pm4(0x76, {0x241, static_cast<std::uint32_t>(srt >> 32u)}), Pm4(0x15, {1, 1, 1, 0x41})}) {
         words.insert(words.end(), packet.begin(), packet.end());
     }
@@ -150,6 +165,172 @@ void SrtMovingPerDispatch(const GuestBlock& block) {
     Require(most <= Driver::DispatchCacheBytes(), "image table cache bounds: dispatches reading a 2 MiB table through " + std::to_string(SrtSlots) + " SRTs kept " + std::to_string(most >> 20u) + " MiB of captured words in the dispatch cache, over its budget of " + std::to_string(Driver::DispatchCacheBytes() >> 20u) + " MiB");
 }
 
+std::array<std::uint32_t, 8> TextureWords(const GuestBlock& block, std::uint32_t element) {
+    const auto address = block.At(TexturesOffset + static_cast<std::size_t>(element / Aliases) * TextureBytes);
+    return {static_cast<std::uint32_t>(address >> 8u), static_cast<std::uint32_t>((address >> 40u) & 0xffu) | (Format8888UNorm << 20u) | (3u << 30u), 7u << 14u, 0xfacu | (Type2D << 28u), 0u, 0u, element % Aliases, 0u};
+}
+
+std::uint64_t WideRecord(const GuestBlock& block, std::uint32_t record) {
+    return block.At(WideHeapOffset + static_cast<std::size_t>(record) * WideStride);
+}
+
+void SetWideRecord(const GuestBlock& block, std::uint32_t record, std::uint32_t element) {
+    const auto words = TextureWords(block, element);
+    std::memcpy(block.Bytes(WideHeapOffset + static_cast<std::size_t>(record) * WideStride), words.data(), sizeof(words));
+}
+
+void VouchedLookups(const GuestBlock& block) {
+    using AgcDriver::ShaderMemory;
+    using ShaderRecompiler::ResourceMaterializer;
+    std::memcpy(block.Bytes(WideKernelOffset), WideTableCode.data(), sizeof(WideTableCode));
+    for (std::uint32_t record = 0; record < WideRecords; ++record) {
+        SetWideRecord(block, record, (1u + record % 15u) * Aliases);
+        std::memset(block.Bytes(WideHeapOffset + static_cast<std::size_t>(record) * WideStride + 32u), 0x5a, WideStride - 32u);
+    }
+    for (const auto slot : WideSlots) {
+        auto* srt = reinterpret_cast<std::uint32_t*>(block.Bytes(SrtOffset + slot * SrtBytes));
+        std::fill(srt, srt + SrtBytes / 4u, 0u);
+        const auto heap = V(block.At(WideHeapOffset), WideStride, WideRecords);
+        const auto output = V(block.At(OutputOffset), 0u, 4096u);
+        std::copy(heap.begin(), heap.end(), srt);
+        std::copy(output.begin(), output.end(), srt + 12);
+    }
+    struct Step {
+        std::uint64_t vouched = 0;
+        std::uint64_t refused = 0;
+        std::uint64_t columns = 0;
+        std::uint64_t reused = 0;
+    };
+    const auto dispatch = [&](std::size_t slot, std::vector<std::uint32_t> before = {}) {
+        const auto vouches = ShaderMemory::Vouches();
+        const auto reuse = ResourceMaterializer::TableReuse();
+        Dispatch(block, slot, WideKernelOffset, std::move(before));
+        const auto after = ShaderMemory::Vouches();
+        const auto reused = ResourceMaterializer::TableReuse();
+        return Step{after.blocks - vouches.blocks, after.refused - vouches.refused, reused.columns - reuse.columns, (reused.unwritten - reuse.unwritten) + (reused.compared - reuse.compared)};
+    };
+    const auto text = [](const Step& step) { return " (" + std::to_string(step.vouched) + " vouched, " + std::to_string(step.refused) + " refused, " + std::to_string(step.columns) + " columns read, " + std::to_string(step.reused) + " reused)"; };
+    const auto read = dispatch(WideSlots[0]);
+    Require(read.columns == 1u, "image table vouched lookups: the first dispatch did not read the strided table" + text(read));
+    const auto shared = dispatch(WideSlots[1]);
+    Require(shared.columns == 0u && shared.reused == 1u, "image table vouched lookups: a second dispatch over the unwritten table did not reuse its column" + text(shared));
+    const auto hit = dispatch(WideSlots[1]);
+    Require(hit.vouched >= 1u && hit.refused == 0u && hit.columns == 0u && hit.reused == 0u, "image table vouched lookups: a cached dispatch over an unwritten table was not validated by its table's vouch" + text(hit));
+
+    std::size_t vouchedSlot = WideSlots[1];
+    std::size_t otherSlot = WideSlots[0];
+    const auto recapture = [&](const std::string& what, std::vector<std::uint32_t> before = {}) {
+        const auto changed = dispatch(vouchedSlot, std::move(before));
+        Require(changed.refused >= 1u && changed.columns == 1u, "image table vouched lookups: " + what + " was hidden by the cached dispatch's vouch" + text(changed));
+        const auto other = dispatch(otherSlot);
+        Require(other.columns == 0u && other.reused == 1u, "image table vouched lookups: the other dispatch after " + what + " did not reuse the column read again" + text(other));
+        const auto again = dispatch(otherSlot);
+        Require(again.vouched >= 1u && again.refused == 0u && again.columns == 0u && again.reused == 0u, "image table vouched lookups: the dispatch after " + what + " was not vouched for again" + text(again));
+        std::swap(vouchedSlot, otherSlot);
+    };
+    SetWideRecord(block, 0u, 9u * Aliases);
+    recapture("a CPU store into a record");
+
+    const auto words = TextureWords(block, 10u * Aliases);
+    const auto target = WideRecord(block, 0u);
+    std::vector<std::uint32_t> cpWrite{0xc0000000u | ((3u + static_cast<std::uint32_t>(words.size()) - 1u) << 16u) | (0x37u << 8u), 0x00100200u, static_cast<std::uint32_t>(target), static_cast<std::uint32_t>(target >> 32u)};
+    cpWrite.insert(cpWrite.end(), words.begin(), words.end());
+    recapture("a GPU write into a record pending ahead of the dispatch", cpWrite);
+    Require(std::memcmp(block.Bytes(WideHeapOffset), words.data(), sizeof(words)) == 0, "image table vouched lookups: the GPU write did not land in the record");
+
+    const auto driverWords = TextureWords(block, 11u * Aliases);
+    AgcDriver::GuestMemory::Write(WideRecord(block, 0u), std::as_bytes(std::span(driverWords)), 4);
+    recapture("a driver store into a record");
+
+    *reinterpret_cast<std::uint32_t*>(block.Bytes(WideHeapOffset + 36u)) ^= 0x10u;
+    const auto gap = dispatch(vouchedSlot);
+    Require(gap.refused >= 1u && gap.columns == 0u && gap.reused == 0u, "image table vouched lookups: a store between two records did not refuse the vouch, or the table was captured again" + text(gap));
+    const auto compared = dispatch(WideSlots[2]);
+    Require(compared.columns == 0u && compared.reused == 1u, "image table vouched lookups: a new dispatch after a store between two records did not compare and keep the column" + text(compared));
+    const auto restamped = dispatch(vouchedSlot);
+    Require(restamped.vouched >= 1u && restamped.refused == 0u && restamped.columns == 0u, "image table vouched lookups: a dispatch sharing a column compared after a store between two records was not vouched for again" + text(restamped));
+    std::puts("image table vouched lookups passed");
+}
+
+struct QueuedDraw {
+    std::atomic<bool> release{false};
+
+    void Enqueue(std::uint64_t begin, std::uint64_t end) {
+        AgcDriver::DriverDetail::DrawPipeline::Queue0().Enqueue([this] {
+            while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
+        }, {{begin, end}});
+    }
+};
+
+void VouchUnderQueuedDraws(const GuestBlock& block) {
+    using AgcDriver::DriverDetail::DrawPipeline;
+    using AgcDriver::DriverDetail::Driver;
+    Require(DrawPipeline::Depth() >= 3u, "image table vouch under queued draws: the draw pipeline is off");
+    for (std::uint32_t record = 0; record < WideRecords; ++record) SetWideRecord(block, record, (1u + record % 15u) * Aliases);
+    const auto low = WideRecord(block, 0u);
+    const auto bytes = static_cast<std::size_t>(WideRecord(block, WideRecords - 1u) + 32u - low);
+    const ShaderRecompiler::SrtWriteStamp stamp{AgcDriver::GuestMemory::CollectWrites(low, bytes), GuestAllocations::GuestAllocationsGeneration_nid_postfix()};
+    Require(stamp.generation != 0u, "image table vouch under queued draws: the table is not write-watched");
+    auto& pipeline = DrawPipeline::Queue0();
+    QueuedDraw elsewhere;
+    QueuedDraw between;
+    QueuedDraw into;
+    const auto releaseAll = [&] {
+        elsewhere.release = true;
+        between.release = true;
+        into.release = true;
+    };
+    DrawPipeline::Active() = true;
+    const auto vouch = [&](const std::string& what, bool expected) {
+        const auto outstanding = pipeline.Outstanding();
+        std::atomic<bool> finished{false};
+        std::atomic<bool> waited{false};
+        std::thread watchdog([&] {
+            const auto start = std::chrono::steady_clock::now();
+            while (!finished.load()) {
+                if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5)) {
+                    waited = true;
+                    releaseAll();
+                    return;
+                }
+                std::this_thread::sleep_for(std::chrono::milliseconds(1));
+            }
+        });
+        const bool vouched = AgcDriver::ShaderMemory::Vouched(&Driver::QueryPendingSpan, low, bytes, stamp);
+        finished = true;
+        watchdog.join();
+        const bool queued = !waited.load() && pipeline.Outstanding() == outstanding;
+        if (!queued || vouched != expected) releaseAll();
+        Require(queued, "image table vouch under queued draws: " + what + ": the vouch waited for the queued draws");
+        Require(vouched == expected, "image table vouch under queued draws: " + what + (expected ? ": the table was not vouched for" : ": the table was still vouched for"));
+    };
+    const auto settle = [&](std::size_t outstanding) {
+        const auto start = std::chrono::steady_clock::now();
+        while (pipeline.Outstanding() != outstanding) {
+            if (std::chrono::steady_clock::now() - start > std::chrono::seconds(20)) {
+                releaseAll();
+                Require(false, "image table vouch under queued draws: the released draws were never committed");
+            }
+            std::this_thread::sleep_for(std::chrono::milliseconds(1));
+        }
+    };
+    vouch("an idle pipeline", true);
+    elsewhere.Enqueue(block.At(OutputOffset), block.At(OutputOffset) + 4096u);
+    vouch("a queued draw writing outside the table", true);
+    between.Enqueue(low + 32u, low + WideStride);
+    vouch("a queued draw writing between two records", false);
+    into.Enqueue(WideRecord(block, 7u), WideRecord(block, 7u) + 32u);
+    elsewhere.release = true;
+    between.release = true;
+    settle(1u);
+    vouch("a queued draw writing into a record", false);
+    into.release = true;
+    pipeline.Drain(DrawPipeline::DrainReason::Capture);
+    vouch("committed draws that wrote nothing", true);
+    DrawPipeline::Active() = false;
+    std::puts("image table vouch under queued draws passed");
+}
+
 void SetBudgets() {
 #ifdef _WIN32
     _putenv_s("APS5_RESOURCE_CACHE_TABLE_ELEMENTS", "8192");
@@ -162,7 +343,7 @@ void SetBudgets() {
 
 }
 
-int main() {
+int main(int argc, char** argv) {
     SetBudgets();
     try {
         {
@@ -171,6 +352,16 @@ int main() {
         }
         GuestBlock block;
         Prepare(block);
+        if (argc == 2 && std::string(argv[1]) == "--vouched-lookups") {
+            VouchedLookups(block);
+            AgcDriverShutdown_nid_postfix();
+            return 0;
+        }
+        if (argc == 2 && std::string(argv[1]) == "--queued-draws") {
+            VouchUnderQueuedDraws(block);
+            AgcDriverShutdown_nid_postfix();
+            return 0;
+        }
         TablesChangingPerDispatch(block);
         SrtMovingPerDispatch(block);
         AgcDriverShutdown_nid_postfix();
diff --git a/core/libs/prx/libSceAgcDriver/tests/execution/ImageTableReuse.cpp b/core/libs/prx/libSceAgcDriver/tests/execution/ImageTableReuse.cpp
index cf52853a1..1a21c88ad 100644
--- a/core/libs/prx/libSceAgcDriver/tests/execution/ImageTableReuse.cpp
+++ b/core/libs/prx/libSceAgcDriver/tests/execution/ImageTableReuse.cpp
@@ -19,6 +19,7 @@
 #include <array>
 #include <bit>
 #include <cstdint>
+#include <cstdlib>
 #include <cstdio>
 #include <cstring>
 #include <iostream>
@@ -207,6 +208,18 @@ Step Since(const AgcDriver::Graphics::ImageTableReuseCounts& before) {
     return {now.reused - before.reused, now.resolved - before.resolved};
 }
 
+void ExpectWrites(const AgcDriver::Graphics::ImageTableReuseCounts& before, std::uint64_t kept, std::uint64_t made, const std::string& what) {
+    static const bool keptSets = std::getenv("APS5_KEPT_DESCRIPTOR_SETS") == nullptr || std::string(std::getenv("APS5_KEPT_DESCRIPTOR_SETS")) != "0";
+    const auto now = AgcDriver::Graphics::ImageTableReuses();
+    const auto keptNow = now.writesKept - before.writesKept;
+    const auto madeNow = now.writesMade - before.writesMade;
+    if (!keptSets) {
+        Require(keptNow == 0u && madeNow == kept + made, "image table reuse: " + what + ": table descriptors were kept with kept sets turned off");
+        return;
+    }
+    Require(keptNow == kept && madeNow == made, "image table reuse: " + what + ": " + std::to_string(keptNow) + " table descriptor writes kept and " + std::to_string(madeNow) + " made, expected " + std::to_string(kept) + " and " + std::to_string(made));
+}
+
 void Expect(const AgcDriver::Graphics::ImageTableReuseCounts& before, std::uint64_t reused, std::uint64_t resolved, const std::string& what) {
     const auto step = Since(before);
     Require(step.reused == reused && step.resolved == resolved, "image table reuse: " + what + ": " + std::to_string(step.reused) + " reused and " + std::to_string(step.resolved) + " resolved tables, expected " + std::to_string(reused) + " and " + std::to_string(resolved));
@@ -271,12 +284,15 @@ void RunTests(AgcDriver::VulkanDevice& device, const GuestBlock& block, const Gu
         return;
     }
     Expect(before, 2, 1, "three dispatches of one table in one epoch");
+    if (std::getenv("APS5_NO_TABLE_REPLAY") == nullptr) Require(AgcDriver::Graphics::ImageTableReuses().replayed - before.replayed == 2u, "image table reuse: three dispatches of one table in one epoch did not replay the bound table twice");
+    ExpectWrites(before, 3, 0, "three dispatches of one table in one epoch");
 
     before = AgcDriver::Graphics::ImageTableReuses();
     AgcDriver::Graphics::ClearCachedTextures(device.Device());
     Require(Dispatch(device, result, 7) == Row(block.Texels(7)), "image table reuse: the table after the texture cache was cleared sampled the wrong entry");
     Require(Dispatch(device, result, 9) == Row(block.Texels(9)), "image table reuse: the table resolved after the texture cache was cleared sampled the wrong entry");
     Expect(before, 1, 1, "a kept table whose textures the texture cache dropped");
+    ExpectWrites(before, 1, 1, "a kept table whose textures the texture cache dropped");
 
     before = AgcDriver::Graphics::ImageTableReuses();
     std::vector<float> stored(Width * Height);
@@ -303,6 +319,7 @@ void RunTests(AgcDriver::VulkanDevice& device, const GuestBlock& block, const Gu
     Require(Dispatch(device, moved, 19) == Row(block.Texels(Moved)), "image table reuse: the moved table sampled the wrong entry when reused");
     Require(Dispatch(device, result, 21) == Row(block.Texels(21)), "image table reuse: the original table sampled the wrong entry when reused");
     Expect(before, 2, 2, "two tables that differ in one record");
+    ExpectWrites(before, 0, 4, "two tables that differ in one record, alternating");
 
     SetOutput(block.Texels(13));
     const auto writer = Compile(device, block);
diff --git a/core/shader/recompiler/CMakeLists.txt b/core/shader/recompiler/CMakeLists.txt
index 139a11b4d..be58d85ca 100644
--- a/core/shader/recompiler/CMakeLists.txt
+++ b/core/shader/recompiler/CMakeLists.txt
@@ -266,6 +266,7 @@ function(add_shader_recompiler target runtimeTarget)
             LinkUserDataCalls
             FindTableCalls
             LinkCalls
+            LoadStamp
             WriteFileAtomically
     )
     set(shaderCacheManifest)
diff --git a/core/shader/recompiler/IntermediateRepresentation/include/IntermediateRepresentation/IrMetadata/ResourcePlan.hpp b/core/shader/recompiler/IntermediateRepresentation/include/IntermediateRepresentation/IrMetadata/ResourcePlan.hpp
index c769521f1..5de2d20ab 100644
--- a/core/shader/recompiler/IntermediateRepresentation/include/IntermediateRepresentation/IrMetadata/ResourcePlan.hpp
+++ b/core/shader/recompiler/IntermediateRepresentation/include/IntermediateRepresentation/IrMetadata/ResourcePlan.hpp
@@ -8,11 +8,15 @@
 #include "IntermediateRepresentation/IrMetadata/ResourceInfo.hpp"
 #include "IntermediateRepresentation/IrMetadata/ShaderInfo.hpp"
 #include "IntermediateRepresentation/IrMetadata/ShaderStage.hpp"
+#include <algorithm>
 #include <array>
+#include <atomic>
 #include <cstdint>
 #include <memory>
 #include <new>
+#include <span>
 #include <stdexcept>
+#include <type_traits>
 #include <utility>
 #include <vector>
 
@@ -25,6 +29,63 @@ struct DescriptorValue {
     bool operator==(const DescriptorValue& other) const = default;
 };
 
+template <typename T>
+class SharedValues {
+public:
+    SharedValues() = default;
+    explicit SharedValues(std::vector<T> values) : block(values.empty() ? nullptr : std::make_shared<const Block>(std::move(values))) {}
+
+    [[nodiscard]] std::size_t size() const { return block != nullptr ? block->values.size() : 0u; }
+    [[nodiscard]] bool empty() const { return block == nullptr; }
+    [[nodiscard]] const T* data() const { return block != nullptr ? block->values.data() : nullptr; }
+    [[nodiscard]] const T* begin() const { return data(); }
+    [[nodiscard]] const T* end() const { return data() + size(); }
+    [[nodiscard]] const T& operator[](std::size_t index) const { return block->values[index]; }
+    [[nodiscard]] const T& at(std::size_t index) const {
+        if (index >= size()) throw std::out_of_range("SharedValues::at");
+        return block->values[index];
+    }
+    [[nodiscard]] const T& front() const { return at(0); }
+    [[nodiscard]] const T& back() const { return at(size() - 1u); }
+    [[nodiscard]] std::span<const T> Span() const { return {data(), size()}; }
+    [[nodiscard]] std::vector<T> Copy() const { return {begin(), end()}; }
+    [[nodiscard]] bool Shares(const SharedValues& other) const { return block == other.block; }
+
+    [[nodiscard]] std::uint64_t Hash() const {
+        constexpr std::uint64_t Basis = 0xcbf29ce484222325ull;
+        if (block == nullptr) return Basis;
+        if (const auto known = block->hash.load(std::memory_order_relaxed); known != 0u) return known;
+        std::uint64_t hash = Basis;
+        const auto mix = [&](std::uint64_t value) { hash = (hash ^ value) * 0x100000001b3ull; };
+        mix(block->values.size());
+        for (const auto& value : block->values) {
+            if constexpr (std::is_same_v<T, DescriptorValue>) {
+                mix(value.dwordCount);
+                for (const auto dword : value.dwords) mix(dword);
+            } else if constexpr (std::is_integral_v<T>) {
+                mix(value);
+            } else {
+                mix(value.first);
+                mix(value.second);
+            }
+        }
+        hash = hash != 0u ? hash : 1u;
+        block->hash.store(hash, std::memory_order_relaxed);
+        return hash;
+    }
+
+    friend bool operator==(const SharedValues& left, const SharedValues& right) { return left.block == right.block || std::ranges::equal(left.Span(), right.Span()); }
+    friend bool operator==(const SharedValues& left, const std::vector<T>& right) { return std::ranges::equal(left.Span(), right); }
+
+private:
+    struct Block {
+        explicit Block(std::vector<T> values) : values(std::move(values)) {}
+        std::vector<T> values;
+        mutable std::atomic<std::uint64_t> hash{0};
+    };
+    std::shared_ptr<const Block> block;
+};
+
 enum class UniformFillKind { None, Buffer, Image };
 
 struct UniformFill {
@@ -44,15 +105,15 @@ struct ImageTableColumnSnapshot {
     std::uint32_t keys = 0;
     std::uint32_t fault = 0;
     bool outside = false;
-    std::vector<std::uint32_t> codes;
-    std::vector<std::pair<std::uint64_t, std::uint64_t>> readRanges;
+    SharedValues<std::uint32_t> codes;
+    SharedValues<DescriptorValue> words;
+    SharedValues<std::pair<std::uint64_t, std::uint64_t>> readRanges;
 
     bool operator==(const ImageTableColumnSnapshot& other) const = default;
 };
 
 struct ImageTableSnapshot {
     std::vector<ImageTableColumnSnapshot> tables;
-    std::vector<DescriptorValue> words;
     std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
     std::uint64_t shader = 0;
 
diff --git a/core/shader/recompiler/Optimization/include/Optimization/ResourceMaterializer.hpp b/core/shader/recompiler/Optimization/include/Optimization/ResourceMaterializer.hpp
index e0300414b..8f59e495e 100644
--- a/core/shader/recompiler/Optimization/include/Optimization/ResourceMaterializer.hpp
+++ b/core/shader/recompiler/Optimization/include/Optimization/ResourceMaterializer.hpp
@@ -6,6 +6,7 @@
 #include "ImageTableAbi.hpp"
 #include <array>
 #include <cstdint>
+#include <memory>
 #include <optional>
 #include <string>
 #include <vector>
@@ -45,6 +46,10 @@ struct ImageTableReuseCounts {
     std::uint64_t unwritten = 0;
     std::uint64_t compared = 0;
     std::uint64_t wordsRead = 0;
+    std::uint64_t shared = 0;
+    std::uint64_t columns = 0;
+    std::uint64_t resolutions = 0;
+    std::uint64_t resolutionsShared = 0;
 };
 
 struct RuntimeImageModeMatch {
@@ -63,6 +68,7 @@ public:
     static void PrepareImageModes(ShaderInfo& info);
     static const char* UnnormalizedSampleMismatch(const ImageResource& image);
     static ResolvedImageTables ResolveImageTables(const ShaderInfo& info, const ResourceSnapshot& snapshot, std::uint64_t memoKey = 0);
+    static std::shared_ptr<const ResolvedImageTables> ResolveImageTablesShared(const ShaderInfo& info, const ResourceSnapshot& snapshot, std::uint64_t memoKey);
     [[nodiscard]] IrResourcePlan ExtractPlan(const IrProgram& program) const;
     void Materialize(const IrResourcePlan& program, const SrtRuntime& runtime, ResourceSnapshot& snapshot) const;
     static std::uint64_t SpecializationNanoseconds();
diff --git a/core/shader/recompiler/Optimization/include/Optimization/SrtWalker.hpp b/core/shader/recompiler/Optimization/include/Optimization/SrtWalker.hpp
index 825d9e8a4..04147ec34 100644
--- a/core/shader/recompiler/Optimization/include/Optimization/SrtWalker.hpp
+++ b/core/shader/recompiler/Optimization/include/Optimization/SrtWalker.hpp
@@ -3,7 +3,9 @@
 
 #include "IntermediateRepresentation/IrProgram.hpp"
 
+#include <atomic>
 #include <cstdint>
+#include <memory>
 #include <span>
 #include <utility>
 #include <vector>
@@ -19,9 +21,45 @@ struct SrtWriteStamp {
     std::uint64_t layout = 0;
 };
 
+struct SrtStampCell {
+    std::atomic<std::uint64_t> sequence{0};
+    std::atomic<std::uint64_t> generation{0};
+    std::atomic<std::uint64_t> layout{0};
+};
+
+struct SrtGuestWords {
+    std::vector<std::pair<std::uint64_t, std::uint32_t>> runs;
+    std::vector<std::uint32_t> words;
+    std::shared_ptr<SrtStampCell> stamp;
+    std::uint64_t low = 0;
+    std::uint64_t high = 0;
+    bool ordered = false;
+};
+
+inline SrtWriteStamp LoadStamp(const SrtGuestWords& words) {
+    if (words.stamp == nullptr) return {};
+    const auto& cell = *words.stamp;
+    const auto sequence = cell.sequence.load(std::memory_order_acquire);
+    if ((sequence & 1u) != 0u) return {};
+    const SrtWriteStamp stamp{cell.generation.load(std::memory_order_relaxed), cell.layout.load(std::memory_order_relaxed)};
+    std::atomic_thread_fence(std::memory_order_acquire);
+    return cell.sequence.load(std::memory_order_relaxed) == sequence ? stamp : SrtWriteStamp{};
+}
+
+inline void StoreStamp(const SrtGuestWords& words, const SrtWriteStamp& stamp) {
+    if (words.stamp == nullptr || stamp.generation == 0u) return;
+    auto& cell = *words.stamp;
+    auto sequence = cell.sequence.load(std::memory_order_relaxed);
+    if ((sequence & 1u) != 0u || !cell.sequence.compare_exchange_strong(sequence, sequence + 1u, std::memory_order_relaxed)) return;
+    std::atomic_thread_fence(std::memory_order_release);
+    cell.generation.store(stamp.generation, std::memory_order_relaxed);
+    cell.layout.store(stamp.layout, std::memory_order_relaxed);
+    cell.sequence.store(sequence + 2u, std::memory_order_release);
+}
+
 using SrtMemoryStamper = SrtWriteStamp (*)(void* userData, std::uint64_t address, std::uint64_t bytes);
 using SrtMemoryUnchanged = bool (*)(void* userData, std::uint64_t address, std::uint64_t bytes, const SrtWriteStamp& stamp);
-using SrtMemorySupplier = bool (*)(void* userData, std::uint64_t address, const std::uint32_t* values, std::uint64_t count);
+using SrtMemorySupplier = bool (*)(void* userData, const std::shared_ptr<const SrtGuestWords>& words);
 
 // The guest addresses a walk dereferenced (Detail::Evaluator::EvaluateRawRead): the leaf read of
 // each pure flat slot (IrResourcePlan::pureFlatSlots) as (flat offset, address), set by the
diff --git a/core/shader/recompiler/Optimization/src/ResourceMaterializer.cpp b/core/shader/recompiler/Optimization/src/ResourceMaterializer.cpp
index 6a2c4f953..94e7657f6 100644
--- a/core/shader/recompiler/Optimization/src/ResourceMaterializer.cpp
+++ b/core/shader/recompiler/Optimization/src/ResourceMaterializer.cpp
@@ -18,6 +18,7 @@
 #include <cstring>
 #include <limits>
 #include <map>
+#include <memory>
 #include <mutex>
 #include <optional>
 #include <set>
@@ -338,6 +339,10 @@ struct TableReuseTotals {
     std::atomic<std::uint64_t> unwritten{0};
     std::atomic<std::uint64_t> compared{0};
     std::atomic<std::uint64_t> wordsRead{0};
+    std::atomic<std::uint64_t> shared{0};
+    std::atomic<std::uint64_t> columns{0};
+    std::atomic<std::uint64_t> resolutions{0};
+    std::atomic<std::uint64_t> resolutionsShared{0};
 };
 
 TableReuseTotals& tableReuseTotals() {
@@ -377,6 +382,15 @@ void reportTables() {
 
 enum class RecordState : std::uint8_t { OutsideDomain, Read, Unmapped, Carried };
 
+struct ColumnContents {
+    SharedValues<std::uint32_t> codes;
+    SharedValues<DescriptorValue> words;
+    SharedValues<std::pair<std::uint64_t, std::uint64_t>> readRanges;
+    std::uint32_t poisoned = 0;
+    std::uint32_t unmapped = 0;
+    std::uint32_t carried = 0;
+};
+
 struct ColumnRecords {
     std::uint64_t base = 0;
     std::uint64_t size = 0;
@@ -385,9 +399,12 @@ struct ColumnRecords {
     std::uint32_t fault = 0;
     bool narrowed = false;
     bool outside = false;
+    bool sealed = false;
     std::vector<RecordState> states;
     std::vector<std::uint32_t> words;
+    std::vector<DescriptorValue> distinct;
     std::vector<std::pair<std::uint64_t, std::uint64_t>> readRanges;
+    ColumnContents contents;
 };
 
 struct TableTrace {
@@ -436,18 +453,13 @@ struct ColumnKey {
 
 struct ColumnMemo {
     ColumnKey key;
-    SrtWriteStamp stamp;
-    std::vector<std::pair<std::uint64_t, std::uint32_t>> runs;
-    std::vector<std::uint32_t> words;
-    std::vector<RecordState> states;
-    std::vector<std::uint32_t> values;
-    std::vector<DescriptorValue> distinct;
-    std::vector<std::pair<std::uint64_t, std::uint64_t>> readRanges;
+    std::shared_ptr<const SrtGuestWords> read;
+    ColumnContents contents;
     std::uint64_t low = 0;
     std::uint64_t high = 0;
 
     std::size_t Footprint() const {
-        return words.size() + values.size() + states.size() / 4u + distinct.size() * 9u + runs.size() * 3u + readRanges.size() * 4u;
+        return read->words.size() + read->runs.size() * 3u + contents.codes.size() + contents.words.size() * 9u + contents.readRanges.size() * 4u;
     }
 };
 
@@ -647,33 +659,64 @@ private:
             low = std::min(low, address);
             high = std::max(high, address + sizeof(std::uint32_t));
         }
-        records.words.front() = WordsIndex(value);
+        records.words.front() = WordsIndex(records, value);
         if (low < high) tables.ranges.emplace_back(low, high - low);
     }
 
-    std::uint32_t WordsIndex(const DescriptorValue& value) {
-        for (; indexed < tables.words.size(); indexed++) wordsIndex.try_emplace({tables.words[indexed].dwords, tables.words[indexed].dwordCount}, static_cast<std::uint32_t>(indexed));
-        const auto [found, inserted] = wordsIndex.try_emplace({value.dwords, value.dwordCount}, static_cast<std::uint32_t>(tables.words.size()));
-        if (inserted) {
-            tables.words.push_back(value);
-            indexed = tables.words.size();
-        }
+    std::uint32_t WordsIndex(ColumnRecords& records, const DescriptorValue& value) {
+        const auto [found, inserted] = wordsIndex.try_emplace({value.dwords, value.dwordCount}, static_cast<std::uint32_t>(records.distinct.size()));
+        if (inserted) records.distinct.push_back(value);
         return found->second;
     }
 
+    void Seal(ColumnRecords& records) {
+        auto& contents = records.contents;
+        std::vector<std::uint32_t> codes(records.keys, 0u);
+        for (std::uint32_t key = 0; key < records.keys; key++) {
+            switch (records.states[key]) {
+                case RecordState::Read:
+                    codes[key] = records.words[key];
+                    continue;
+                case RecordState::OutsideDomain:
+                    codes[key] = ImageTableAbi::PoisonCode(static_cast<std::uint32_t>(PoisonReason::OutsideDomain));
+                    break;
+                case RecordState::Unmapped:
+                    codes[key] = ImageTableAbi::PoisonCode(static_cast<std::uint32_t>(PoisonReason::Unmapped));
+                    contents.unmapped++;
+                    break;
+                case RecordState::Carried:
+                    codes[key] = ImageTableAbi::PoisonCode(static_cast<std::uint32_t>(PoisonReason::OffsetCarry));
+                    contents.carried++;
+                    break;
+            }
+            contents.poisoned++;
+        }
+        contents.codes = SharedValues<std::uint32_t>(std::move(codes));
+        contents.words = SharedValues<DescriptorValue>(std::move(records.distinct));
+        contents.readRanges = SharedValues<std::pair<std::uint64_t, std::uint64_t>>(std::move(records.readRanges));
+        records.sealed = true;
+        tableReuseTotals().columns.fetch_add(1, std::memory_order_relaxed);
+    }
+
     ColumnRecords& Column(std::uint32_t source) {
         if (const auto found = columns.find(source); found != columns.end()) {
             return found->second;
         }
         auto& records = columns[source];
-        const auto& column = *plan.descriptorSources.at(source).tableColumn;
+        ReadColumn(*plan.descriptorSources.at(source).tableColumn, source, records);
+        if (!records.sealed) Seal(records);
+        return records;
+    }
+
+    void ReadColumn(const TableColumn& column, std::uint32_t source, ColumnRecords& records) {
         stamp = {};
         comparedRuns.clear();
         comparedWords.clear();
+        wordsIndex.clear();
         const bool active = source >= activeSources.size() || activeSources[source] != 0u;
         if (!active) {
             records.fault = ImageTableAbi::PoisonCode(static_cast<std::uint32_t>(PoisonReason::Inactive));
-            return records;
+            return;
         }
         if (column.address) {
             DescriptorValue pointer;
@@ -684,13 +727,13 @@ private:
             const auto heap = EvaluateBuffer(column.heapSource);
             if (heap.Type() != 0u) {
                 records.fault = ImageTableAbi::PoisonCode(static_cast<std::uint32_t>(PoisonReason::NotBuffer));
-                return records;
+                return;
             }
             records.base = heap.Base48();
             records.size = heap.GetSize();
             if (column.selected) {
                 Select(column, records);
-                return records;
+                return;
             }
             records.records = ImageTableAbi::ScalarBufferRecords(column.addend, column.stride, column.offset + (column.indirect ? 4u : 0u), records.size);
             if (records.size != 0u) tables.ranges.emplace_back(records.base, records.size);
@@ -698,13 +741,16 @@ private:
         records.records = std::min(records.records, ImageTableAbi::KeyRecords(column.maxKey, column.stride));
         records.keys = std::min(records.records, ImageTableAbi::MaxKeys);
         records.outside = records.keys < records.records;
-        records.states.assign(records.keys, RecordState::Read);
         if (!column.keyDomains.empty() && records.keys != 0u) {
             Narrow(column, records);
         }
-        records.words.assign(records.keys, 0u);
+        const auto unread = [&] {
+            if (!records.narrowed) records.states.assign(records.keys, RecordState::Read);
+            records.words.assign(records.keys, 0u);
+        };
         const auto end = records.base + records.size;
         if (column.indirect) {
+            unread();
             for (std::uint32_t record = 0; record < records.keys; record++) {
                 if (records.states[record] != RecordState::Read) continue;
                 const auto offset = column.addend + record * column.stride;
@@ -731,18 +777,20 @@ private:
                     past = std::max(past, *address + sizeof(std::uint32_t));
                 }
                 if (records.states[record] != RecordState::Read) continue;
-                records.words[record] = WordsIndex(value);
+                records.words[record] = WordsIndex(records, value);
                 tables.ranges.emplace_back(first, past - first);
             }
-            return records;
+            return;
         }
         const bool memoized = TableSnapshotReuse() && !records.narrowed && records.keys != 0u;
+        const ColumnKey key{records.base, records.size, records.records, records.keys, column.stride, column.addend, column.offset, column.dwordCount, column.maxKey, column.foldedAddend, column.foldedCarry, column.address, column.sampler};
+        if (memoized && Reuse(key, column, records, false)) return;
         if (memoized && !column.address && records.size != 0u && runtime.stampWrites != nullptr) {
             const auto span = std::min<std::uint64_t>(records.size, static_cast<std::uint64_t>(column.addend) + column.offset + static_cast<std::uint64_t>(records.keys) * column.stride + column.dwordCount * 4u);
             stamp = runtime.stampWrites(runtime.userContext, records.base, span);
         }
-        const ColumnKey key{records.base, records.size, records.records, records.keys, column.stride, column.addend, column.offset, column.dwordCount, column.maxKey, column.foldedAddend, column.foldedCarry, column.address, column.sampler};
-        if (memoized && Reuse(key, column, records)) return records;
+        if (memoized && Reuse(key, column, records, true)) return;
+        unread();
         constexpr auto Unread = std::numeric_limits<std::uint32_t>::max();
         std::vector<std::uint32_t> whole(records.keys, Unread);
         std::vector<std::pair<std::uint32_t, std::array<std::uint32_t, 8>>> split;
@@ -818,7 +866,6 @@ private:
             read.resize(total);
             ReadRuns(runs, read);
         }
-        const auto firstWord = tables.words.size();
         auto next = split.begin();
         for (std::uint32_t record = 0; record < records.keys; record++) {
             if (records.states[record] != RecordState::Read) continue;
@@ -832,7 +879,7 @@ private:
                 }
                 ++next;
             }
-            records.words[record] = WordsIndex(value);
+            records.words[record] = WordsIndex(records, value);
         }
         if (column.address && low < high) tables.ranges.emplace_back(low, high - low);
         std::sort(records.readRanges.begin(), records.readRanges.end());
@@ -846,8 +893,8 @@ private:
             }
         }
         records.readRanges.resize(ranges);
-        if (memoized) Remember(key, records, std::move(runs), std::move(read), column.address && low < high ? low : 0u, column.address && low < high ? high : 0u, firstWord == 0u);
-        return records;
+        Seal(records);
+        if (memoized) Remember(key, records, std::move(runs), std::move(read), column.address && low < high ? low : 0u, column.address && low < high ? high : 0u);
     }
 
     void ReadRuns(const std::vector<std::pair<std::uint64_t, std::uint32_t>>& runs, std::vector<std::uint32_t>& read) const {
@@ -870,106 +917,83 @@ private:
     }
 
     bool Unwritten(const ColumnMemo& memo) const {
-        if (memo.stamp.generation == 0u || runtime.unchangedSince == nullptr || runtime.supplyWords == nullptr) return false;
-        for (const auto& [address, count] : memo.runs) {
-            if (!runtime.unchangedSince(runtime.userContext, address, static_cast<std::uint64_t>(count) * sizeof(std::uint32_t), memo.stamp)) return false;
-        }
-        std::size_t at = 0;
-        for (const auto& [address, count] : memo.runs) {
-            if (!runtime.supplyWords(runtime.userContext, address, memo.words.data() + at, count)) return false;
-            at += count;
-        }
-        return true;
+        if (runtime.unchangedSince == nullptr || runtime.supplyWords == nullptr || memo.read->low >= memo.read->high) return false;
+        const auto vouched = LoadStamp(*memo.read);
+        if (vouched.generation == 0u || !runtime.unchangedSince(runtime.userContext, memo.read->low, memo.read->high - memo.read->low, vouched)) return false;
+        return runtime.supplyWords(runtime.userContext, memo.read);
     }
 
     bool Compared(const TableColumn& column, const ColumnRecords& records, ColumnMemo& memo) {
         const auto end = records.base + records.size;
-        for (const auto& [address, count] : memo.runs) {
+        for (const auto& [address, count] : memo.read->runs) {
             const auto past = address + static_cast<std::uint64_t>(count) * sizeof(std::uint32_t);
             for (auto page = address & ~(PageBytes - 1u); page < past; page += PageBytes) {
                 const auto first = std::max(page, address);
                 if (column.address ? !WholePageAccessible(first) : !PageAccessible(first, records.base, end)) return false;
             }
         }
-        std::vector<std::uint32_t> read(memo.words.size());
-        ReadRuns(memo.runs, read);
-        if (read != memo.words) {
-            comparedRuns = memo.runs;
+        std::vector<std::uint32_t> read(memo.read->words.size());
+        ReadRuns(memo.read->runs, read);
+        if (read != memo.read->words) {
+            comparedRuns = memo.read->runs;
             comparedWords = std::move(read);
             return false;
         }
-        if (stamp.generation != 0u) memo.stamp = stamp;
+        StoreStamp(*memo.read, stamp);
         return true;
     }
 
     void Apply(const TableColumn& column, ColumnRecords& records, const ColumnMemo& memo) {
-        records.states = memo.states;
-        records.readRanges = memo.readRanges;
-        if (tables.words.empty()) {
-            tables.words = memo.distinct;
-            records.words = memo.values;
-        } else {
-            std::vector<std::uint32_t> global(memo.distinct.size());
-            for (std::size_t value = 0; value < memo.distinct.size(); value++) global[value] = WordsIndex(memo.distinct[value]);
-            records.words.assign(records.keys, 0u);
-            for (std::uint32_t record = 0; record < records.keys; record++) {
-                if (memo.states[record] == RecordState::Read) records.words[record] = global[memo.values[record]];
-            }
-        }
+        records.contents = memo.contents;
+        records.sealed = true;
         if (column.address && memo.low < memo.high) tables.ranges.emplace_back(memo.low, memo.high - memo.low);
     }
 
-    bool Reuse(const ColumnKey& key, const TableColumn& column, ColumnRecords& records) {
+    bool Reuse(const ColumnKey& key, const TableColumn& column, ColumnRecords& records, bool compare) {
         auto& memos = ColumnMemos();
         const auto found = std::find_if(memos.begin(), memos.end(), [&](const ColumnMemo& entry) { return entry.key == key; });
         if (found == memos.end()) return false;
         auto& counters = tableCounters();
         auto& totals = tableReuseTotals();
-        if (Unwritten(*found)) {
+        if (!compare && Unwritten(*found)) {
             counters.reusedUnwritten.fetch_add(1, std::memory_order_relaxed);
             totals.unwritten.fetch_add(1, std::memory_order_relaxed);
-        } else if (Compared(column, records, *found)) {
+        } else if (compare && Compared(column, records, *found)) {
             counters.reusedCompared.fetch_add(1, std::memory_order_relaxed);
             totals.compared.fetch_add(1, std::memory_order_relaxed);
         } else {
             return false;
         }
         Apply(column, records, *found);
+        totals.shared.fetch_add(1, std::memory_order_relaxed);
         std::rotate(memos.begin(), found, found + 1);
         return true;
     }
 
-    void Remember(const ColumnKey& key, const ColumnRecords& records, std::vector<std::pair<std::uint64_t, std::uint32_t>>&& runs, std::vector<std::uint32_t>&& words, std::uint64_t low, std::uint64_t high, bool first) {
-        if (std::any_of(records.states.begin(), records.states.end(), [](RecordState state) { return state != RecordState::Read && state != RecordState::Carried; })) return;
+    void Remember(const ColumnKey& key, const ColumnRecords& records, std::vector<std::pair<std::uint64_t, std::uint32_t>>&& runs, std::vector<std::uint32_t>&& words, std::uint64_t low, std::uint64_t high) {
+        if (records.contents.poisoned != records.contents.carried) return;
         auto& memos = ColumnMemos();
         auto found = std::find_if(memos.begin(), memos.end(), [&](const ColumnMemo& entry) { return entry.key == key; });
         if (found == memos.end()) found = memos.emplace(memos.end());
         auto& memo = *found;
         memo.key = key;
-        memo.stamp = stamp;
-        memo.runs = std::move(runs);
-        memo.words = std::move(words);
-        memo.states.assign(records.states.begin(), records.states.end());
-        memo.readRanges.assign(records.readRanges.begin(), records.readRanges.end());
+        std::uint64_t first = std::numeric_limits<std::uint64_t>::max();
+        std::uint64_t past = 0;
+        std::uint64_t total = 0;
+        bool ordered = !runs.empty();
+        for (const auto& [address, count] : runs) {
+            const auto bytes = static_cast<std::uint64_t>(count) * sizeof(std::uint32_t);
+            ordered = ordered && count != 0u && address % sizeof(std::uint32_t) == 0u && address >= past && address <= std::numeric_limits<std::uint64_t>::max() - bytes;
+            first = std::min(first, address);
+            past = std::max(past, address + bytes);
+            total += count;
+        }
+        ordered = ordered && total == words.size();
+        memo.read = std::make_shared<const SrtGuestWords>(SrtGuestWords{std::move(runs), std::move(words), std::make_shared<SrtStampCell>(), first < past ? first : 0u, first < past ? past : 0u, ordered});
+        StoreStamp(*memo.read, stamp);
+        memo.contents = records.contents;
         memo.low = low;
         memo.high = high;
-        if (first) {
-            memo.values.assign(records.words.begin(), records.words.end());
-            memo.distinct.assign(tables.words.begin(), tables.words.end());
-        } else {
-            std::vector<std::uint32_t> local(tables.words.size(), NoValue);
-            memo.values.assign(records.keys, 0u);
-            memo.distinct.clear();
-            for (std::uint32_t record = 0; record < records.keys; record++) {
-                if (records.states[record] != RecordState::Read) continue;
-                auto& slot = local[records.words[record]];
-                if (slot == NoValue) {
-                    slot = static_cast<std::uint32_t>(memo.distinct.size());
-                    memo.distinct.push_back(tables.words[records.words[record]]);
-                }
-                memo.values[record] = slot;
-            }
-        }
         if (memo.Footprint() > ColumnMemoWords) {
             memos.erase(found);
             return;
@@ -991,32 +1015,15 @@ private:
         out.keys = records.keys;
         out.fault = records.fault;
         out.outside = records.outside;
-        out.codes.assign(records.keys, 0u);
-        out.readRanges = records.readRanges;
-        TableTrace trace{records.base, records.size, records.records, records.keys, 0u, records.narrowed};
+        out.codes = records.contents.codes;
+        out.words = records.contents.words;
+        out.readRanges = records.contents.readRanges;
         auto& counters = tableCounters();
         if (records.fault != 0u) counters.poison[records.fault & ~ImageTableAbi::PoisonFlag].fetch_add(1, std::memory_order_relaxed);
-        for (std::uint32_t key = 0; key < records.keys; key++) {
-            switch (records.states[key]) {
-                case RecordState::Read:
-                    out.codes[key] = records.words[key];
-                    continue;
-                case RecordState::OutsideDomain:
-                    out.codes[key] = ImageTableAbi::PoisonCode(static_cast<std::uint32_t>(PoisonReason::OutsideDomain));
-                    break;
-                case RecordState::Unmapped:
-                    out.codes[key] = ImageTableAbi::PoisonCode(static_cast<std::uint32_t>(PoisonReason::Unmapped));
-                    counters.poison[static_cast<std::size_t>(PoisonReason::Unmapped)].fetch_add(1, std::memory_order_relaxed);
-                    break;
-                case RecordState::Carried:
-                    out.codes[key] = ImageTableAbi::PoisonCode(static_cast<std::uint32_t>(PoisonReason::OffsetCarry));
-                    counters.poison[static_cast<std::size_t>(PoisonReason::OffsetCarry)].fetch_add(1, std::memory_order_relaxed);
-                    break;
-            }
-            trace.poison++;
-        }
+        if (records.contents.unmapped != 0u) counters.poison[static_cast<std::size_t>(PoisonReason::Unmapped)].fetch_add(records.contents.unmapped, std::memory_order_relaxed);
+        if (records.contents.carried != 0u) counters.poison[static_cast<std::size_t>(PoisonReason::OffsetCarry)].fetch_add(records.contents.carried, std::memory_order_relaxed);
         counters.keys.fetch_add(records.keys, std::memory_order_relaxed);
-        if (TableTraced()) traceTable(plan, table, column, trace);
+        if (TableTraced()) traceTable(plan, table, column, {records.base, records.size, records.records, records.keys, records.contents.poisoned, records.narrowed});
     }
 
     const IrResourcePlan& plan;
@@ -1035,11 +1042,9 @@ private:
     std::unordered_map<std::pair<std::array<std::uint32_t, 8>, std::uint32_t>, std::uint32_t, WordsHash> wordsIndex;
     std::unordered_map<std::uint64_t, bool> pages;
     std::unordered_map<std::uint64_t, bool> wholePages;
-    std::size_t indexed = 0;
     SrtWriteStamp stamp;
     std::vector<std::pair<std::uint64_t, std::uint32_t>> comparedRuns;
     std::vector<std::uint32_t> comparedWords;
-    static constexpr std::uint32_t NoValue = std::numeric_limits<std::uint32_t>::max();
 };
 
 void materializeSnapshot(const IrResourcePlan& plan, const SrtRuntime& runtime, SrtWalker& walker, ResourceSnapshot& snapshot, std::vector<std::uint8_t>& activeSources) {
@@ -1315,6 +1320,59 @@ std::uint32_t decodeTableEntry(const ImageResource& image, std::span<const Image
     return value;
 }
 
+std::vector<bool> reducingSamplers(const ShaderInfo& info, const ResourceSnapshot& snapshot) {
+    std::vector<bool> reducing(info.images.size(), false);
+    for (const auto& pair : info.sampledPairs) {
+        if (info.samplers.at(pair.sampler).table != NoTable) continue;
+        const auto& words = snapshot.samplers.at(pair.sampler).dwords;
+        if (reducesBetweenTexels(words[0], words[2])) reducing.at(pair.image) = true;
+    }
+    return reducing;
+}
+
+bool TableResolutionReuse() {
+    static const bool enabled = std::getenv("APS5_NO_TABLE_RESOLUTION_REUSE") == nullptr;
+    return enabled;
+}
+
+struct ResolutionMemoEntry {
+    std::uint64_t memoKey = 0;
+    std::vector<ImageTableColumnSnapshot> tables;
+    std::vector<bool> reducing;
+    std::shared_ptr<const ResolvedImageTables> resolved;
+    std::size_t footprint = 0;
+};
+
+struct ResolutionMemo {
+    std::mutex mutex;
+    std::vector<ResolutionMemoEntry> entries;
+};
+
+ResolutionMemo& resolutionMemo() {
+    static ResolutionMemo memo;
+    return memo;
+}
+
+constexpr std::size_t ResolutionMemoEntries = 256;
+constexpr std::size_t ResolutionMemoVariantEntries = 4;
+constexpr std::size_t ResolutionMemoWords = std::size_t{1} << 23u;
+
+bool sameColumns(const std::vector<ImageTableColumnSnapshot>& left, const std::vector<ImageTableColumnSnapshot>& right) {
+    if (left.size() != right.size()) return false;
+    for (std::size_t index = 0; index < left.size(); index++) {
+        const auto& a = left[index];
+        const auto& b = right[index];
+        if (a.base != b.base || a.size != b.size || a.records != b.records || a.keys != b.keys || a.fault != b.fault || a.outside != b.outside || !a.codes.Shares(b.codes) || !a.words.Shares(b.words)) return false;
+    }
+    return true;
+}
+
+std::size_t resolutionFootprint(const ResolvedImageTables& resolved, const std::vector<ImageTableColumnSnapshot>& tables) {
+    std::size_t footprint = resolved.map.size() + (resolved.elements.size() * sizeof(ImageTableElement) + resolved.samplers.size() * sizeof(ImageTableSamplerElement) + resolved.poison.size() * sizeof(ImageTablePoisonRecord)) / sizeof(std::uint32_t);
+    for (const auto& table : tables) footprint += 16u + table.codes.size() + table.words.size() * 9u;
+    return footprint;
+}
+
 }
 
 ResolvedImageTables ResourceMaterializer::ResolveImageTables(const ShaderInfo& info, const ResourceSnapshot& snapshot, std::uint64_t memoKey) {
@@ -1346,14 +1404,10 @@ ResolvedImageTables ResourceMaterializer::ResolveImageTables(const ShaderInfo& i
     };
     std::map<std::tuple<std::array<std::uint32_t, 8>, RdnaImageDimension, bool, DescriptorBindingKind>, std::uint32_t> elementIndex;
     std::vector<std::uint32_t> directSamplers(info.images.size(), 0u);
-    std::vector<bool> reducingSampler(info.images.size(), false);
     for (const auto& pair : info.sampledPairs) {
-        const auto& sampler = info.samplers.at(pair.sampler);
-        if (sampler.table != NoTable) continue;
-        directSamplers.at(pair.image) |= 1u << pair.sampler;
-        const auto& words = snapshot.samplers.at(pair.sampler).dwords;
-        if (reducesBetweenTexels(words[0], words[2])) reducingSampler.at(pair.image) = true;
+        if (info.samplers.at(pair.sampler).table == NoTable) directSamplers.at(pair.image) |= 1u << pair.sampler;
     }
+    const auto reducingSampler = reducingSamplers(info, snapshot);
     const auto imageRecord = [&](std::uint32_t resource, const DescriptorValue& words) {
         const auto& image = info.images[resource];
         const auto& modes = info.runtimeImageModes[resource];
@@ -1439,7 +1493,7 @@ ResolvedImageTables ResourceMaterializer::ResolveImageTables(const ShaderInfo& i
                 const auto reason = static_cast<PoisonReason>(code & ~Abi::PoisonFlag);
                 mapped = reason == PoisonReason::OutsideDomain ? Abi::PoisonCode(Abi::OutsideDomainPoison) : poison(resource, reason, nullptr);
             } else if (out.map[header + Abi::TableFault] == 0u) {
-                const auto& words = tables.words.at(code);
+                const auto& words = column.words.at(code);
                 if (sampler || !nullImageDescriptor(words)) {
                     const auto [found, inserted] = records.try_emplace(code, 0u);
                     if (inserted) found->second = sampler ? samplerRecord(resource, words) : imageRecord(resource, words);
@@ -1472,6 +1526,42 @@ ResolvedImageTables ResourceMaterializer::ResolveImageTables(const ShaderInfo& i
     return out;
 }
 
+std::shared_ptr<const ResolvedImageTables> ResourceMaterializer::ResolveImageTablesShared(const ShaderInfo& info, const ResourceSnapshot& snapshot, std::uint64_t memoKey) {
+    if (memoKey == 0u || snapshot.tables.tables.empty() || !TableResolutionReuse()) return std::make_shared<const ResolvedImageTables>(ResolveImageTables(info, snapshot, memoKey));
+    auto reducing = reducingSamplers(info, snapshot);
+    auto& memo = resolutionMemo();
+    auto& totals = tableReuseTotals();
+    {
+        std::lock_guard lock(memo.mutex);
+        const auto found = std::find_if(memo.entries.begin(), memo.entries.end(), [&](const ResolutionMemoEntry& entry) { return entry.memoKey == memoKey && entry.reducing == reducing && sameColumns(entry.tables, snapshot.tables.tables); });
+        if (found != memo.entries.end()) {
+            std::rotate(memo.entries.begin(), found, found + 1);
+            totals.resolutionsShared.fetch_add(1, std::memory_order_relaxed);
+            return memo.entries.front().resolved;
+        }
+    }
+    auto resolved = std::make_shared<const ResolvedImageTables>(ResolveImageTables(info, snapshot, memoKey));
+    totals.resolutions.fetch_add(1, std::memory_order_relaxed);
+    const auto footprint = resolutionFootprint(*resolved, snapshot.tables.tables);
+    if (footprint > ResolutionMemoWords) return resolved;
+    std::lock_guard lock(memo.mutex);
+    memo.entries.insert(memo.entries.begin(), ResolutionMemoEntry{memoKey, snapshot.tables.tables, std::move(reducing), resolved, footprint});
+    std::size_t kept = 0;
+    std::size_t variantEntries = 0;
+    std::size_t total = 0;
+    for (std::size_t index = 0; index < memo.entries.size(); index++) {
+        auto& entry = memo.entries[index];
+        const bool sameVariant = entry.memoKey == memoKey;
+        if ((sameVariant && variantEntries == ResolutionMemoVariantEntries) || kept == ResolutionMemoEntries || total + entry.footprint > ResolutionMemoWords) continue;
+        if (sameVariant) variantEntries++;
+        total += entry.footprint;
+        if (kept != index) memo.entries[kept] = std::move(entry);
+        kept++;
+    }
+    memo.entries.resize(kept);
+    return resolved;
+}
+
 std::vector<ImageResource> ResourceMaterializer::RuntimeImageModes(const ImageResource& image) {
     const bool table = image.table != NoTable;
     if (table && image.tableOperation == TableOperation::Unsupported) return {};
@@ -1929,7 +2019,7 @@ std::uint64_t ResourceMaterializer::SpecializationNanoseconds() {
 
 ImageTableReuseCounts ResourceMaterializer::TableReuse() {
     auto& totals = tableReuseTotals();
-    return {totals.unwritten.load(std::memory_order_relaxed), totals.compared.load(std::memory_order_relaxed), totals.wordsRead.load(std::memory_order_relaxed)};
+    return {totals.unwritten.load(std::memory_order_relaxed), totals.compared.load(std::memory_order_relaxed), totals.wordsRead.load(std::memory_order_relaxed), totals.shared.load(std::memory_order_relaxed), totals.columns.load(std::memory_order_relaxed), totals.resolutions.load(std::memory_order_relaxed), totals.resolutionsShared.load(std::memory_order_relaxed)};
 }
 
 std::uint64_t ResourceMaterializer::EvaluateNanoseconds() {
diff --git a/core/shader/recompiler/Recompiler.cpp b/core/shader/recompiler/Recompiler.cpp
index 326d926ec..4ab3b4812 100644
--- a/core/shader/recompiler/Recompiler.cpp
+++ b/core/shader/recompiler/Recompiler.cpp
@@ -967,7 +967,8 @@ RecompileResult materializeResult(const CompiledVariant& variant, const Recompil
     } else {
         result.spirv = artifact.spirv;
     }
-    const auto tables = ResourceMaterializer::ResolveImageTables(variant.info.info, snapshot, artifact.variantId);
+    const auto resolved = ResourceMaterializer::ResolveImageTablesShared(variant.info.info, snapshot, artifact.variantId);
+    const auto& tables = *resolved;
     BindingAllocationResult bindings;
     DescriptorBindingBuilder{}.Populate(bindings, variant.bindings, *bindingPlan, variant.info.userDataBase, snapshot, partialThreads(request), &tables);
     result.workgroupMemoryDwords = WorkgroupMemoryStrideDwords(variant.info.info);
@@ -996,7 +997,7 @@ RecompileResult materializeResult(const CompiledVariant& variant, const Recompil
         for (const auto& poison : tables.poison) result.imageTablePoison.push_back({poison.words, poison.dwordCount, poison.resource, static_cast<std::uint32_t>(poison.reason)});
         result.imageTableRanges = snapshot.tables.ranges;
         result.imageTableReadRanges.reserve(snapshot.tables.tables.size());
-        for (const auto& table : snapshot.tables.tables) result.imageTableReadRanges.push_back(table.readRanges);
+        for (const auto& table : snapshot.tables.tables) result.imageTableReadRanges.push_back(table.readRanges.Copy());
         result.imageTableShader = snapshot.tables.shader;
         result.imageTableFaults = tables.faults;
     }
@@ -1184,14 +1185,10 @@ std::uint64_t snapshotHash(const RecompileRequest& request, const ResourceSnapsh
         mix(table.keys);
         mix(table.fault);
         mix(table.outside ? 1u : 0u);
-        mixWords(table.codes);
-        mix(table.readRanges.size());
-        for (const auto& [base, size] : table.readRanges) {
-            mix(base);
-            mix(size);
-        }
+        mix(table.codes.Hash());
+        mix(table.words.Hash());
+        mix(table.readRanges.Hash());
     }
-    mixDescriptors(tables.words);
     mix(tables.ranges.size());
     for (const auto& [base, size] : tables.ranges) {
         mix(base);
diff --git a/core/shader/recompiler/ShaderDiskCache.cpp b/core/shader/recompiler/ShaderDiskCache.cpp
index 05c6eea6e..3471c1d20 100644
--- a/core/shader/recompiler/ShaderDiskCache.cpp
+++ b/core/shader/recompiler/ShaderDiskCache.cpp
@@ -1253,6 +1253,8 @@ constexpr std::string_view NeutralSwitches[] = {
     "APS5_NO_PERF_ITERATION_SELECTION",
     "APS5_NO_PERF_LAYOUT_PAIR",
     "APS5_NO_RESULT_MEMO",
+    "APS5_NO_TABLE_RESOLUTION_REUSE",
+    "APS5_NO_TABLE_SNAPSHOT_REUSE",
 };
 
 const std::vector<std::byte>& switchKey() {
diff --git a/docs/dev/ARCHITECTURE.md b/docs/dev/ARCHITECTURE.md
index a37582ebe..5d1a66aa2 100644
--- a/docs/dev/ARCHITECTURE.md
+++ b/docs/dev/ARCHITECTURE.md
@@ -114,11 +114,11 @@ A T# or S# that a shader loads from a buffer at a runtime offset (`key * stride
 
 A T# whose eight dwords and record offset the SRT walker can evaluate is planned as a selected column (`TableColumn::selected`): the offset is kept as a descriptor source (`TableColumn::selection`) and the shader indexes entry 0 of a one-record table. The column stays a whole table when the shader may write the memory its offset is read from at run time: the buffer it is read from, or any memory when the read or a write goes through a pointer or a GPU-only descriptor. Samplers are not selected: an S# the walker can evaluate is a direct sampler.
 
-At every invocation, `TableSnapshotter` in [`ResourceMaterializer.cpp`](../../core/shader/recompiler/Optimization/src/ResourceMaterializer.cpp) reads each column's records from guest memory into `ResourceSnapshot::tables`: heap base and size, the record count (at most `ImageTableAbi::MaxKeys`), one code per record (an index into the distinct descriptor words, or a poison code) and the ranges it read. A selected column evaluates its offset and reads only that record, as the scalar loads would: dwords past the heap read zeros, an unmapped record or an offset that carries past 32 bits is poison, and an offset the walker cannot read faults the table instead of throwing. Its heap base moves to the record, so the shader's fault address and the written-heap check cover that record alone. Any other column narrows to the records its keys select, plus record 0, when every access has a key domain: a key buffer of at most 256 positions (`APS5_IMAGE_TABLE_KEY_SCAN`), or a value the SRT walker evaluates from user data, SRT reads and constants (which, for an image, selects its record before it could narrow). A loop-carried, lane-reduced or masked key keeps the whole column, so the cost of an invocation grows with the column's records and its distinct T#s. The snapshot is part of the invocation, not of the compiled variant: a changed table or key changes neither the SPIR-V nor the variant's disk key.
+At every invocation, `TableSnapshotter` in [`ResourceMaterializer.cpp`](../../core/shader/recompiler/Optimization/src/ResourceMaterializer.cpp) reads each column's records from guest memory into `ResourceSnapshot::tables`: heap base and size, the record count (at most `ImageTableAbi::MaxKeys`), one code per record (an index into the column's distinct descriptor words, or a poison code) and the ranges it read. A selected column evaluates its offset and reads only that record, as the scalar loads would: dwords past the heap read zeros, an unmapped record or an offset that carries past 32 bits is poison, and an offset the walker cannot read faults the table instead of throwing. Its heap base moves to the record, so the shader's fault address and the written-heap check cover that record alone. Any other column narrows to the records its keys select, plus record 0, when every access has a key domain: a key buffer of at most 256 positions (`APS5_IMAGE_TABLE_KEY_SCAN`), or a value the SRT walker evaluates from user data, SRT reads and constants (which, for an image, selects its record before it could narrow). A loop-carried, lane-reduced or masked key keeps the whole column, so the cost of an invocation grows with the column's records and its distinct T#s. The snapshot is part of the invocation, not of the compiled variant: a changed table or key changes neither the SPIR-V nor the variant's disk key.
 
-`TableSnapshotter` keeps the last 16 column snapshots per thread (at most 4 Mi words in all, least recently used first out; `APS5_NO_TABLE_SNAPSHOT_REUSE=1` turns them off), keyed by heap base and size, record shape and key count: the words read, each record's state and code, and the distinct T#s. A narrowed or pointer-indirect column, or one with an unmapped record, is not kept. A later capture of the same column takes the codes and the distinct T#s from it with no per-record work. When the runtime vouches that nothing wrote the column's runs since a stamp taken before the column's first read, the column is not read again: its words are handed to the capture as read (`SrtRuntime::supplyWords`), so they stay in the captured regions the draw cache compares. `ShaderMemory` vouches through the write tracker: the range was stamped by `GuestMemory::CollectWrites` before any page of it was fetched, no block of it was stamped since (a collected CPU store, the driver's own stores, `MarkWritten`), no recorded GPU work writes it, and no guest mapping changed. The stamp and the check both call `GuestMemory::FlushGpuWrites` over the range first, as a read through `GuestMemory` does, so storage image results kept on the GPU, unit shadows and queued labels over it are in guest memory before the tracker answers. A CPU store between two collects of one collect epoch is seen at the next epoch, as for every other stamp. Otherwise, outside the write watch for example, the runs are read again and the snapshot is reused only when they are equal word for word; when they differ, the column is walked afresh from the words just read.
+`TableSnapshotter` keeps the last 16 column snapshots per thread (at most 4 Mi words in all, least recently used first out; `APS5_NO_TABLE_SNAPSHOT_REUSE=1` turns them off), keyed by heap base and size, record shape and key count: the words read and the column a snapshot holds, its codes, distinct T#s and read ranges. A narrowed, selected or pointer-indirect column, or one with an unmapped record, is not kept. The column is built once and never changed (`SharedValues`): a later capture of the same column shares it with no per-record work and no copy, each snapshot holding a reference, and the result memo hashes its contents once (`SharedValues::Hash`). A column read again after a write is a new one; the snapshots that share the old one keep it. When the runtime vouches that nothing wrote the column's runs since a stamp taken before the column's first read, the column is not read again: its words are handed to the capture as one shared block (`SrtRuntime::supplyWords`, `SrtGuestWords`): `ShaderMemory` keeps a reference to it and reports its runs in the captured regions the draw cache compares, split at page boundaries as a read reports them, without copying them into its pages, and moves them into pages only when a later read in the same `ShaderMemory` touches them. When the draw or dispatch cache sets a fresh capture against a kept one, words both copied from the same place of one such block are equal without a compare (`SameCapturedWords`); only the words around them are compared. A capture holds its blocks weakly for this, so the identity of a released block never matches a new one. `ShaderMemory` vouches through the write tracker: the range was stamped by `GuestMemory::CollectWrites` before any page of it was fetched, no block of it was stamped since (a collected CPU store, the driver's own stores, `MarkWritten`), no recorded GPU work writes it, and no guest mapping changed. The stamp and the check both call `GuestMemory::FlushGpuWrites` over the range first, as a read through `GuestMemory` does, so storage image results kept on the GPU, unit shadows and queued labels over it are in guest memory before the tracker answers. A CPU store between two collects of one collect epoch is seen at the next epoch, as for every other stamp. Otherwise, outside the write watch for example, the runs are read again and the snapshot is reused only when they are equal word for word; when they differ, the column is walked afresh from the words just read.
 
-`ResolveImageTables` turns the snapshot into the map buffer (`DescriptorRole::ImageTableMap`, layout in [`ImageTableAbi.hpp`](../../core/shader/recompiler/ImageTableAbi.hpp)): a header, eight words per table (map start, key count, heap base and size, the code for keys past the snapshot, a table fault) and one record per key (flags with the decode mode, the swizzle dword and the element for images; flags and element for samplers). The shader reads `map[key]` and switches over the image's prepared runtime modes. Each distinct T# becomes one element, up to `ImageElementBudget` and the device's capacity. An entry that does not decode, is multisampled or over budget, pairs a reducing sampler with a point-only mode, is an unnormalized or reducing S#, or whose surface has no mapped byte is poisoned: it reads zeros and reports a fault through the BDA fault buffer only when a lane selects it (`APS5_IMAGE_TABLE_STRICT=1` throws at the snapshot instead). The driver's `resolveImageTables` in [`ShaderResources.cpp`](../../core/libs/prx/libSceAgcDriver/Graphics/src/ShaderResources.cpp) looks up or creates a texture per element and keeps the last four resolved tables per thread for reuse within one collect epoch while their memory is unchanged. Cached objects grow with their tables, so the resource cache keeps at most `ResourceCache::TableElementBudget()` table elements (65,536, `APS5_RESOURCE_CACHE_TABLE_ELEMENTS`) and evicts its least recently used table objects past that, and the dispatch cache evicts its least recently used entries while their captured words exceed `Driver::DispatchCacheBytes()` (256 MiB, `APS5_DISPATCH_CACHE_BYTES`).
+`ResolveImageTables` turns the snapshot into the map buffer (`DescriptorRole::ImageTableMap`, layout in [`ImageTableAbi.hpp`](../../core/shader/recompiler/ImageTableAbi.hpp)): a header, eight words per table (map start, key count, heap base and size, the code for keys past the snapshot, a table fault) and one record per key (flags with the decode mode, the swizzle dword and the element for images; flags and element for samplers). The shader reads `map[key]` and switches over the image's prepared runtime modes. A result memo miss takes the resolution of an earlier one by reference when the variant, the reducing state of the direct samplers paired with table images and every column match, a column's codes and distinct T#s by identity (the same shared values, so equal copies resolve again): `ResolveImageTablesShared` keeps at most 4 resolutions per variant and 256 in all within 8 Mi words, counting the columns they keep alive, least recently used first out, and `APS5_NO_TABLE_RESOLUTION_REUSE=1` turns it off. Each distinct T# becomes one element, up to `ImageElementBudget` and the device's capacity. An entry that does not decode, is multisampled or over budget, pairs a reducing sampler with a point-only mode, is an unnormalized or reducing S#, or whose surface has no mapped byte is poisoned: it reads zeros and reports a fault through the BDA fault buffer only when a lane selects it (`APS5_IMAGE_TABLE_STRICT=1` throws at the snapshot instead). The driver's `resolveImageTables` in [`ShaderResources.cpp`](../../core/libs/prx/libSceAgcDriver/Graphics/src/ShaderResources.cpp) looks up or creates a texture per element and keeps the last four resolved tables per thread for reuse within one collect epoch while their memory is unchanged. Cached objects grow with their tables, so the resource cache keeps at most `ResourceCache::TableElementBudget()` table elements (65,536, `APS5_RESOURCE_CACHE_TABLE_ELEMENTS`) and evicts its least recently used table objects past that, and the dispatch cache evicts its least recently used entries while their captured words exceed `Driver::DispatchCacheBytes()` (256 MiB, `APS5_DISPATCH_CACHE_BYTES`).
 
 The disk cache (`FormatVersion` 36) stores the table and table operation of each image and sampler, the image shapes of the table binding, the invocation's table poison list, read ranges and fault count, and the table column of each descriptor source in the front-end plan, with its key domains and its selection.
 
```
