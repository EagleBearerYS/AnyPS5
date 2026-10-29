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
};

bool Enabled();
void Add(Count count);
void Admitted(Admit admit);
void NoteRelocationsLearned(std::size_t shaderSets);
void NoteLookup(const Lookup& lookup);
void Frame();
std::vector<FrameSummary> RecentFrames();

}

#endif
