#include "prx/libSceAgcDriver/Execution/include/ProfileOutput.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/ShaderMemory.hpp"
#include <cstdlib>
#include <set>

namespace AgcDriver::DriverDetail {

bool Driver::traceDrawCache() {
    static const bool trace = std::getenv("APS5_TRACE_DRAW_CACHE") != nullptr;
    return trace;
}

void Driver::traceInsert(std::unordered_set<std::uint64_t>& set, std::uint64_t key) {
    if (set.size() >= (1u << 16u)) set.clear();
    set.insert(key);
}

void Driver::maybeReportDrawCache(bool profile) {
    auto& counters = drawEntryCounters;
    if (!profile && !traceDrawCache()) return;
    const auto now = std::chrono::steady_clock::now();
    if (now - counters.lastReport < std::chrono::seconds(10)) return;
    reportDrawCache(counters, profile);
    counters.lastReport = now;
}

void Driver::reportDrawCache(DrawEntryCounters& counters, bool profile) {
    const auto count = [](std::uint64_t value) { return static_cast<unsigned long long>(value); };
    const auto miss = [&](DrawMiss reason) { return count(counters.misses[static_cast<std::size_t>(reason)]); };
    std::string text;
    if (traceDrawCache()) {
        char line[1536];
        std::uint64_t misses = 0;
        for (const auto value : counters.misses) misses += value;
        std::snprintf(line, sizeof(line), "[draw-cache] 10 s: %llu lookups, %llu hits (%.1f%%), %llu stage misses, %llu without an entry, %llu data-only hits (stages: %llu reused, %llu compiled; %llu verified); no entry: new registers %llu, user words/pointers only %llu, evicted %llu, seen but never inserted %llu; stage misses: front differing %llu, fragment differing %llu, other differing %llu, layout %llu, gate %llu, stage count %llu; differing captures: same runs %llu (%.1f words changed each), other runs %llu; %zu entries, %llu evicted\n", count(counters.lookups), count(counters.hits), counters.lookups != 0 ? 100.0 * static_cast<double>(counters.hits) / static_cast<double>(counters.lookups) : 0.0, count(misses), count(counters.absent), count(counters.dataHits), count(counters.dataStagesReused), count(counters.dataStagesCompiled), count(counters.dataVerified), count(counters.absentNewRegisters), count(counters.absentUserWords), count(counters.absentEvicted), count(counters.absentNeverInserted), miss(DrawMiss::FrontDiffering), miss(DrawMiss::FragmentDiffering), miss(DrawMiss::OtherDiffering), miss(DrawMiss::Layout), miss(DrawMiss::Gate), miss(DrawMiss::Stages), count(counters.differingSameRuns), counters.differingSameRuns != 0 ? static_cast<double>(counters.differingWords) / static_cast<double>(counters.differingSameRuns) : 0.0, count(counters.differingRunsChanged), drawCache.size(), count(counters.evictions));
        text += line;
        std::snprintf(line, sizeof(line), "[draw-cache] register-key miss component changes (10 s): stable registers %llu, user data %llu, auxiliary registers %llu, device %llu, first shader set %llu, unchanged components %llu\n", count(counters.registerKeyMissChanges[0]), count(counters.registerKeyMissChanges[1]), count(counters.registerKeyMissChanges[2]), count(counters.registerKeyMissChanges[3]), count(counters.registerKeyMissChanges[4]), count(counters.registerKeyMissChanges[5]));
        text += line;
    }
    if (profile) text += profileDrawCacheLines(counters);
    AgcDriver::ProfilePrint_nid_no_patch("%s", text.c_str());
    counters = DrawEntryCounters{};
}

DrawEntryCounters Driver::DrawCacheCounters() {
    std::lock_guard cacheLock(drawCacheMutex);
    return drawEntryCounters;
}

Driver::DispatchCacheFootprint Driver::DrawCacheSize() {
    std::lock_guard cacheLock(drawCacheMutex);
    return {drawCache.size(), drawCacheVariantBytes};
}

namespace {

template<typename T>
std::uint64_t VectorBytes(const std::vector<T>& values) {
    return static_cast<std::uint64_t>(values.capacity()) * sizeof(T);
}

std::uint64_t BoolVectorBytes(const std::vector<bool>& values) {
    return (static_cast<std::uint64_t>(values.capacity()) + 7u) / 8u;
}

std::uint64_t ResultBytes(const ShaderRecompiler::RecompileResult& result) {
    std::uint64_t bytes = sizeof(ShaderRecompiler::RecompileResult);
    bytes += VectorBytes(result.vertexInputPatches) + VectorBytes(result.runtimeImageResources) + VectorBytes(result.vertexInputs) + VectorBytes(result.parameterExports) + VectorBytes(result.fragmentParameters) + VectorBytes(result.stencilReferences) + VectorBytes(result.specialization) + VectorBytes(result.bindings) + VectorBytes(result.pushConstants) + VectorBytes(result.vertexAttributes) + VectorBytes(result.imageTablePoison) + VectorBytes(result.imageTableRanges) + VectorBytes(result.imageTableReadRanges) + VectorBytes(result.shaderDataUserWords);
    for (const auto& ranges : result.imageTableReadRanges) bytes += VectorBytes(ranges);
    for (const auto& binding : result.bindings) {
        bytes += VectorBytes(binding.guestDescriptor) + BoolVectorBytes(binding.samplerDepthCompare) + BoolVectorBytes(binding.imageWritten) + BoolVectorBytes(binding.imageDepthCompare) + BoolVectorBytes(binding.imageAtomic) + BoolVectorBytes(binding.imageAtomic64) + VectorBytes(binding.imageShapes) + BoolVectorBytes(binding.bufferAtomic) + BoolVectorBytes(binding.bufferWritten);
    }
    return bytes;
}

}

Driver::DrawCacheParts Driver::DrawCacheBreakdown() {
    std::lock_guard cacheLock(drawCacheMutex);
    DrawCacheParts parts;
    std::set<const void*> decodes, results, spirv, memories;
    parts.entries = drawCache.size();
    parts.accountedBytes = drawCacheVariantBytes;
    for (const auto& [key, entry] : drawCache) {
        if (entry->decode != nullptr && decodes.insert(entry->decode.get()).second) {
            const auto& decode = *entry->decode;
            parts.decodeBytes += sizeof(DrawDecode) + VectorBytes(decode.programs) + VectorBytes(decode.roles);
            for (const auto& program : decode.programs) parts.decodeBytes += VectorBytes(program.userData);
        }
        if (const auto records = entry->recipes.load(); records != nullptr) parts.recipes += records->size();
        for (const auto& variants : entry->stages) {
            ++parts.stages;
            ++parts.variantsPerStage[std::min(variants.size(), parts.variantsPerStage.size() - 1u)];
            for (const auto& variant : variants) {
                ++parts.variants;
                parts.variantBytes += variantBytes(*variant) + VectorBytes(variant->dataPositions) + VectorBytes(variant->dataSlots) + VectorBytes(variant->runBases) + VectorBytes(variant->shaderDataPatches) + VectorBytes(variant->spans);
                for (const auto& region : variant->captured) parts.capturedBytes += region.bytes.size();
                if (variant->compiled != nullptr && results.insert(variant->compiled.get()).second) {
                    parts.resultBytes += ResultBytes(*variant->compiled);
                    const auto words = variant->compiled->spirv.Words();
                    if (!words.empty() && spirv.insert(words.data()).second) parts.spirvBytes += words.size_bytes();
                }
                if (variant->memory != nullptr && memories.insert(variant->memory.get()).second) {
                    parts.pages += variant->memory->PageCount();
                    parts.memoryBytes += variant->memory->HeldBytes();
                }
            }
        }
    }
    parts.decodes = decodes.size();
    parts.results = results.size();
    parts.spirvModules = spirv.size();
    parts.memories = memories.size();
    return parts;
}

std::string Driver::profileDrawCacheLines(const DrawEntryCounters& counters) {
    std::string text;
    const auto append = [&](const char* format, auto... values) {
        char line[2048];
        std::snprintf(line, sizeof(line), format, values...);
        text += line;
    };
    const auto count = [](std::uint64_t value) { return static_cast<unsigned long long>(value); };
    const auto validated = counters.lookups > counters.absent ? counters.lookups - counters.absent : 0;
    const auto miss = [&](DrawMiss reason) { return count(counters.misses[static_cast<std::size_t>(reason)]); };
    std::string ranks;
    for (std::size_t rank = 0; rank < dispatchVariants(); ++rank) {
        char item[32];
        std::snprintf(item, sizeof(item), "%s%llu", rank == 0 ? "" : " / ", count(counters.variantHitsByRank[rank]));
        ranks += item;
    }
    append("[draw-cache] %llu lookups (10 s): %llu no entry, %llu validated in %.1f us each (key, lookup and every stage): %llu hits (every stage equal; stage hits by variant rank 1..k %s), misses by reason: front stage differing %llu, fragment differing %llu, other stage differing %llu, layout (push offset) %llu, gate %llu, stage count %llu; %llu stage validations (%llu equal, %.2f variants compared each); %llu inserts (%llu variants inserted, %llu evicted beyond k, %llu already present, %llu unstable), %zu entries (%llu variants, ~%.1f MiB), %llu evictions in total, %llu LRU moves; verify: %llu hits captured again, %llu stages differed\n", count(counters.lookups), count(counters.absent), count(validated), validated != 0 ? counters.validateUs / static_cast<double>(validated) : 0.0, count(counters.hits), ranks.c_str(), miss(DrawMiss::FrontDiffering), miss(DrawMiss::FragmentDiffering), miss(DrawMiss::OtherDiffering), miss(DrawMiss::Layout), miss(DrawMiss::Gate), miss(DrawMiss::Stages), count(counters.stageValidations), count(counters.stageEqual), counters.stageValidations != 0 ? static_cast<double>(counters.variantsCompared) / static_cast<double>(counters.stageValidations) : 0.0, count(counters.inserts), count(counters.variantsInserted), count(counters.variantsEvicted), count(counters.present), count(counters.unstable), drawCache.size(), count(drawCacheVariants), static_cast<double>(drawCacheVariantBytes) / (1024.0 * 1024.0), count(drawCacheEvictions), count(counters.touches), count(counters.verifyHits), count(counters.verifyMismatches));
    append("[draw-cache] register key (10 s): %llu lookups, %llu hits, key build %.1f us each; decode skipped %llu, partial (state/pixel/programs from the entry) %llu; facade log != table %llu; verify: %llu decodes compared, %llu differed\n", count(counters.registerKeyLookups), count(counters.registerKeyHits), counters.registerKeyLookups != 0 ? counters.keyUs / static_cast<double>(counters.registerKeyLookups) : 0.0, count(counters.decodeSkipped), count(counters.decodePartial), count(counters.facadeMismatches), count(counters.verifyDecodes), count(counters.verifyDecodeMismatches));
    append("[draw-cache] stage data hits (10 s): %llu stages (%llu words refreshed), verified %llu; stage inserts with data positions %llu (%.1f positions each)\n", count(counters.stageDataHits), count(counters.stageDataWords), count(counters.stageDataVerified), count(counters.stageDataInserts), counters.stageDataInserts != 0 ? static_cast<double>(counters.stageDataPositions) / static_cast<double>(counters.stageDataInserts) : 0.0);
    if (cacheCpuIndirect()) append("[draw-cache] CPU-path indirect stages (10 s): %llu record stages patched in their pushed words, %llu kept at the values they were compiled with, %llu captured again; entries: %zu held in %.1f of %.1f MiB, %llu refused over the budget, %llu idle ones swept\n", count(counters.cpuIndirectPatched), count(counters.cpuIndirectReused), count(counters.cpuIndirectCaptured), cpuIndirectEntries, static_cast<double>(cpuIndirectBytes) / (1024.0 * 1024.0), static_cast<double>(cpuIndirectBudget()) / (1024.0 * 1024.0), count(counters.cpuIndirectRefused), count(counters.cpuIndirectSwept));
    return text;
}

}
