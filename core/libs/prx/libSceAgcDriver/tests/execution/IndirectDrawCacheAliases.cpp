#include "IndirectDrawCacheFixture.hpp"
#include <cstdlib>
#include <exception>
#include <iostream>

namespace {

using namespace IndirectDrawCacheFixture;

constexpr std::uint32_t Draws = 32;
constexpr std::uint32_t UnusedUserWordLocation = BaseVertexLocation + 2;
alignas(4096) std::array<std::array<float, 1024>, Draws> Depths{};

IndirectPacket Packet(std::uint32_t draw) {
    const auto address = reinterpret_cast<std::uintptr_t>(Depths[draw].data());
    IndirectPacket packet{WalkedVertexCode.data(), {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>(address >> 32u), 0}, 0, 1, 3};
    packet.baseVertexLocation = UnusedUserWordLocation;
    packet.viewportScale = 32.0f - static_cast<float>(draw) / 8.0f;
    return packet;
}

void EvictedEntriesLeaveTheirRelocationAliases() {
    for (std::uint32_t draw = 0; draw < Draws; ++draw) Depths[draw].fill(static_cast<float>(draw + 1) / 64.0f);
    for (std::uint32_t draw = 0; draw < Draws; ++draw) {
        for (std::uint32_t sighting = 0; sighting < 2; ++sighting) static_cast<void>(Submit(Packet(draw), {{3, 1, 0, 0}}));
    }
    auto& driver = AgcDriver::DriverDetail::Driver::Get();
    const auto footprint = driver.CpuIndirectCacheSize();
    const auto cache = driver.DrawCacheSize();
    Require(footprint.dropped != 0, "no CPU-path indirect draw-cache entry was dropped, so the test exercises nothing (" + std::to_string(footprint.entries) + " in the cache)");
    Require(footprint.entries == cache.entries, std::to_string(cache.entries - footprint.entries) + " draw-cache entries are not CPU-path indirect ones, so the alias counts are not theirs alone");
    Require(cache.aliased != 0, "no relocation alias was learned for a CPU-path indirect draw-cache entry, so the test exercises nothing");
    Require(cache.aliasedOutside == 0, std::to_string(cache.aliasedOutside) + " CPU-path indirect draw-cache entries that left the cache are still held by relocation aliases (" + std::to_string(cache.aliased) + " aliased entries, " + std::to_string(footprint.entries) + " in the cache, " + std::to_string(footprint.dropped) + " dropped)");
    const auto again = Submit(Packet(Draws - 1), {{3, 1, 0, 0}});
    Require(again.Hits() == 1, "the most recent CPU-path indirect draw made " + std::to_string(again.Hits()) + " draw-cache hits after the evictions, expected 1");
    std::printf("%llu CPU-path indirect draw-cache entries were dropped and none stayed held by its relocation aliases (%zu aliased entries, all in the cache); the newest draw still hit\n", static_cast<unsigned long long>(footprint.dropped), cache.aliased);
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
        Set("APS5_DRAW_CACHE_ENTRIES", "16");
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
        }
        RegisterShaders();
        EvictedEntriesLeaveTheirRelocationAliases();
        AgcDriverShutdown_nid_postfix();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        AgcDriverShutdown_nid_postfix();
        return 1;
    }
}
