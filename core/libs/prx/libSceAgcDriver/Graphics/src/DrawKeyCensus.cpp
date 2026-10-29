#include "prx/libSceAgcDriver/Graphics/include/DrawKeyCensus.hpp"
#include "prx/libSceAgcDriver/Execution/include/ProfileOutput.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace AgcDriver::Graphics::DrawKeyCensus {

namespace {

constexpr std::size_t CountTotal = static_cast<std::size_t>(Count::Total);
constexpr std::size_t MaxDrawsPerShape = 256;
constexpr std::size_t MaxWordedDraws = 131072;
constexpr std::size_t KeptFrames = 64;
constexpr std::size_t KeptWindows = 16;
constexpr std::size_t TopSlots = 8;
constexpr std::size_t MaxRegisterDraws = 4096;
constexpr std::size_t MaxPrograms = 131072;
constexpr auto Window = std::chrono::seconds(10);

struct ShapeDraw {
    std::uint64_t alias = 0;
    std::vector<UserWord> words;
};

struct ShapeDraws {
    std::size_t seen = 0;
    std::vector<ShapeDraw> draws;
};

struct SetDraws {
    std::size_t seen = 0;
    std::vector<std::vector<UserWord>> draws;
};

struct FrameKeys {
    std::unordered_set<std::uint64_t> keys;
    std::unordered_set<std::uint64_t> shapes;
    std::unordered_set<std::uint64_t> shaderSets;
    std::unordered_map<std::uint64_t, ShapeDraws> byShape;
    std::unordered_map<std::uint64_t, SetDraws> byShaderSet;
    std::size_t wordedDraws = 0;
    std::size_t registerDraws = 0;

    void Clear() {
        keys.clear();
        shapes.clear();
        shaderSets.clear();
        byShape.clear();
        byShaderSet.clear();
        wordedDraws = 0;
        registerDraws = 0;
    }
};

struct State {
    std::array<std::atomic<std::uint64_t>, CountTotal> counts{};
    std::array<std::atomic<std::uint64_t>, UncacheableReasons> uncacheable{};
    std::array<std::atomic<std::uint64_t>, NotRecordableReasons> notRecordable{};
    std::array<std::atomic<std::uint64_t>, UncacheableReasons> uncacheableNanoseconds{};
    std::atomic<std::uint64_t> missBuilds{0};
    std::atomic<std::uint64_t> missNanoseconds{0};
    std::atomic<std::uint64_t> hitLookups{0};
    std::atomic<std::uint64_t> hitNanoseconds{0};
    std::atomic<std::uint64_t> relocationsLearned{0};
    std::mutex mutex;
    FrameKeys current;
    FrameKeys previous;
    FrameSummary pending;
    std::unordered_map<std::uint32_t, std::uint64_t> slots;
    std::unordered_map<std::uint32_t, std::uint64_t> registerSlots;
    std::unordered_map<const void*, std::uint64_t> programs;
    std::map<std::pair<std::uint64_t, std::uint64_t>, ShaderSetCount> windowSets;
    std::uint64_t windowUncacheable = 0;
    std::uint64_t windowSerial = 0;
    std::uint64_t windowFirstLine = 1;
    std::uint64_t serial = 0;
    std::uint64_t emptyFlips = 0;
    std::chrono::steady_clock::time_point lastLine = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lastWindow = std::chrono::steady_clock::now();
    std::deque<FrameSummary> recent;
    std::deque<WindowSummary> windows;
};

State& state() {
    static auto* census = new State();
    return *census;
}

std::size_t at(Count count) {
    return static_cast<std::size_t>(count);
}

bool mapped(std::uint64_t address) {
    return address != 0 && GuestAllocations::GuestAllocationsLookup_nid_postfix(static_cast<std::uintptr_t>(address), false) != nullptr;
}

bool addressLike(std::span<const UserWord> words, std::size_t index) {
    const auto& word = words[index];
    if (mapped(word.value & ~std::uint32_t{3})) return true;
    const auto pair = [](std::uint32_t low, std::uint32_t high) { return static_cast<std::uint64_t>(low) | static_cast<std::uint64_t>(high & 0xffffu) << 32u; };
    if (index + 1 < words.size() && words[index + 1].reg == word.reg + 1 && mapped(pair(word.value, words[index + 1].value))) return true;
    return index > 0 && words[index - 1].reg + 1 == word.reg && mapped(pair(words[index - 1].value, word.value));
}

void compareWords(State& census, std::span<const UserWord> now, std::span<const UserWord> before) {
    auto& frame = census.pending;
    std::size_t i = 0, j = 0;
    while (i < now.size() || j < before.size()) {
        if (j == before.size() || (i < now.size() && now[i].reg < before[j].reg)) {
            ++census.slots[now[i].reg];
            ++frame.differingWords;
            if (addressLike(now, i)) ++frame.addressLikeWords;
            ++i;
        } else if (i == now.size() || before[j].reg < now[i].reg) {
            ++census.slots[before[j].reg];
            ++frame.differingWords;
            ++j;
        } else {
            if (now[i].value != before[j].value) {
                ++census.slots[now[i].reg];
                ++frame.differingWords;
                if (addressLike(now, i)) ++frame.addressLikeWords;
            }
            ++i;
            ++j;
        }
    }
}

void compareRegisters(State& census, std::span<const UserWord> now, std::span<const UserWord> before) {
    auto& frame = census.pending;
    std::size_t i = 0, j = 0;
    while (i < now.size() || j < before.size()) {
        if (j == before.size() || (i < now.size() && now[i].reg < before[j].reg)) {
            ++census.registerSlots[now[i].reg];
            ++frame.differingRegisters;
            ++i;
        } else if (i == now.size() || before[j].reg < now[i].reg) {
            ++census.registerSlots[before[j].reg];
            ++frame.differingRegisters;
            ++j;
        } else {
            if (now[i].value != before[j].value) {
                ++census.registerSlots[now[i].reg];
                ++frame.differingRegisters;
            }
            ++i;
            ++j;
        }
    }
}

constexpr std::array<const char*, UncacheableReasons> UncacheableNames{"not recordable", "no variant id", "bda pagetable", "fault buffer", "switch"};
constexpr std::array<const char*, NotRecordableReasons> NotRecordableNames{"sync draws", "no recorder", "dump target", "resident targets off", "no detiler", "resident linear off", "linear target not imported"};
constexpr std::array<const char*, 3> BankNames{"ctx", "sh", "uc"};

template <typename TItem>
void keepTop(std::vector<TItem>& items, auto&& before) {
    const auto top = std::min(items.size(), TopSlots);
    std::partial_sort(items.begin(), items.begin() + static_cast<std::ptrdiff_t>(top), items.end(), before);
    items.resize(top);
}

void append(std::string& line, const char* format, ...) {
    char buffer[1024];
    va_list arguments;
    va_start(arguments, format);
    const auto written = std::vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (written > 0) line.append(buffer, std::min<std::size_t>(static_cast<std::size_t>(written), sizeof(buffer) - 1));
}

unsigned long long value(const FrameSummary& frame, Count count) {
    return static_cast<unsigned long long>(frame.counts[at(count)]);
}

std::string describe(const FrameSummary& frame, std::uint64_t emptyFlips, std::size_t cacheSize, std::size_t cacheCapacity, std::uint64_t learned) {
    std::string line;
    const auto v = [&](Count count) { return value(frame, count); };
    append(line, "[draw-key] %s %llu", frame.flipped ? "frame" : "10 s window", static_cast<unsigned long long>(frame.serial));
    if (emptyFlips != 0) append(line, " (after %llu flips without draws)", static_cast<unsigned long long>(emptyFlips));
    append(line, ": draws %llu (cpu-indirect %llu inline, %llu queued; gpu-indirect %llu; empty indirect skipped %llu), %llu at the draw-cache lookup (%llu keys cached)", v(Count::Draws), v(Count::CpuIndirectInline), v(Count::CpuIndirectQueued), v(Count::GpuIndirect), v(Count::EmptyIndirectSkipped), v(Count::Lookups), v(Count::KeyCached));
    const auto admitted = v(Count::AdmittedCached) + v(Count::AdmittedSeen) + v(Count::AdmittedShape) + v(Count::AdmittedRelocation) + v(Count::AdmittedIdentity);
    const auto refused = v(Count::RefusedFirstSighting) + v(Count::RefusedShapeHeld) + v(Count::RefusedNoShape);
    append(line, "; admission: %llu admitted (%llu cached, %llu seen, %llu shape seen, %llu relocation trial, %llu identity; %llu by verify), %llu refused (%llu first sighting, %llu shape held, %llu no shape key), not tried: %llu hits, %llu cpu-indirect, %llu entries off; %llu shape entries replaced", admitted, v(Count::AdmittedCached), v(Count::AdmittedSeen), v(Count::AdmittedShape), v(Count::AdmittedRelocation), v(Count::AdmittedIdentity), v(Count::AdmittedVerify), refused, v(Count::RefusedFirstSighting), v(Count::RefusedShapeHeld), v(Count::RefusedNoShape), v(Count::NotTriedHit), v(Count::NotTriedCpuIndirect), v(Count::NotTriedEntriesOff), v(Count::ShapeReplaced));
    append(line, "; hits: %llu draw cache, %llu data, %llu/%llu recipes found/recorded, %llu/%llu relocations applied/productive (%llu shader sets learned)", v(Count::DrawCacheHits), v(Count::DataHits), v(Count::RecipesFound), v(Count::RecipesRecorded), v(Count::RelocationsApplied), v(Count::RelocationsProductive), static_cast<unsigned long long>(learned));
    append(line, "; resource cache: %llu hits, %llu misses (%llu invalidated), %llu uncacheable, %zu entries of %zu", v(Count::ResourceHits), v(Count::ResourceMisses), v(Count::ResourceInvalidated), v(Count::ResourceUncacheable), cacheSize, cacheCapacity);
    append(line, "; repeat against the previous %s, of %llu keyed draws: %llu full key, %llu shape key, %llu shader set", frame.flipped ? "frame" : "window", static_cast<unsigned long long>(frame.keyed), static_cast<unsigned long long>(frame.keyRepeats), static_cast<unsigned long long>(frame.shapeRepeats), static_cast<unsigned long long>(frame.shaderSetRepeats));
    append(line, "; %llu shape-only repeats: %llu differing words (%llu address-like), slots", static_cast<unsigned long long>(frame.shapeOnlyRepeats), static_cast<unsigned long long>(frame.differingWords), static_cast<unsigned long long>(frame.addressLikeWords));
    if (frame.slots.empty()) line += " none";
    for (const auto& [reg, count] : frame.slots) append(line, " 0x%03x:%llu", reg, static_cast<unsigned long long>(count));
    append(line, "; content behind the user words on %llu shape-repeat misses: %llu same, %llu changed, %llu not hashed", static_cast<unsigned long long>(frame.contentSame + frame.contentChanged + frame.contentUnhashed), static_cast<unsigned long long>(frame.contentSame), static_cast<unsigned long long>(frame.contentChanged), static_cast<unsigned long long>(frame.contentUnhashed));
    append(line, "; uncacheable by reason: %llu %s (", static_cast<unsigned long long>(frame.uncacheable[0]), UncacheableNames[0]);
    for (std::size_t why = 0; why < NotRecordableReasons; ++why) append(line, "%s%llu %s", why == 0 ? "" : ", ", static_cast<unsigned long long>(frame.notRecordable[why]), NotRecordableNames[why]);
    line += ")";
    for (std::size_t reason = 1; reason < UncacheableReasons; ++reason) append(line, ", %llu %s", static_cast<unsigned long long>(frame.uncacheable[reason]), UncacheableNames[reason]);
    line += "; build us:";
    for (std::size_t reason = 0; reason < UncacheableReasons; ++reason) append(line, "%s %s %.1f", reason == 0 ? "" : ",", UncacheableNames[reason], static_cast<double>(frame.uncacheableNanoseconds[reason]) / 1000.0);
    append(line, "; cacheable misses %.1f us in %llu builds; hits %.1f us in %llu lookups (lookup and Revalidate)", static_cast<double>(frame.missNanoseconds) / 1000.0, static_cast<unsigned long long>(frame.missBuilds), static_cast<double>(frame.hitNanoseconds) / 1000.0, static_cast<unsigned long long>(frame.hitLookups));
    append(line, "; %llu shader-set-only repeats (%llu compared): %llu differing registers, registers", static_cast<unsigned long long>(frame.shaderSetOnlyRepeats), static_cast<unsigned long long>(frame.comparedRegisterDraws), static_cast<unsigned long long>(frame.differingRegisters));
    if (frame.registerSlots.empty()) line += " none";
    for (const auto& [reg, count] : frame.registerSlots) append(line, " %s 0x%03x:%llu", BankNames[std::min<std::size_t>(reg >> 16u, BankNames.size() - 1)], reg & 0xffffu, static_cast<unsigned long long>(count));
    return line;
}

std::string address(std::uint64_t value) {
    if (value == 0) return "?";
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(value));
    return buffer;
}

std::string describeWindow(const WindowSummary& window) {
    std::string line;
    append(line, "uncacheable shader sets over lines %llu-%llu: top %zu of %zu sets, %llu lookups:", static_cast<unsigned long long>(window.firstLine), static_cast<unsigned long long>(window.lastLine), window.top.size(), window.shaderSets, static_cast<unsigned long long>(window.uncacheable));
    if (window.top.empty()) line += " none";
    for (const auto& set : window.top) {
        append(line, " [vs %s ps %s: %llu (", address(set.vertex).c_str(), address(set.pixel).c_str(), static_cast<unsigned long long>(set.lookups));
        bool first = true;
        for (std::size_t reason = 0; reason < UncacheableReasons; ++reason) {
            if (set.reasons[reason] == 0) continue;
            append(line, "%s%llu %s", first ? "" : ", ", static_cast<unsigned long long>(set.reasons[reason]), UncacheableNames[reason]);
            first = false;
        }
        line += ")]";
    }
    return line;
}

WindowSummary closeWindow(State& census, std::chrono::steady_clock::time_point now) {
    WindowSummary window;
    window.serial = ++census.windowSerial;
    window.firstLine = census.windowFirstLine;
    window.lastLine = census.serial;
    window.uncacheable = census.windowUncacheable;
    window.shaderSets = census.windowSets.size();
    window.top.reserve(census.windowSets.size());
    for (const auto& [key, set] : census.windowSets) window.top.push_back(set);
    keepTop(window.top, [](const ShaderSetCount& a, const ShaderSetCount& b) { return a.lookups != b.lookups ? a.lookups > b.lookups : a.vertex != b.vertex ? a.vertex < b.vertex : a.pixel < b.pixel; });
    census.windowSets.clear();
    census.windowUncacheable = 0;
    census.windowFirstLine = census.serial + 1;
    census.lastWindow = now;
    return window;
}

void emit(State& census, bool flipped) {
    auto& frame = census.pending;
    for (std::size_t i = 0; i < CountTotal; ++i) frame.counts[i] = census.counts[i].exchange(0, std::memory_order_relaxed);
    frame.serial = ++census.serial;
    frame.flipped = flipped;
    frame.slots.assign(census.slots.begin(), census.slots.end());
    const auto top = std::min(frame.slots.size(), TopSlots);
    std::partial_sort(frame.slots.begin(), frame.slots.begin() + static_cast<std::ptrdiff_t>(top), frame.slots.end(), [](const auto& a, const auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
    frame.slots.resize(top);
    census.slots.clear();
    for (std::size_t reason = 0; reason < UncacheableReasons; ++reason) {
        frame.uncacheable[reason] = census.uncacheable[reason].exchange(0, std::memory_order_relaxed);
        frame.uncacheableNanoseconds[reason] = census.uncacheableNanoseconds[reason].exchange(0, std::memory_order_relaxed);
    }
    for (std::size_t why = 0; why < NotRecordableReasons; ++why) frame.notRecordable[why] = census.notRecordable[why].exchange(0, std::memory_order_relaxed);
    frame.missBuilds = census.missBuilds.exchange(0, std::memory_order_relaxed);
    frame.missNanoseconds = census.missNanoseconds.exchange(0, std::memory_order_relaxed);
    frame.hitLookups = census.hitLookups.exchange(0, std::memory_order_relaxed);
    frame.hitNanoseconds = census.hitNanoseconds.exchange(0, std::memory_order_relaxed);
    frame.registerSlots.assign(census.registerSlots.begin(), census.registerSlots.end());
    keepTop(frame.registerSlots, [](const auto& a, const auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
    census.registerSlots.clear();
    auto& cache = SharedResourceCache();
    auto line = describe(frame, census.emptyFlips, cache.Size(), cache.Capacity(), census.relocationsLearned.load(std::memory_order_relaxed));
    const auto now = std::chrono::steady_clock::now();
    const bool windowDue = !flipped || now - census.lastWindow >= Window;
    WindowSummary window;
    if (windowDue) window = closeWindow(census, now);
    if (windowDue && !flipped) line += "; " + describeWindow(window);
    AgcDriver::ProfilePrint_nid_no_patch("%s\n", line.c_str());
    if (windowDue && flipped) AgcDriver::ProfilePrint_nid_no_patch("[draw-key] 10 s window %llu: %s\n", static_cast<unsigned long long>(window.serial), describeWindow(window).c_str());
    if (windowDue) {
        census.windows.push_back(std::move(window));
        if (census.windows.size() > KeptWindows) census.windows.pop_front();
    }
    census.recent.push_back(std::move(frame));
    if (census.recent.size() > KeptFrames) census.recent.pop_front();
    std::swap(census.previous, census.current);
    census.current.Clear();
    census.pending = {};
    census.emptyFlips = 0;
    census.lastLine = std::chrono::steady_clock::now();
}

}

bool Enabled() {
    static const bool enabled = std::getenv("APS5_DRAW_KEY_CENSUS") != nullptr;
    return enabled;
}

void Add(Count count) {
    if (!Enabled()) return;
    state().counts[at(count)].fetch_add(1, std::memory_order_relaxed);
}

void Admitted(Admit admit) {
    switch (admit) {
        case Admit::Cached: Add(Count::AdmittedCached); return;
        case Admit::Seen: Add(Count::AdmittedSeen); return;
        case Admit::Relocation: Add(Count::AdmittedRelocation); return;
        case Admit::Identity: Add(Count::AdmittedIdentity); return;
        case Admit::Shape: Add(Count::AdmittedShape); return;
        case Admit::NoShape: Add(Count::RefusedNoShape); return;
        case Admit::ShapeHeld: Add(Count::RefusedShapeHeld); return;
        case Admit::FirstSighting: Add(Count::RefusedFirstSighting); return;
    }
}

void NoteRelocationsLearned(std::size_t shaderSets) {
    if (!Enabled()) return;
    state().relocationsLearned.store(shaderSets, std::memory_order_relaxed);
}

void NoteLookup(const Lookup& lookup) {
    if (!Enabled()) return;
    auto& census = state();
    std::lock_guard lock(census.mutex);
    auto& frame = census.pending;
    ++frame.keyed;
    const bool keyRepeat = census.previous.keys.contains(lookup.key);
    const bool shapeRepeat = census.previous.shapes.contains(lookup.shape);
    frame.keyRepeats += keyRepeat ? 1u : 0u;
    frame.shapeRepeats += shapeRepeat ? 1u : 0u;
    const bool shaderSetRepeat = census.previous.shaderSets.contains(lookup.shaderSet);
    frame.shaderSetRepeats += shaderSetRepeat ? 1u : 0u;
    census.current.keys.insert(lookup.key);
    census.current.shapes.insert(lookup.shape);
    census.current.shaderSets.insert(lookup.shaderSet);
    auto& shape = census.current.byShape[lookup.shape];
    const auto ordinal = shape.seen++;
    const ShapeDraw* before = nullptr;
    if (shapeRepeat) {
        const auto found = census.previous.byShape.find(lookup.shape);
        if (found != census.previous.byShape.end() && ordinal < found->second.draws.size()) before = &found->second.draws[ordinal];
    }
    if (shapeRepeat && !keyRepeat) {
        ++frame.shapeOnlyRepeats;
        if (before != nullptr) compareWords(census, lookup.words, before->words);
    }
    if (shapeRepeat && !lookup.cached) {
        if (before != nullptr && before->alias != 0 && lookup.alias != 0) ++(before->alias == lookup.alias ? frame.contentSame : frame.contentChanged);
        else ++frame.contentUnhashed;
    }
    if (ordinal == shape.draws.size() && ordinal < MaxDrawsPerShape && census.current.wordedDraws < MaxWordedDraws) {
        shape.draws.push_back({lookup.alias, std::vector<UserWord>(lookup.words.begin(), lookup.words.end())});
        ++census.current.wordedDraws;
    }
    auto& set = census.current.byShaderSet[lookup.shaderSet];
    const auto setOrdinal = set.seen++;
    if (shaderSetRepeat && !shapeRepeat) {
        ++frame.shaderSetOnlyRepeats;
        const auto found = census.previous.byShaderSet.find(lookup.shaderSet);
        if (found != census.previous.byShaderSet.end() && setOrdinal < found->second.draws.size()) {
            ++frame.comparedRegisterDraws;
            compareRegisters(census, lookup.registers, found->second.draws[setOrdinal]);
        }
    }
    if (setOrdinal == set.draws.size() && setOrdinal < MaxDrawsPerShape && census.current.registerDraws < MaxRegisterDraws) {
        set.draws.emplace_back(lookup.registers.begin(), lookup.registers.end());
        ++census.current.registerDraws;
    }
    if (std::chrono::steady_clock::now() - census.lastLine >= Window) emit(census, false);
}

void NoteProgram(const void* program, std::uint64_t codeAddress) {
    if (!Enabled() || program == nullptr) return;
    auto& census = state();
    std::lock_guard lock(census.mutex);
    if (census.programs.size() >= MaxPrograms) census.programs.clear();
    census.programs[program] = codeAddress;
}

void NoteUncacheable(Uncacheable reason, NotRecordable why, const void* vertex, const void* pixel) {
    if (!Enabled()) return;
    auto& census = state();
    const auto index = static_cast<std::size_t>(reason);
    std::lock_guard lock(census.mutex);
    census.counts[at(Count::ResourceUncacheable)].fetch_add(1, std::memory_order_relaxed);
    census.uncacheable[index].fetch_add(1, std::memory_order_relaxed);
    if (reason == Uncacheable::NotRecordable && why != NotRecordable::Total) census.notRecordable[static_cast<std::size_t>(why)].fetch_add(1, std::memory_order_relaxed);
    const auto codeAddress = [&](const void* program) -> std::uint64_t {
        if (program == nullptr) return 0;
        const auto found = census.programs.find(program);
        return found == census.programs.end() ? 0 : found->second;
    };
    const std::pair key{codeAddress(vertex), codeAddress(pixel)};
    auto& set = census.windowSets[key];
    set.vertex = key.first;
    set.pixel = key.second;
    ++set.lookups;
    ++set.reasons[index];
    ++census.windowUncacheable;
}

void NoteUncacheableBuild(Uncacheable reason, std::uint64_t nanoseconds) {
    if (!Enabled()) return;
    state().uncacheableNanoseconds[static_cast<std::size_t>(reason)].fetch_add(nanoseconds, std::memory_order_relaxed);
}

void NoteMissBuild(std::uint64_t nanoseconds) {
    if (!Enabled()) return;
    auto& census = state();
    std::lock_guard lock(census.mutex);
    census.counts[at(Count::ResourceMisses)].fetch_add(1, std::memory_order_relaxed);
    census.missBuilds.fetch_add(1, std::memory_order_relaxed);
    census.missNanoseconds.fetch_add(nanoseconds, std::memory_order_relaxed);
}

void NoteHit(std::uint64_t nanoseconds) {
    if (!Enabled()) return;
    auto& census = state();
    std::lock_guard lock(census.mutex);
    census.counts[at(Count::ResourceHits)].fetch_add(1, std::memory_order_relaxed);
    census.hitLookups.fetch_add(1, std::memory_order_relaxed);
    census.hitNanoseconds.fetch_add(nanoseconds, std::memory_order_relaxed);
}

void Frame() {
    if (!Enabled()) return;
    auto& census = state();
    std::lock_guard lock(census.mutex);
    if (census.counts[at(Count::Draws)].load(std::memory_order_relaxed) == 0 && census.pending.keyed == 0) {
        ++census.emptyFlips;
        return;
    }
    emit(census, true);
}

std::vector<FrameSummary> RecentFrames() {
    auto& census = state();
    std::lock_guard lock(census.mutex);
    return {census.recent.begin(), census.recent.end()};
}

std::vector<WindowSummary> RecentWindows() {
    auto& census = state();
    std::lock_guard lock(census.mutex);
    return {census.windows.begin(), census.windows.end()};
}

}
