# PM-043: bench sources (cloud-2)

These are local, uncommitted bench additions, applied identically to main, #6440, #6340, v3 and the three merge branches for §3 of `PM-043.md`. None of it is on any branch.

## 1. `TableCaptureBench.cpp` (#6440-style 19k-record capture bench)

```cpp
#include "prx/libSceAgcDriver/Execution/include/ShaderMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "Optimization/ResourceMaterializer.hpp"
#include "Optimization/ResourceProgram.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libc/include/GuestWriteWatch.hpp"
#include "BdaAbi.hpp"
#include "ImageTableAbi.hpp"
#include <spirv/unified1/spirv.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace ShaderRecompiler;

constexpr std::uint32_t Format8888UNorm = 56;
constexpr std::uint32_t Type2D = 9;

const std::vector<std::uint32_t> WholeCode{0xf4080100u, 0xfa000000u, 0xf4080200u, 0xfa000010u, 0xf4080300u, 0xfa000020u, 0xf4080700u, 0xfa000030u, 0x7e200500u, 0x8f108510u, 0xf42c0502u, 0x20000000u, 0xf09c0f08u, 0x00450000u, 0xe0700000u, 0x80070000u, 0xbf810000u};

std::array<std::uint32_t, 8> T(std::uint64_t address, std::uint32_t height) {
    return {static_cast<std::uint32_t>(address >> 8u), static_cast<std::uint32_t>((address >> 40u) & 0xffu) | (Format8888UNorm << 20u) | (3u << 30u), height << 14u, 0xfacu | (Type2D << 28u), 0u, 0u, 0u, 0u};
}

std::array<std::uint32_t, 4> V(const void* base, std::uint32_t stride, std::uint32_t records) {
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(base));
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (stride << 16u), records, stride == 0u ? 0x31016facu : 0x01016facu};
}

struct Guest {
    std::array<std::array<std::uint32_t, 4>, 3> materials{};
    std::array<std::uint32_t, 4> output{};
    std::array<std::uint32_t, 20> srt{};
};

void FillSrt(Guest& guest, const void* heap, std::uint32_t records) {
    const auto heapV = V(heap, 32u, records);
    const auto materialV = V(guest.materials.data(), 16u, 3u);
    const auto outputV = V(guest.output.data(), 0u, 16u);
    std::copy(heapV.begin(), heapV.end(), guest.srt.begin());
    std::fill(guest.srt.begin() + 4, guest.srt.begin() + 8, 0u);
    std::copy(materialV.begin(), materialV.end(), guest.srt.begin() + 8);
    std::copy(outputV.begin(), outputV.end(), guest.srt.begin() + 12);
}

RecompileRequest Request(const Guest& guest) {
    static constexpr std::array<std::uint32_t, 4> capabilities{29u, spv::CapabilityInt64, spv::CapabilityPhysicalStorageBufferAddresses, spv::CapabilityStorageBuffer8BitAccess};
    static constexpr std::array<std::string_view, 2> extensions{"SPV_KHR_physical_storage_buffer", "SPV_KHR_8bit_storage"};
    static std::array<std::uint32_t, 2> userData{};
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(guest.srt.data()));
    userData = {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>(address >> 32u)};
    RecompileRequest request{};
    request.shader = {ShaderStage::Compute, 0x7a4000u, WholeCode, 0, {}};
    request.context.waveSize = 64;
    request.context.userDataBaseRegister = 0;
    request.context.userData = userData;
    request.context.compute = ShaderComputeStageInfo{{64u, 1u, 1u}, 0u, {false, false, false}, false, 1u};
    request.target.vulkanVersion = 0x00401000u;
    request.target.spirvVersion = 0x00010300u;
    request.target.subgroupSize = 32u;
    request.target.supportedCapabilities = capabilities;
    request.target.supportedExtensions = extensions;
    request.target.bdaAbiVersion = BdaAbi::Version;
    request.target.fragmentShaderBarycentricEnabled = false;
    request.layout.pushConstantSizeBytes = 128;
    return request;
}

std::uint32_t Setting(const char* name, std::uint32_t fallback) {
    const char* value = std::getenv(name);
    return value != nullptr ? static_cast<std::uint32_t>(std::strtoul(value, nullptr, 10)) : fallback;
}

struct Counts {
    std::uint64_t unwritten = 0, compared = 0, wordsRead = 0, memoHits = 0, memoLayouts = 0, memoMisses = 0;
};

Counts Now() {
    Counts counts;
#ifdef BENCH_V3
    const auto reuse = ResourceMaterializer::TableReuse();
    counts.unwritten = reuse.unwritten;
    counts.compared = reuse.compared;
    counts.wordsRead = reuse.wordsRead;
#endif
#ifdef BENCH_6440
    const auto memo = ResourceMaterializer::ColumnMemoCounts();
    counts.memoHits = memo.hits;
    counts.memoLayouts = memo.layoutsReused;
    counts.memoMisses = memo.misses;
#endif
    return counts;
}

double Percentile(std::vector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1u) + 0.5);
    return values[std::min(index, values.size() - 1u)];
}

std::uint32_t* WatchedBlock(std::size_t bytes) {
    auto* block = static_cast<std::uint32_t*>(GuestArena::GuestArenaAllocate_nid_postfix(bytes, 65536));
    if (block == nullptr) throw std::runtime_error("table capture bench: cannot allocate the guest block");
    GuestArena::GuestArenaCommit_nid_postfix(block, bytes, 0x04u, bytes);
#ifndef _WIN32
    GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(block, bytes);
#endif
    return block;
}

}

int main() {
    const auto records = Setting("BENCH_RECORDS", 19000);
    const auto samples = Setting("BENCH_SAMPLES", 200);
    const auto warmup = Setting("BENCH_WARMUP", 5);
    const auto tables = Setting("BENCH_TABLES", 40);
    const auto shifts = samples + warmup + 2u;
    const std::size_t tableBytes = (static_cast<std::size_t>(records + shifts) * 32u + 65535u) & ~static_cast<std::size_t>(65535);
    auto* block = WatchedBlock(tableBytes * (tables + 1u));
    const auto fill = [&](std::uint32_t* table, std::uint32_t count, std::uint32_t salt) {
        for (std::uint32_t record = 0; record < count; record++) {
            const auto words = T(0x100000000ull + static_cast<std::uint64_t>((record + salt) % 1024u) * 256u, 1u + record % 5u);
            std::memcpy(table + static_cast<std::size_t>(record) * 8u, words.data(), sizeof(words));
        }
    };
    const auto tableAt = [&](std::uint32_t index) { return block + index * (tableBytes / sizeof(std::uint32_t)); };
    for (std::uint32_t index = 0; index <= tables; index++) fill(tableAt(index), records + shifts, index);
    std::printf("CAPTUREBENCH records %u samples %u warmup %u tables %u watched %d collect %llu\n", records, samples, warmup, tables, AgcDriver::GuestMemory::Watched(reinterpret_cast<std::uintptr_t>(block), tableBytes) ? 1 : 0,
        static_cast<unsigned long long>(AgcDriver::GuestMemory::CollectWrites(reinterpret_cast<std::uintptr_t>(block), tableBytes)));
    static Guest guest;
    const auto capture = [&](const std::uint32_t* heap) {
        FillSrt(guest, heap, records);
        const auto request = Request(guest);
        const auto start = std::chrono::steady_clock::now();
        {
            AgcDriver::ShaderMemory memory({});
            const auto captured = memory.Capture(request);
            if (captured->snapshot.tables.tables.empty() || captured->snapshot.tables.tables.at(0).keys != records) throw std::runtime_error("table capture bench: the capture did not snapshot the table");
        }
        return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
    };
    const auto run = [&](const char* name, auto&& before, auto&& heapOf) {
        for (std::uint32_t round = 0; round < warmup; round++) {
            before(round);
            capture(heapOf(round));
        }
        const auto counts = Now();
        std::vector<double> times;
        times.reserve(samples);
        for (std::uint32_t sample = 0; sample < samples; sample++) {
            before(warmup + sample);
            times.push_back(capture(heapOf(warmup + sample)));
        }
        const auto after = Now();
        double sum = 0;
        for (const auto time : times) sum += time;
        std::printf("CAPTUREBENCH %-14s n=%u p50=%.1f p90=%.1f max=%.1f mean=%.1f us | v3 unwritten %llu compared %llu words %llu | memo hits %llu layouts %llu misses %llu\n", name, samples, Percentile(times, 0.5), Percentile(times, 0.9), *std::max_element(times.begin(), times.end()), sum / samples,
            static_cast<unsigned long long>(after.unwritten - counts.unwritten), static_cast<unsigned long long>(after.compared - counts.compared), static_cast<unsigned long long>(after.wordsRead - counts.wordsRead), static_cast<unsigned long long>(after.memoHits - counts.memoHits),
            static_cast<unsigned long long>(after.memoLayouts - counts.memoLayouts), static_cast<unsigned long long>(after.memoMisses - counts.memoMisses));
        std::fflush(stdout);
    };
    auto* heap = tableAt(0);
    const std::array<std::uint32_t, 8> original = [&] {
        std::array<std::uint32_t, 8> words{};
        std::memcpy(words.data(), heap, sizeof(words));
        return words;
    }();
    try {
        run("unchanged", [](std::uint32_t) {}, [&](std::uint32_t) { return heap; });
        run("written-equal", [&](std::uint32_t round) {
            auto* record = heap + static_cast<std::size_t>((round * 7919u) % records) * 8u;
            std::array<std::uint32_t, 8> words{};
            std::memcpy(words.data(), record, sizeof(words));
            std::memcpy(record, words.data(), sizeof(words));
        }, [&](std::uint32_t) { return heap; });
        run("one-changed", [&](std::uint32_t round) {
            auto* record = heap + static_cast<std::size_t>((round * 7919u) % records) * 8u;
            const auto words = T(0x200000000ull + static_cast<std::uint64_t>(round % 64u) * 256u, 1u + round % 5u);
            std::memcpy(record, words.data(), sizeof(words));
        }, [&](std::uint32_t) { return heap; });
        std::memcpy(heap, original.data(), sizeof(original));
        run("many-tables", [](std::uint32_t) {}, [&](std::uint32_t round) { return tableAt(1u + round % tables); });
        run("cold-base", [](std::uint32_t) {}, [&](std::uint32_t round) { return tableAt(0) + static_cast<std::size_t>(round + 1u) * 8u; });
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
    return 0;
}
```

## 2. `Execution/include/StepTimers.hpp`

This is PM-038's step timers (v3 commit 08215003c) with two changes: each call is binned in 1/8-octave buckets with a maximum kept, and the first 10 s window is dropped as warm-up. A summary with p50, p90 and maximum is printed at exit.

```cpp
#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_STEPTIMERS_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_STEPTIMERS_HPP

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace AgcDriver {

constexpr std::size_t StepBuckets = 320;

struct StepSite {
    const char* name;
    std::atomic<std::uint64_t> nanoseconds{0};
    std::atomic<std::uint64_t> calls{0};
    StepSite* next = nullptr;
    std::array<std::atomic<std::uint64_t>, StepBuckets> buckets{};
    std::atomic<std::uint64_t> steadyCalls{0};
    std::atomic<std::uint64_t> steadyNanoseconds{0};
    std::atomic<std::uint64_t> worst{0};
};

inline std::atomic<StepSite*>& StepSites() {
    static std::atomic<StepSite*> head{nullptr};
    return head;
}

inline std::atomic<bool>& StepsSteady() {
    static std::atomic<bool> steady{false};
    return steady;
}

inline bool StepsTimed() {
    static const bool timed = std::getenv("APS5_PROFILE_DRAW") != nullptr;
    return timed;
}

inline std::size_t StepBucket(std::uint64_t nanoseconds) {
    if (nanoseconds < 16u) return 0;
    const auto index = static_cast<std::size_t>(std::log2(static_cast<double>(nanoseconds)) * 8.0) - 32u;
    return index < StepBuckets ? index : StepBuckets - 1u;
}

inline double StepBucketMicroseconds(std::size_t bucket) {
    return std::exp2((static_cast<double>(bucket) + 32.5) / 8.0) / 1000.0;
}

inline void SummarizeSteps() {
    for (auto* site = StepSites().load(std::memory_order_acquire); site != nullptr; site = site->next) {
        const auto count = site->steadyCalls.load(std::memory_order_relaxed);
        if (count == 0) continue;
        const auto rank = [&](double fraction) {
            const auto target = static_cast<std::uint64_t>(std::ceil(fraction * static_cast<double>(count)));
            std::uint64_t seen = 0;
            for (std::size_t bucket = 0; bucket < StepBuckets; ++bucket) {
                seen += site->buckets[bucket].load(std::memory_order_relaxed);
                if (seen >= target) return StepBucketMicroseconds(bucket);
            }
            return StepBucketMicroseconds(StepBuckets - 1u);
        };
        std::fprintf(stderr, "[steps-summary] %s: n=%llu mean=%.1f p50=%.1f p90=%.1f max=%.1f us\n", site->name, static_cast<unsigned long long>(count), static_cast<double>(site->steadyNanoseconds.load(std::memory_order_relaxed)) / 1000.0 / static_cast<double>(count), rank(0.5), rank(0.9),
            static_cast<double>(site->worst.load(std::memory_order_relaxed)) / 1000.0);
    }
}

inline StepSite& RegisterStep(StepSite& site) {
    static const bool summarized = [] {
        std::atexit(&SummarizeSteps);
        return true;
    }();
    static_cast<void>(summarized);
    site.next = StepSites().load(std::memory_order_relaxed);
    while (!StepSites().compare_exchange_weak(site.next, &site, std::memory_order_release, std::memory_order_relaxed)) {
    }
    return site;
}

inline void ReportSteps(std::chrono::steady_clock::time_point now) {
    static std::atomic<std::int64_t> lastReport{0};
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    auto last = lastReport.load(std::memory_order_relaxed);
    if (last == 0) {
        lastReport.compare_exchange_strong(last, seconds);
        return;
    }
    if (seconds - last < 10 || !lastReport.compare_exchange_strong(last, seconds)) return;
    StepsSteady().store(true, std::memory_order_relaxed);
    std::string report;
    for (auto* site = StepSites().load(std::memory_order_acquire); site != nullptr; site = site->next) {
        const auto count = site->calls.exchange(0, std::memory_order_relaxed);
        const auto total = site->nanoseconds.exchange(0, std::memory_order_relaxed);
        if (count == 0) continue;
        char text[160];
        std::snprintf(text, sizeof(text), " %s %.1f us x%llu;", site->name, static_cast<double>(total) / 1000.0 / static_cast<double>(count), static_cast<unsigned long long>(count));
        report += text;
    }
    std::fprintf(stderr, "[steps] per call (10 s):%s\n", report.c_str());
}

class StepTimer {
public:
    explicit StepTimer(StepSite& site) : site(site), enabled(StepsTimed()), start(enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}) {}
    ~StepTimer() {
        if (!enabled) return;
        const auto now = std::chrono::steady_clock::now();
        const auto spent = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count());
        site.nanoseconds.fetch_add(spent, std::memory_order_relaxed);
        site.calls.fetch_add(1, std::memory_order_relaxed);
        if (StepsSteady().load(std::memory_order_relaxed)) {
            site.buckets[StepBucket(spent)].fetch_add(1, std::memory_order_relaxed);
            site.steadyCalls.fetch_add(1, std::memory_order_relaxed);
            site.steadyNanoseconds.fetch_add(spent, std::memory_order_relaxed);
            auto worst = site.worst.load(std::memory_order_relaxed);
            while (worst < spent && !site.worst.compare_exchange_weak(worst, spent, std::memory_order_relaxed)) {
            }
        }
        ReportSteps(now);
    }
    StepTimer(const StepTimer&) = delete;
    StepTimer& operator=(const StepTimer&) = delete;

private:
    StepSite& site;
    bool enabled;
    std::chrono::steady_clock::time_point start;
};

#define APS5_STEP_JOIN(a, b) a##b
#define APS5_STEP_NAME(a, b) APS5_STEP_JOIN(a, b)
#define APS5_STEP(text)                                                                                            \
    static ::AgcDriver::StepSite& APS5_STEP_NAME(stepSite, __LINE__) = ::AgcDriver::RegisterStep(*new ::AgcDriver::StepSite{text}); \
    const ::AgcDriver::StepTimer APS5_STEP_NAME(stepTimer, __LINE__)(APS5_STEP_NAME(stepSite, __LINE__))

}

#endif
```

## 3. Anchors (one line each, plus the header include)

| File | Inserted line | Where |
|---|---|---|
| `Execution/src/Driver/Dispatch/Dispatch.cpp` | `APS5_STEP("dispatch: capture (miss)");` | before `return shaderMemory->Capture(invocation);` |
| `Execution/src/Driver/Dispatch/DispatchLookup.cpp` | `APS5_STEP("dispatch: compare variants");` | first statement inside `if (!stampValidate()) {` |
| `Execution/src/Driver/Dispatch/DispatchInsert.cpp` | `APS5_STEP("dispatch: insert (miss)");` | first statement of `Driver::insertDispatch` |
| `Graphics/src/ShaderResources.cpp` | `APS5_STEP("resolution: resolveImageTables");` | first statement of `ShaderResources::resolveImageTables` |
| `Graphics/src/ShaderResources.cpp` (addendum, §3.5 only) | `APS5_STEP("resolution: entry checks");` | in the fresh resolution's per-entry `try` block: before the `if (!GuestMemory::Accessible(...` line that throws "the table entry's surface ... is not mapped" (main, v3), or before `RequireSampledImageWithinDeviceLimits(context, resource, geometry, binding.imageDepthCompare.at(element));` (#6481 and v3 + #6481). It times the rest of the block: the mapping check or #6481's two checks, then `ResolveTextureFormat`. |

## 4. `core/libs/prx/libSceAgcDriver/CMakeLists.txt` (appended)

```cmake
add_test_executable(agc_table_capture_bench EXCLUDE_FROM_ALL <path>/TableCaptureBench.cpp Execution/src/ShaderMemory.cpp Execution/src/GuestMemory.cpp)
target_include_directories(agc_table_capture_bench PRIVATE ${LIBS_INCLUDE_DIR})
target_link_libraries(agc_table_capture_bench PRIVATE shader_recompiler_agc_driver libc)
set_target_properties(agc_table_capture_bench PROPERTIES CXX_EXTENSIONS OFF)
target_compile_definitions(agc_table_capture_bench PRIVATE BENCH_V3 BENCH_6440)
add_test_executable(agc_image_table_village_bench EXCLUDE_FROM_ALL <path>/ImageTableVillageBench.cpp)
target_include_directories(agc_image_table_village_bench PRIVATE ${LIBS_INCLUDE_DIR} ${CMAKE_SOURCE_DIR}/core/shader/recompiler ${ANYPS5_VULKAN_HEADERS_INCLUDE_DIR} ${ANYPS5_SPIRV_HEADERS_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests/execution)
target_link_libraries(agc_image_table_village_bench PRIVATE libSceAgcDriver SDL2-static libc)
set_target_properties(agc_image_table_village_bench PROPERTIES CXX_EXTENSIONS OFF)
```

The compile definitions are `BENCH_V3` where the tree has `ResourceMaterializer::TableReuse()` (v3 and the merges) and `BENCH_6440` where it has `ColumnMemoCounts()` (#6440 and its merges). `ImageTableVillageBench.cpp` is v3 commit 06a5ca05a's file, unchanged.
