#include "prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawCacheBypass.hpp"
#include <algorithm>

namespace AgcDriver::DriverDetail {

namespace {

constexpr std::uint64_t FieldMask = 0xffffu;
constexpr unsigned LookupsShift = 0, HitsShift = 16, StageHitsShift = 32, IdleShift = 48;

static_assert(DrawCacheBypassWindow::Window <= FieldMask && DrawCacheBypassWindow::IdleDraws <= FieldMask);

std::uint64_t field(std::uint64_t state, unsigned shift) {
    return (state >> shift) & FieldMask;
}

std::uint64_t pack(std::uint64_t lookups, std::uint64_t hits, std::uint64_t stageHits, std::uint64_t idle) {
    return lookups << LookupsShift | hits << HitsShift | stageHits << StageHitsShift | idle << IdleShift;
}

}

bool DrawCacheBypassWindow::Bypass() {
    auto current = state.load(std::memory_order_relaxed);
    while (field(current, IdleShift) != 0) {
        if (state.compare_exchange_weak(current, current - (std::uint64_t{1} << IdleShift), std::memory_order_relaxed)) return true;
    }
    return false;
}

bool DrawCacheBypassWindow::Note(bool hit, std::uint32_t stages) {
    auto current = state.load(std::memory_order_relaxed);
    for (;;) {
        const auto lookups = field(current, LookupsShift) + 1;
        const auto hits = field(current, HitsShift) + (hit ? 1u : 0u);
        const auto stageHits = std::min(field(current, StageHitsShift) + stages, FieldMask);
        const auto idle = field(current, IdleShift);
        const bool closes = lookups >= Window;
        const bool bypass = closes && hits < MinimumHits && stageHits < MinimumHits;
        const auto next = closes ? pack(0, 0, 0, bypass ? IdleDraws : idle) : pack(lookups, hits, stageHits, idle);
        if (state.compare_exchange_weak(current, next, std::memory_order_relaxed)) return bypass;
    }
}

}
