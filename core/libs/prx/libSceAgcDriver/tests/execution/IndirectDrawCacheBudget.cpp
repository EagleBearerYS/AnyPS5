#include "IndirectDrawCacheFixture.hpp"
#include <cstdlib>
#include <exception>
#include <iostream>

namespace {

using namespace IndirectDrawCacheFixture;

constexpr std::uint32_t Draws = 48;
constexpr std::uint64_t HitMargin = 2;

IndirectPacket Packet(std::uint32_t scene, std::uint32_t draw) {
    return {PushedVertexCode.data(), {Clipped, Clipped, scene * 0x1000u + draw + 1u}, StartInstanceLocation, 1, 3};
}

struct Cycle {
    std::uint64_t hits = 0;
    AgcDriver::DriverDetail::Driver::CpuIndirectFootprint footprint;
};

Cycle RunCycle(std::uint32_t scene, const Drawn& reference) {
    Cycle cycle;
    for (std::uint32_t draw = 0; draw < Draws; ++draw) {
        const auto drawn = Submit(Packet(scene, draw), {{3, 1, 0, 0}});
        RequireSamePixels(drawn, reference, "draw " + std::to_string(draw) + " of scene " + std::to_string(scene));
        cycle.hits += drawn.Hits();
    }
    cycle.footprint = AgcDriver::DriverDetail::Driver::Get().CpuIndirectCacheSize();
    Require(cycle.footprint.bytes <= cycle.footprint.budget, "the CPU-path indirect draw-cache entries of scene " + std::to_string(scene) + " hold " + std::to_string(cycle.footprint.bytes) + " bytes in " + std::to_string(cycle.footprint.entries) + " entries, over the budget of " + std::to_string(cycle.footprint.budget) + " bytes");
    return cycle;
}

std::uint64_t ExpectedHits(const Cycle& cycle) {
    Require(cycle.footprint.entries != 0, "no CPU-path indirect draw was kept in the draw cache");
    const auto perEntry = cycle.footprint.bytes / cycle.footprint.entries;
    return std::min<std::uint64_t>(Draws, cycle.footprint.budget / perEntry);
}

void CycleOverTheBudgetKeepsItsHits() {
    const auto reference = Reference();
    for (std::uint32_t warm = 0; warm < 2; ++warm) static_cast<void>(RunCycle(1, reference));
    std::uint64_t expected = 0;
    for (std::uint32_t repeat = 0; repeat < 3; ++repeat) {
        const auto cycle = RunCycle(1, reference);
        expected = ExpectedHits(cycle);
        Require(cycle.footprint.entries < Draws, "the budget of " + std::to_string(cycle.footprint.budget) + " bytes kept all " + std::to_string(Draws) + " draws of the cycle, so it did not bind");
        Require(cycle.hits + HitMargin >= expected, "a cycle of " + std::to_string(Draws) + " CPU-path indirect draws over the budget made " + std::to_string(cycle.hits) + " draw-cache hits, expected at least " + std::to_string(expected) + " (the budget over the bytes per entry) minus " + std::to_string(HitMargin));
    }
    std::printf("a cycle of %u CPU-path indirect draws over a budget that holds %llu of them kept its hits (%llu of %u per cycle) within the budget\n", Draws, static_cast<unsigned long long>(expected), static_cast<unsigned long long>(expected), Draws);
    Cycle last;
    for (std::uint32_t repeat = 0; repeat < 12; ++repeat) last = RunCycle(2, reference);
    Require(last.hits + HitMargin >= ExpectedHits(last), "after the draws changed, the idle entries of the old cycle were not swept: the new cycle made " + std::to_string(last.hits) + " draw-cache hits in its twelfth repeat, expected at least " + std::to_string(ExpectedHits(last)) + " minus " + std::to_string(HitMargin));
    std::printf("a new cycle replaced the idle entries of the old one and made %llu hits per cycle\n", static_cast<unsigned long long>(last.hits));
}

void Set(const char* name, const char* value) {
    if (std::getenv(name) != nullptr) return;
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

}

int main() {
    try {
        Set("APS5_CACHE_CPU_INDIRECT", "1");
        Set("APS5_CPU_INDIRECT_CACHE_KIB", "64");
        Set("APS5_DRAW_CACHE_ENTRIES", "64");
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
        }
        RegisterShaders();
        CycleOverTheBudgetKeepsItsHits();
        AgcDriverShutdown_nid_postfix();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        AgcDriverShutdown_nid_postfix();
        return 1;
    }
}
