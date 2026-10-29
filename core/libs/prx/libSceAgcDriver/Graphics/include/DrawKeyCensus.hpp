#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_DRAWKEYCENSUS_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_DRAWKEYCENSUS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace AgcDriver::Graphics::DrawKeyCensus {

enum class Count : std::size_t {
    Draws,
    EmptyIndirectSkipped,
    GpuIndirect,
    CpuIndirectInline,
    CpuIndirectQueued,
    Lookups,
    KeyCached,
    NotTriedHit,
    NotTriedCpuIndirect,
    NotTriedEntriesOff,
    AdmittedVerify,
    AdmittedCached,
    AdmittedSeen,
    AdmittedRelocation,
    AdmittedIdentity,
    AdmittedShape,
    RefusedNoShape,
    RefusedShapeHeld,
    RefusedFirstSighting,
    ShapeReplaced,
    DrawCacheHits,
    DataHits,
    RecipesFound,
    RecipesRecorded,
    RelocationsApplied,
    RelocationsProductive,
    ResourceHits,
    ResourceMisses,
    ResourceInvalidated,
    ResourceUncacheable,
    Total,
};

enum class Uncacheable : std::uint8_t {
    NotRecordable,
    NoVariantId,
    BdaPagetable,
    FaultBuffer,
    Switch,
    Total,
};

enum class NotRecordable : std::uint8_t {
    SyncDraws,
    NoRecorder,
    DumpTarget,
    ResidentTargetsOff,
    NoDetiler,
    ResidentLinearOff,
    LinearNotImported,
    Total,
};

inline constexpr std::size_t UncacheableReasons = static_cast<std::size_t>(Uncacheable::Total);
inline constexpr std::size_t NotRecordableReasons = static_cast<std::size_t>(NotRecordable::Total);

enum class Admit : std::uint8_t {
    Cached,
    Seen,
    Relocation,
    Identity,
    Shape,
    NoShape,
    ShapeHeld,
    FirstSighting,
};

struct UserWord {
    std::uint32_t reg = 0;
    std::uint32_t value = 0;
};

struct Lookup {
    std::uint64_t key = 0;
    std::uint64_t shape = 0;
    std::uint64_t shaderSet = 0;
    std::uint64_t alias = 0;
    bool cached = false;
    std::span<const UserWord> words;
    std::span<const UserWord> registers;
};

struct ShaderSetCount {
    std::uint64_t vertex = 0;
    std::uint64_t pixel = 0;
    std::uint64_t lookups = 0;
    std::array<std::uint64_t, UncacheableReasons> reasons{};
};

struct WindowSummary {
    std::uint64_t serial = 0;
    std::uint64_t firstLine = 0;
    std::uint64_t lastLine = 0;
    std::uint64_t uncacheable = 0;
    std::size_t shaderSets = 0;
    std::vector<ShaderSetCount> top;
};

struct FrameSummary {
    std::uint64_t serial = 0;
    bool flipped = false;
    std::array<std::uint64_t, static_cast<std::size_t>(Count::Total)> counts{};
    std::uint64_t keyed = 0;
    std::uint64_t keyRepeats = 0;
    std::uint64_t shapeRepeats = 0;
    std::uint64_t shaderSetRepeats = 0;
    std::uint64_t shapeOnlyRepeats = 0;
    std::uint64_t differingWords = 0;
    std::uint64_t addressLikeWords = 0;
    std::uint64_t contentSame = 0;
    std::uint64_t contentChanged = 0;
    std::uint64_t contentUnhashed = 0;
    std::vector<std::pair<std::uint32_t, std::uint64_t>> slots;
    std::array<std::uint64_t, UncacheableReasons> uncacheable{};
    std::array<std::uint64_t, NotRecordableReasons> notRecordable{};
    std::array<std::uint64_t, UncacheableReasons> uncacheableNanoseconds{};
    std::uint64_t missBuilds = 0;
    std::uint64_t missNanoseconds = 0;
    std::uint64_t hitLookups = 0;
    std::uint64_t hitNanoseconds = 0;
    std::uint64_t shaderSetOnlyRepeats = 0;
    std::uint64_t comparedRegisterDraws = 0;
    std::uint64_t differingRegisters = 0;
    std::vector<std::pair<std::uint32_t, std::uint64_t>> registerSlots;
};

bool Enabled();
void Add(Count count);
void Admitted(Admit admit);
void NoteRelocationsLearned(std::size_t shaderSets);
void NoteLookup(const Lookup& lookup);
void NoteProgram(const void* program, std::uint64_t codeAddress);
void NoteUncacheable(Uncacheable reason, NotRecordable why, const void* vertex, const void* pixel);
void NoteUncacheableBuild(Uncacheable reason, std::uint64_t nanoseconds);
void NoteMissBuild(std::uint64_t nanoseconds);
void NoteHit(std::uint64_t nanoseconds);
void Frame();
std::vector<FrameSummary> RecentFrames();
std::vector<WindowSummary> RecentWindows();

}

#endif
