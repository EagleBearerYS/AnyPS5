#include "IndirectDrawCacheFixture.hpp"
#include <cstdlib>
#include <exception>
#include <iostream>

namespace {

using namespace IndirectDrawCacheFixture;

alignas(256) constexpr std::array<std::uint32_t, 26> WalkedAddressVertexCode{
    0x7e000d05u, 0x080200f4u, 0x10020300u, 0x100202f4u, 0x060202f3u, 0x060400f3u, 0x10040500u, 0x060404f3u,
    0xf4000304u, 0xfa000000u, 0x7e0c0280u, 0xdc308000u, 0x03080006u, 0xbf8c0070u, 0x0606060cu,
    0x7e0802f2u, 0x7e0a02ffu, 0x44800000u, 0xf80000d1u, 0x00000005u, 0xf80008cfu, 0x04030201u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u,
};
alignas(8) Header WalkedAddressVertexHeader{};
alignas(4096) std::array<float, 1024> VisibleDepth{};
alignas(4096) std::array<float, 1024> ClippedDepth{};

std::uint64_t RecapturedStageKeepsItsMemory() {
    Registered(WalkedAddressVertexHeader, WalkedAddressVertexCode.data(), sizeof(WalkedAddressVertexCode), 2);
    VisibleDepth.fill(0.0f);
    ClippedDepth.fill(1.0f);
    const auto visible = reinterpret_cast<std::uintptr_t>(VisibleDepth.data());
    const auto clipped = reinterpret_cast<std::uintptr_t>(ClippedDepth.data());
    Require(visible >> 32u == clipped >> 32u, "the two depth pages do not share their high address word");
    const auto low = [](std::uintptr_t address) { return static_cast<std::uint32_t>(address); };
    const IndirectPacket draw{WalkedAddressVertexCode.data(), {low(visible), static_cast<std::uint32_t>(visible >> 32u)}, 0, 1};
    std::uint64_t hits = 0;
    for (int repeat = 0; repeat < 3; ++repeat) {
        const auto drawn = Submit(draw, {{3, 1, low(visible), 0}});
        hits += drawn.Hits();
        Require(drawn.covered != 0, "the indirect draw whose vertex shader loads and global-loads its depth through the record's address " + Hex(low(visible)) + " (0.0) covered nothing, repeat " + std::to_string(repeat));
    }
    auto two = draw;
    two.records = 2;
    const auto both = Submit(two, {{3, 1, low(clipped), 0}, {3, 1, low(visible), 0}});
    hits += both.Hits();
    Require(both.covered != 0, "the indirect draw of a record loading depth 1.0 through its address, then one loading 0.0 at the address the stage was compiled for, covered nothing (" + std::to_string(both.Hits()) + " draw-cache hits)");
    return hits;
}

std::uint64_t RecordsDrawPerRecord(const Drawn& reference) {
    struct Case {
        const IndirectPacket* draw;
        std::initializer_list<std::array<std::uint32_t, 4>> records;
        bool visible;
        const char* what;
    };
    const std::initializer_list<Case> cases{
        {&PushedOne, {{3, 1, 0, 0}}, true, "one record with depth 0"},
        {&PushedOne, {{3, 1, 0, Clipped}}, false, "one record with a clipped start instance"},
        {&PushedOne, {{3, 1, Clipped, 0}}, false, "one record with a clipped base vertex"},
        {&PushedOne, {{3, 1, 0, 0}}, true, "one record with depth 0 again"},
        {&PushedTwo, {{3, 1, Clipped, 0}, {3, 1, 0, 0}}, true, "a record with a clipped base vertex, then one with depth 0"},
        {&PushedTwo, {{3, 1, 0, 0}, {3, 1, 0, Clipped}}, true, "a record with depth 0, then one with a clipped start instance"},
        {&PushedTwo, {{3, 1, Clipped, 0}, {3, 1, 0, Clipped}}, false, "two records clipped by base vertex and by start instance"},
    };
    std::uint64_t hits = 0;
    for (const auto& item : cases) {
        const auto drawn = Submit(*item.draw, item.records);
        hits += drawn.Hits();
        if (item.visible) RequireSamePixels(drawn, reference, std::string("the indirect draw of ") + item.what);
        else Require(drawn.covered == 0, std::string("the indirect draw of ") + item.what + " covered " + std::to_string(drawn.covered) + " bytes, expected none");
    }
    return hits;
}

std::uint64_t WalkedWordsAreNotReusedAcrossValues() {
    Depths.fill(0.0f);
    Depths[32] = 2.0f;
    const auto visible = reinterpret_cast<std::uintptr_t>(&Depths[0]);
    const auto clipped = reinterpret_cast<std::uintptr_t>(&Depths[32]);
    Require(visible >> 32u == clipped >> 32u, "the two depth words do not share their high address word");
    const auto low = [](std::uintptr_t address) { return static_cast<std::uint32_t>(address); };
    const IndirectPacket draw{WalkedVertexCode.data(), {low(visible), static_cast<std::uint32_t>(visible >> 32u)}, 0, 1};
    std::uint64_t hits = 0;
    for (int repeat = 0; repeat < 3; ++repeat) {
        const auto drawn = Submit(draw, {{3, 1, low(visible), 0}});
        hits += drawn.Hits();
        Require(drawn.covered != 0, "the indirect draw whose record points the vertex shader's depth load at 0.0 (" + Hex(low(visible)) + ") covered nothing, repeat " + std::to_string(repeat));
    }
    const auto moved = Submit(draw, {{3, 1, low(clipped), 0}});
    hits += moved.Hits();
    Require(moved.covered == 0, "the indirect draw whose record points the vertex shader's depth load at 2.0 (" + Hex(low(clipped)) + ") covered " + std::to_string(moved.covered) + " bytes, expected none: the stage compiled for " + Hex(low(visible)) + " was reused (" + std::to_string(moved.Hits()) + " draw-cache hits)");
    auto two = draw;
    two.records = 2;
    const auto both = Submit(two, {{3, 1, low(clipped), 0}, {3, 1, low(visible), 0}});
    hits += both.Hits();
    Require(both.covered != 0, "the indirect draw of a record loading depth 2.0, then one loading 0.0, covered nothing");
    return hits;
}

}

int main() {
    try {
        setenv("APS5_CACHE_CPU_INDIRECT", "1", 0);
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
        }
        RegisterShaders();
        const auto reference = Reference();
        const auto pushed = RecordsDrawPerRecord(reference);
        const auto walked = WalkedWordsAreNotReusedAcrossValues();
        const auto addressed = RecapturedStageKeepsItsMemory();
        AgcDriverShutdown_nid_postfix();
        std::printf("a stage recaptured for one record and drawn at its compiled address for the next read that address (%llu of 4 hits)\n", static_cast<unsigned long long>(addressed));
        std::printf("CPU-path indirect draws drew each record's base vertex and start instance (%llu of 7 draws were draw-cache hits) and a stage whose patched word is a load address was not reused across values (%llu of 5 hits)\n", static_cast<unsigned long long>(pushed), static_cast<unsigned long long>(walked));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        AgcDriverShutdown_nid_postfix();
        return 1;
    }
}
