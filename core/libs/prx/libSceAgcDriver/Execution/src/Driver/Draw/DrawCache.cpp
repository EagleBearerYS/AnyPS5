#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawCache.hpp"
#include <algorithm>
#include <bit>
#include "prx/libSceAgcDriver/Execution/include/Driver/Synchronization/DeferredLabels.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Memory/WriteEvidence.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/ShaderMemory.hpp"
#include <cstdlib>

namespace AgcDriver::DriverDetail {

namespace {

template<typename T>
std::uint64_t VectorBytes(const std::vector<T>& values) {
    return static_cast<std::uint64_t>(values.capacity()) * sizeof(T);
}

std::uint64_t BitBytes(std::initializer_list<const std::vector<bool>*> vectors) {
    std::uint64_t bits = 0;
    for (const auto* vector : vectors) bits += vector->capacity();
    return (bits + 7u) / 8u;
}

std::uint64_t ResultBytes(const ShaderRecompiler::RecompileResult& result) {
    std::uint64_t bytes = sizeof(ShaderRecompiler::RecompileResult) + VectorBytes(result.vertexInputPatches) + VectorBytes(result.runtimeImageResources) + VectorBytes(result.vertexInputs) + VectorBytes(result.parameterExports) + VectorBytes(result.fragmentParameters) + VectorBytes(result.stencilReferences) + VectorBytes(result.specialization) + VectorBytes(result.bindings) + VectorBytes(result.pushConstants) + VectorBytes(result.vertexAttributes) + VectorBytes(result.imageTablePoison) + VectorBytes(result.imageTableRanges) + VectorBytes(result.imageTableReadRanges) + VectorBytes(result.shaderDataUserWords);
    for (const auto& ranges : result.imageTableReadRanges) bytes += VectorBytes(ranges);
    for (const auto& binding : result.bindings) bytes += VectorBytes(binding.guestDescriptor) + VectorBytes(binding.imageShapes) + BitBytes({&binding.samplerDepthCompare, &binding.imageWritten, &binding.imageDepthCompare, &binding.imageAtomic, &binding.imageAtomic64, &binding.bufferAtomic, &binding.bufferWritten});
    return bytes;
}

std::uint64_t DecodeBytes(const DrawDecode& decode) {
    std::uint64_t bytes = sizeof(DrawDecode) + VectorBytes(decode.programs) + VectorBytes(decode.roles);
    for (const auto& program : decode.programs) bytes += VectorBytes(program.userData);
    return bytes;
}

std::vector<std::pair<const void*, std::uint64_t>> SharedParts(const DrawEntry& entry) {
    std::vector<std::pair<const void*, std::uint64_t>> parts;
    const auto add = [&](const void* pointer, const auto& bytes) {
        if (pointer == nullptr || std::any_of(parts.begin(), parts.end(), [&](const auto& part) { return part.first == pointer; })) return;
        parts.emplace_back(pointer, bytes());
    };
    if (entry.decode != nullptr) add(entry.decode.get(), [&] { return DecodeBytes(*entry.decode); });
    for (const auto& variants : entry.stages) {
        for (const auto& variant : variants) {
            if (variant == nullptr) continue;
            if (variant->compiled != nullptr) {
                add(variant->compiled.get(), [&] { return ResultBytes(*variant->compiled); });
                const auto words = variant->compiled->spirv.Words();
                if (!words.empty()) add(words.data(), [&] { return static_cast<std::uint64_t>(words.size_bytes()); });
            }
            if (variant->memory != nullptr) add(variant->memory.get(), [&] { return static_cast<std::uint64_t>(variant->memory->HeldBytes()); });
        }
    }
    return parts;
}


}

bool DrawRecipeRecord::Matches(const std::vector<std::shared_ptr<DispatchVariant>>& variants) const {
    if (stages.size() != variants.size()) return false;
    for (std::size_t i = 0; i < stages.size(); ++i) {
        if (stages[i].owner_before(variants[i]) || variants[i].owner_before(stages[i])) return false;
    }
    return true;
}

bool DrawRecipeRecord::Expired() const {
    return std::any_of(stages.begin(), stages.end(), [](const std::weak_ptr<const DispatchVariant>& stage) { return stage.expired(); });
}

bool Driver::drawEntries() {
    static const bool entries = std::getenv("APS5_NO_DRAW_SRT_ENTRIES") == nullptr && !stampValidate();
    return entries;
}

bool Driver::drawDataHits() {
    static const bool hits = std::getenv("APS5_NO_DRAW_DATA_HITS") == nullptr;
    return hits;
}

bool Driver::cacheCpuIndirect() {
    static const bool keep = [] {
        const char* text = std::getenv("APS5_CACHE_CPU_INDIRECT");
        return text != nullptr && text[0] == '1';
    }();
    return keep;
}

bool Driver::drawStageDataHits() {
    static const bool hits = std::getenv("APS5_NO_DRAW_STAGE_DATA_HITS") == nullptr && !stampValidate();
    return hits;
}

bool Driver::verifyDrawDataHits() {
    static const bool verify = std::getenv("APS5_VERIFY_DRAW_DATA_HITS") != nullptr;
    return verify;
}

bool Driver::verifyDrawEntries() {
    static const bool verify = std::getenv("APS5_VERIFY_DRAW_ENTRIES") != nullptr;
    return verify;
}

bool Driver::registerKeyEnabled() {
    static const bool registerKey = std::getenv("APS5_NO_DRAW_KEY") == nullptr;
    return registerKey;
}

bool Driver::verifyDrawRecipe() {
    static const bool verify = std::getenv("APS5_VERIFY_DRAW_RECIPE") != nullptr;
    return verify;
}

std::uint64_t Driver::cpuIndirectBudget() {
    static const std::uint64_t budget = [] {
        const char* text = std::getenv("APS5_CPU_INDIRECT_CACHE_KIB");
        const auto parsed = text != nullptr ? std::strtoull(text, nullptr, 10) : 0ull;
        return (parsed != 0 ? parsed : 768ull * 1024ull) * 1024ull;
    }();
    return budget;
}

std::uint64_t Driver::cpuIndirectIdleLookups() {
    return 4u * static_cast<std::uint64_t>(drawCacheEntries());
}

std::uint64_t Driver::cpuIndirectOwnBytes(const DrawEntry& entry) {
    std::uint64_t bytes = 0;
    for (const auto& variants : entry.stages) {
        for (const auto& variant : variants) {
            if (variant != nullptr) bytes += variantBytes(*variant);
        }
    }
    return bytes;
}

std::uint64_t Driver::cpuIndirectCharge(const DrawEntry& entry) const {
    std::uint64_t bytes = cpuIndirectOwnBytes(entry);
    for (const auto& [pointer, partBytes] : SharedParts(entry)) {
        if (!cpuIndirectPins.contains(pointer)) bytes += partBytes;
    }
    return bytes;
}

void Driver::pinCpuIndirect(DrawEntry& entry) {
    entry.ownBytes = cpuIndirectOwnBytes(entry);
    entry.pinned = SharedParts(entry);
    cpuIndirectBytes += entry.ownBytes;
    for (const auto& [pointer, bytes] : entry.pinned) {
        auto& pin = cpuIndirectPins[pointer];
        if (pin.first++ == 0) {
            pin.second = bytes;
            cpuIndirectBytes += bytes;
        }
    }
    ++cpuIndirectEntries;
}

void Driver::unpinCpuIndirect(DrawEntry& entry) {
    cpuIndirectBytes -= entry.ownBytes;
    for (const auto& [pointer, bytes] : entry.pinned) {
        const auto pin = cpuIndirectPins.find(pointer);
        if (pin == cpuIndirectPins.end() || --pin->second.first != 0) continue;
        cpuIndirectBytes -= pin->second.second;
        cpuIndirectPins.erase(pin);
    }
    --cpuIndirectEntries;
}

void Driver::sweepCpuIndirect() {
    const auto idle = cpuIndirectIdleLookups();
    if (drawLookupSerial - cpuIndirectSweptAt < idle / 4u) return;
    cpuIndirectSweptAt = drawLookupSerial;
    for (auto found = drawCache.begin(); found != drawCache.end();) {
        const auto next = std::next(found);
        if (found->second->cpuIndirect && drawLookupSerial - found->second->lastUse > idle) {
            dropDrawEntry(found);
            ++drawEntryCounters.cpuIndirectSwept;
        }
        found = next;
    }
}

Driver::CpuIndirectFootprint Driver::CpuIndirectCacheSize() {
    std::lock_guard cacheLock(drawCacheMutex);
    return {cpuIndirectEntries, cpuIndirectBytes, cpuIndirectBudget()};
}

std::size_t Driver::drawCacheEntries() {
    static const std::size_t entries = [] {
        const char* text = std::getenv("APS5_DRAW_CACHE_ENTRIES");
        const auto parsed = text != nullptr ? std::strtoull(text, nullptr, 10) : 0ull;
        return parsed != 0 ? static_cast<std::size_t>(parsed) : std::size_t{4096};
    }();
    return entries;
}

void Driver::accountDrawVariant(const DispatchVariant& variant, bool added) {
    if (added) {
        ++drawCacheVariants;
        drawCacheVariantBytes += variantBytes(variant);
    } else {
        --drawCacheVariants;
        drawCacheVariantBytes -= variantBytes(variant);
    }
}

DrawKeySightings::DrawKeySightings(std::size_t keyCount) {
    Graphics::Require(keyCount >= Ways, "draw key sightings need at least one bucket");
    const auto buckets = std::bit_ceil(keyCount / Ways);
    keys.assign(buckets * Ways, 0);
    filled.assign(buckets, 0);
    next.assign(buckets, 0);
    bucketMask = buckets - 1;
}

bool DrawKeySightings::SeenBefore(std::uint64_t key) {
    auto mixed = key;
    mixed = (mixed ^ (mixed >> 30u)) * 0xbf58476d1ce4e5b9ull;
    mixed = (mixed ^ (mixed >> 27u)) * 0x94d049bb133111ebull;
    mixed ^= mixed >> 31u;
    const auto bucket = static_cast<std::size_t>(mixed) & bucketMask;
    auto* ways = keys.data() + bucket * Ways;
    if (std::find(ways, ways + filled[bucket], key) != ways + filled[bucket]) return true;
    ways[next[bucket]] = key;
    next[bucket] = static_cast<std::uint8_t>((next[bucket] + 1u) % Ways);
    if (filled[bucket] < Ways) ++filled[bucket];
    return false;
}

bool Driver::admitDrawKey(std::uint64_t key, std::uint64_t shape) {
    std::lock_guard cacheLock(drawCacheMutex);
    if (drawCache.contains(key)) return true;
    if (!drawKeySightings) drawKeySightings.emplace(drawCacheEntries() * DrawKeySightings::Ways);
    bool seen = drawKeySightings->SeenBefore(key);
    const auto& pending = drawRelocationPending;
    if (pending.key == key && pending.identity != 0) {
        if (pending.alias != 0) {
            const auto trial = drawRelocationTrials.find(pending.identity);
            if (drawKeySightings->SeenBefore(pending.alias ^ 0x9e3779b97f4a7c15ull) && trial != drawRelocationTrials.end()) {
                auto& state = trial->second;
                if (state.productive || state.hits != 0 || state.admissions < DrawRelocationTrial::ProbationAdmissions) {
                    if (!state.productive && state.hits == 0) ++state.admissions;
                    seen = true;
                }
            }
        } else if (!drawRelocations.contains(pending.identity)) {
            if (drawKeySightings->SeenBefore(pending.identity ^ 0xc2b2ae3d27d4eb4full)) seen = true;
        }
    }
    if (seen || shape == 0 || drawShapes.contains(shape)) return seen;
    if (!drawShapeSightings) drawShapeSightings.emplace(drawCacheEntries() * DrawKeySightings::Ways);
    return drawShapeSightings->SeenBefore(shape);
}

void Driver::insertDrawEntry(std::uint64_t key, std::vector<std::shared_ptr<DispatchVariant>>& fresh, std::shared_ptr<const DrawDecode> decode, std::uint64_t shape, bool cpuIndirect) {
    std::lock_guard cacheLock(drawCacheMutex);
    auto& counters = drawEntryCounters;
    if (cpuIndirect) sweepCpuIndirect();
    const auto found = drawCache.find(key);
    auto replacement = std::make_shared<DrawEntry>();
    replacement->stages.resize(fresh.size());
    replacement->decode = std::move(decode);
    replacement->shape = shape;
    if (found != drawCache.end()) {
        if (found->second->stages.size() == fresh.size()) replacement->stages = found->second->stages;
        if (found->second->decode != nullptr) replacement->decode = found->second->decode;
        replacement->recipes.store(found->second->recipes.load());
    }
    std::vector<std::shared_ptr<DispatchVariant>> added, evicted;
    for (std::size_t i = 0; i < fresh.size(); ++i) {
        if (fresh[i] == nullptr) continue;
        auto& variants = replacement->stages[i];
        const auto present = std::find_if(variants.begin(), variants.end(), [&](const std::shared_ptr<DispatchVariant>& kept) { return kept->pushOffset == fresh[i]->pushOffset && kept->meshIndexDescriptor == fresh[i]->meshIndexDescriptor && kept->runs == fresh[i]->runs && kept->words == fresh[i]->words; });
        if (present != variants.end()) {
            ++counters.present;
            fresh[i] = *present;
            continue;
        }
        variants.insert(variants.begin(), fresh[i]);
        added.push_back(fresh[i]);
        while (variants.size() > dispatchVariants()) {
            evicted.push_back(variants.back());
            variants.pop_back();
        }
    }
    const bool replacesCpuIndirect = found != drawCache.end() && found->second->cpuIndirect;
    if (cpuIndirect) {
        replacement->cpuIndirect = true;
        replacement->lastUse = drawLookupSerial;
        pinCpuIndirect(*replacement);
        if (replacesCpuIndirect) unpinCpuIndirect(*found->second);
        if (cpuIndirectBytes > cpuIndirectBudget()) {
            if (replacesCpuIndirect) pinCpuIndirect(*found->second);
            unpinCpuIndirect(*replacement);
            ++counters.cpuIndirectRefused;
            return;
        }
    } else if (replacesCpuIndirect) {
        unpinCpuIndirect(*found->second);
    }
    ++counters.inserts;
    for (const auto& variant : added) accountDrawVariant(*variant, true);
    for (const auto& variant : evicted) accountDrawVariant(*variant, false);
    counters.variantsInserted += added.size();
    counters.variantsEvicted += evicted.size();
    replacement->touched = drawCacheHits;
    if (replacement->decode != nullptr && shape != 0) drawShapes[shape] = key;
    if (found == drawCache.end()) {
        drawOrder.push_front(key);
        replacement->order = drawOrder.begin();
        drawCache.emplace(key, std::move(replacement));
    } else {
        replacement->order = found->second->order;
        drawOrder.splice(drawOrder.begin(), drawOrder, replacement->order);
        found->second = std::move(replacement);
    }
    while (drawCache.size() > drawCacheEntries()) {
        const auto last = drawCache.find(drawOrder.back());
        if (traceDrawCache()) traceInsert(traceKeysEvicted, last->first);
        dropDrawEntry(last);
        ++drawCacheEvictions;
        ++counters.evictions;
    }
}

void Driver::dropDrawEntry(std::unordered_map<std::uint64_t, std::shared_ptr<DrawEntry>>::iterator found) {
    for (const auto& variants : found->second->stages) {
        for (const auto& variant : variants) accountDrawVariant(*variant, false);
    }
    if (found->second->cpuIndirect) unpinCpuIndirect(*found->second);
    if (const auto shapeOf = drawShapes.find(found->second->shape); shapeOf != drawShapes.end() && shapeOf->second == found->first) drawShapes.erase(shapeOf);
    drawOrder.erase(found->second->order);
    drawCache.erase(found);
}

void Driver::eraseDrawEntry(std::uint64_t key, const std::shared_ptr<DrawEntry>& expected) {
    const auto found = drawCache.find(key);
    if (found == drawCache.end() || found->second != expected) return;
    dropDrawEntry(found);
}

bool DecodeProgramsCurrent(const DrawDecode& decode, const ShaderRegistry&) {
    return std::all_of(decode.programs.begin(), decode.programs.end(), [&](const DrawProgram& program) {
        if (!program.snapshot->header.empty()) return true;
        return ReadRawShader(program.snapshot->codeAddress + program.codeOffset * sizeof(std::uint32_t)) == program.snapshot &&
            (program.tailContinuation == nullptr || ReadRawShader(program.tailContinuation->codeAddress) == program.tailContinuation);
    });
}

std::shared_ptr<const DrawRecipe> Driver::findDrawRecipe(std::uint64_t key, const std::vector<std::shared_ptr<DispatchVariant>>& stages) {
    std::shared_ptr<const std::vector<DrawRecipeRecord>> records;
    {
        std::lock_guard cacheLock(drawCacheMutex);
        const auto found = drawCache.find(key);
        if (found == drawCache.end()) return nullptr;
        records = found->second->recipes.load();
    }
    if (records == nullptr) return nullptr;
    for (const auto& record : *records) {
        if (record.Matches(stages)) return record.recipe;
    }
    return nullptr;
}

void Driver::attachDrawRecipe(std::uint64_t key, const std::vector<std::shared_ptr<DispatchVariant>>& stages, std::shared_ptr<const DrawRecipe> recipe) {
    std::shared_ptr<DrawEntry> entry;
    {
        std::lock_guard cacheLock(drawCacheMutex);
        const auto found = drawCache.find(key);
        if (found == drawCache.end()) return;
        entry = found->second;
    }
    const auto old = entry->recipes.load();
    auto records = std::make_shared<std::vector<DrawRecipeRecord>>();
    records->push_back({std::vector<std::weak_ptr<const DispatchVariant>>(stages.begin(), stages.end()), std::move(recipe)});
    if (old != nullptr) {
        for (const auto& record : *old) {
            if (record.Matches(stages) || record.Expired() || records->size() >= dispatchVariants()) continue;
            records->push_back(record);
        }
    }
    entry->recipes.store(std::move(records));
    VulkanDevice::NoteRecipe(VulkanDevice::RecipeEvent::Attach, VulkanDevice::RecipeKind::Draw);
}

}
