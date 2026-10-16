#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Submit/include/Dcb.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using Counters = AgcDriver::DriverDetail::DrawEntryCounters;

constexpr std::byte Kept{0x40};
constexpr std::uint32_t BaseVertexLocation = 0x8c;
constexpr std::uint32_t Clipped = 0x40000000u;
constexpr std::uint32_t TriangleList = 4;
constexpr std::uint32_t RectList = 17;
constexpr std::uint32_t RecordWords = 4;
constexpr std::uint32_t BenchmarkDraws = 256;
constexpr std::size_t TiledBytes = 65536;
constexpr std::uint32_t TiledAttrib3 = 0x09000000u | (0x1au << 14u);

alignas(256) constexpr std::array<std::uint32_t, 20> VertexCode{
    0x7e000d05u, 0x080200f4u, 0x10020300u, 0x100202f4u, 0x060202f3u, 0x060400f3u, 0x10040500u, 0x060404f3u,
    0x7e060208u, 0x7e0802f2u, 0x7e0a02ffu, 0x44800000u, 0xf80000d1u, 0x00000005u, 0xf80008cfu, 0x04030201u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u,
};
alignas(256) constexpr std::array<std::uint32_t, 24> LoadedDepthCode{
    0x7e000d05u, 0x080200f4u, 0x10020300u, 0x100202f4u, 0x060202f3u, 0x060400f3u, 0x10040500u, 0x060404f3u,
    0xf4000304u, 0xfa000000u, 0xbf8cc07fu, 0x7e06020cu,
    0x7e0802f2u, 0x7e0a02ffu, 0x44800000u, 0xf80000d1u, 0x00000005u, 0xf80008cfu, 0x04030201u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u,
};
alignas(256) constexpr std::array<std::uint32_t, 8> PixelCode{0x7e000200u, 0x7e020280u, 0xf800180fu, 0x00010100u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u};
alignas(256) std::array<std::uint32_t, 64> Depths{};

using Target = std::array<std::byte, 1024>;
alignas(256) Target DirectMemory{};
alignas(256) Target CachedMemory{};
alignas(256) std::array<std::uint32_t, RecordWords * (BenchmarkDraws + 8)> Records{};

struct Header {
    Shader shader{};
    ShaderUserData userData{};
};

alignas(8) Header VertexHeader{};
alignas(8) Header LoadedDepthHeader{};
alignas(8) Header PixelHeader{};

void Registered(Header& header, const std::uint32_t* code, std::size_t bytes, std::uint8_t type) {
    header.shader.file_header = 0x34333231u;
    header.shader.version = 0x18u;
    header.shader.header_size = sizeof(Header);
    header.shader.shader_size = static_cast<std::uint32_t>(bytes);
    header.shader.code = code;
    header.shader.type = type;
    header.shader.user_data = &header.userData;
    AgcDriverRegisterShader_nid_postfix(&header.shader);
}

void Append(std::vector<std::uint32_t>& commands, std::uint32_t opcode, std::initializer_list<std::uint32_t> payload) {
    commands.push_back(0xc0000000u | (static_cast<std::uint32_t>(payload.size() - 1) << 16u) | (opcode << 8u));
    commands.insert(commands.end(), payload);
}

std::vector<std::uint32_t> State(std::uint32_t primitive, bool loadedDepth = false) {
    const auto vertex = reinterpret_cast<std::uintptr_t>(loadedDepth ? LoadedDepthCode.data() : VertexCode.data());
    const auto depths = reinterpret_cast<std::uintptr_t>(Depths.data());
    const auto pixel = reinterpret_cast<std::uintptr_t>(PixelCode.data());
    const auto records = reinterpret_cast<std::uintptr_t>(Records.data());
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> context{
        {0x2d5, 0x2000},
        {0x1b6, 0}, {0x207, 0x01200000}, {0x200, 0}, {0x203, 0x800},
        {0x2dc, 0xaa00}, {0x2f8, 0}, {0x292, 2}, {0x293, 0},
        {0x80, 0}, {0x8d, 0}, {0x83, 0xffff}, {0x8c, 0xa},
        {0x2f9, 0x2d}, {0x313, 0x6000}, {0x30e, 0xffffffff}, {0x30f, 0xffffffff},
        {0x206, 0x43f}, {0x204, 0x80000}, {0x205, 0x240},
        {0x8e, 0xf}, {0x8f, 0xf}, {0x202, 0xcc0010},
        {0x1c4, 0}, {0x1c5, 9}, {0x1c3, 4}, {0x31c, 0x28028},
        {0x31b, 0}, {0x31d, 0}, {0x3b0, (63u << 14u) | 3u},
        {0x3b8, 0x9000000}, {0x1e0, 0},
        {0xc, 0}, {0xd, 0x40040},
        {0x81, 0x80000000}, {0x82, 0x40040},
        {0x90, 0x80000000}, {0x91, 0x40040},
        {0x94, 0x80000000}, {0x95, 0x40040},
        {0x10f, std::bit_cast<std::uint32_t>(32.0f)}, {0x110, std::bit_cast<std::uint32_t>(32.0f)},
        {0x111, std::bit_cast<std::uint32_t>(-2.0f)}, {0x112, std::bit_cast<std::uint32_t>(2.0f)},
        {0x113, std::bit_cast<std::uint32_t>(1.0f)}, {0x114, 0},
        {0xb4, 0}, {0xb5, std::bit_cast<std::uint32_t>(1.0f)},
        {0x2fe, 0}, {0x302, 0}, {0x306, 0}, {0x30a, 0},
    };
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> shader{
        {0xc8, static_cast<std::uint32_t>(vertex >> 8u)}, {0xc9, static_cast<std::uint32_t>(vertex >> 40u)}, {0x8b, 1u << 1u},
        {0x008, static_cast<std::uint32_t>(pixel >> 8u)}, {0x009, static_cast<std::uint32_t>(pixel >> 40u)}, {0x00b, 1u << 1u}, {0x00c, std::bit_cast<std::uint32_t>(1.0f)},
    };
    std::vector<std::uint32_t> commands;
    Append(commands, 0x79, {0x242, primitive});
    Append(commands, 0x79, {0x24a, 0});
    for (const auto& [offset, value] : context) Append(commands, 0x69, {offset, value});
    for (const auto& [offset, value] : shader) Append(commands, 0x76, {offset, value});
    if (loadedDepth) {
        Append(commands, 0x76, {0x8b, 2u << 1u});
        Append(commands, 0x76, {0x8d, static_cast<std::uint32_t>(depths >> 32u)});
    }
    Append(commands, 0x11, {1, static_cast<std::uint32_t>(records), static_cast<std::uint32_t>(records >> 32u)});
    return commands;
}

void Into(std::vector<std::uint32_t>& commands, Target& memory, std::uint32_t userWord) {
    const auto color = reinterpret_cast<std::uintptr_t>(memory.data());
    Append(commands, 0x69, {0x318, static_cast<std::uint32_t>(color >> 8u)});
    Append(commands, 0x69, {0x390, static_cast<std::uint32_t>(color >> 40u)});
    Append(commands, 0x76, {BaseVertexLocation, userWord});
}

void Indirect(std::vector<std::uint32_t>& commands, std::uint32_t record) {
    Append(commands, 0x24, {record * RecordWords * 4u, BaseVertexLocation, 0, 2});
}

void SetRecord(std::uint32_t record, std::uint32_t vertices, std::uint32_t firstVertex) {
    const std::array<std::uint32_t, RecordWords> words{vertices, 1, firstVertex, 0};
    std::copy(words.begin(), words.end(), Records.begin() + record * RecordWords);
}

Counters Submit(std::vector<std::uint32_t> commands) {
    Packet packet{commands.data(), static_cast<std::uint32_t>(commands.size()), 0, {}};
    Require(sceAgcDriverSubmitDcb(&packet) == 0, "the draw submission failed");
    AgcDriverWaitIdle_nid_postfix();
    return AgcDriver::DriverDetail::Driver::Get().DrawCacheCounters();
}

void Flush(Target& memory) {
    AgcDriver::GuestMemory::FlushGpuWrites(reinterpret_cast<std::uintptr_t>(memory.data()), memory.size());
}

std::size_t Covered(Target& memory) {
    Flush(memory);
    return static_cast<std::size_t>(std::count_if(memory.begin(), memory.end(), [](std::byte value) { return value != Kept; }));
}

void RequireSame(Target& actual, Target& expected, const std::string& what) {
    Flush(actual);
    Flush(expected);
    for (std::size_t index = 0; index < actual.size(); ++index) {
        Require(actual[index] == expected[index], what + ": byte " + std::to_string(index) + " is " + std::to_string(static_cast<unsigned>(actual[index])) + ", the direct draw wrote " + std::to_string(static_cast<unsigned>(expected[index])));
    }
}

bool Enabled() {
    return std::getenv("APS5_NO_INDIRECT_DRAW_ENTRIES") == nullptr;
}

struct Delta {
    std::uint64_t hits, inserts, reused, built;
};

Delta Since(const Counters& before, const Counters& after) {
    return {after.hits - before.hits, after.inserts - before.inserts, after.indirectStagesReused - before.indirectStagesReused, after.indirectStagesBuilt - before.indirectStagesBuilt};
}

std::string Describe(const Delta& delta) {
    return std::to_string(delta.hits) + " hits, " + std::to_string(delta.inserts) + " inserts, " + std::to_string(delta.reused) + " record stages reused and " + std::to_string(delta.built) + " built";
}

void RequireDelta(const Delta& delta, const Delta& expected, const std::string& what) {
    const bool same = delta.hits == expected.hits && delta.inserts == expected.inserts && delta.reused == expected.reused && delta.built == expected.built;
    Require(same, what + ": " + Describe(delta) + ", expected " + Describe(expected));
}

void CachedRecordsDrawTheirRecords(std::uint32_t primitive, const std::string& path) {
    const bool enabled = Enabled();
    DirectMemory.fill(Kept);
    CachedMemory.fill(Kept);
    SetRecord(0, 3, 0);
    auto direct = State(primitive);
    Into(direct, DirectMemory, 0);
    Append(direct, 0x2d, {3, 2});
    Submit(direct);
    Require(Covered(DirectMemory) != 0, path + ": the direct reference draw covered nothing");

    auto repeated = State(primitive);
    Into(repeated, CachedMemory, Clipped);
    for (int draw = 0; draw < 4; ++draw) Indirect(repeated, 0);
    auto once = State(primitive);
    Into(once, CachedMemory, Clipped);
    Indirect(once, 0);

    const auto before = AgcDriver::DriverDetail::Driver::Get().DrawCacheCounters();
    const auto cached = Submit(repeated);
    RequireDelta(Since(before, cached), enabled ? Delta{2, 1, 2, 2} : Delta{}, path + ": four identical CPU-path indirect draws whose record patches the word the shader reads as depth made");
    RequireSame(CachedMemory, DirectMemory, path + ": the cached indirect draws of record {3, 1, 0, 0}");

    const auto changed = [&](std::uint32_t vertices, std::uint32_t firstVertex, const Counters& from, const std::string& what) {
        SetRecord(0, vertices, firstVertex);
        CachedMemory.fill(Kept);
        const auto after = Submit(once);
        RequireDelta(Since(from, after), enabled ? Delta{1, 0, 0, 1} : Delta{}, path + ": the same draw after " + what + " made");
        const auto covered = Covered(CachedMemory);
        Require(covered == 0, path + ": the draw after " + what + " covered " + std::to_string(covered) + " bytes, expected none");
        return after;
    };
    const auto recounted = changed(2, 0, cached, "its record's vertex count went from 3 to 2");
    const auto repatched = changed(3, Clipped, recounted, "its record's patched word went from 0 to a clipped depth");

    SetRecord(0, 3, 0);
    CachedMemory.fill(Kept);
    const auto again = Submit(repeated);
    RequireDelta(Since(repatched, again), enabled ? Delta{4, 0, 4, 0} : Delta{}, path + ": four draws back on record {3, 1, 0, 0} made");
    RequireSame(CachedMemory, DirectMemory, path + ": the indirect draws back on record {3, 1, 0, 0}");

    SetRecord(0, 3, Clipped);
    const auto written = reinterpret_cast<std::uintptr_t>(Records.data() + 2);
    auto rewritten = State(primitive);
    Into(rewritten, CachedMemory, Clipped);
    Indirect(rewritten, 0);
    Append(rewritten, 0x37, {0x00100200u, static_cast<std::uint32_t>(written), static_cast<std::uint32_t>(written >> 32u), 0u});
    Indirect(rewritten, 0);
    CachedMemory.fill(Kept);
    const auto afterWrite = Submit(rewritten);
    Require(Records[2] == 0, path + ": WRITE_DATA did not write the record's first vertex");
    RequireDelta(Since(again, afterWrite), enabled ? Delta{2, 0, 2, 0} : Delta{}, path + ": a draw of the clipped record, a WRITE_DATA of its first vertex to 0 and the same draw again made");
    RequireSame(CachedMemory, DirectMemory, path + ": the draw after a WRITE_DATA set its record's depth to 0 (a stage kept for the clipped record drew it)");
    std::cout << path << ": cached CPU-path indirect draws drew their records\n";
}

void RecordStagesRevalidateTheirReads() {
    const bool enabled = Enabled();
    const auto low = [](const std::uint32_t& word) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&word)); };
    DirectMemory.fill(Kept);
    CachedMemory.fill(Kept);
    Depths.fill(0);
    Depths[1] = Clipped;
    SetRecord(0, 3, low(Depths[0]));
    auto direct = State(TriangleList, true);
    Into(direct, DirectMemory, low(Depths[0]));
    Append(direct, 0x2d, {3, 2});
    Submit(direct);
    Require(Covered(DirectMemory) != 0, "the direct draw of a depth loaded through a pointer to depth 0 covered nothing");

    auto repeated = State(TriangleList, true);
    Into(repeated, CachedMemory, low(Depths[1]));
    for (int draw = 0; draw < 4; ++draw) Indirect(repeated, 0);
    const auto before = AgcDriver::DriverDetail::Driver::Get().DrawCacheCounters();
    const auto cached = Submit(repeated);
    RequireDelta(Since(before, cached), enabled ? Delta{2, 1, 2, 2} : Delta{}, "four identical CPU-path indirect draws whose record patches the low word of the pointer their depth is loaded through made");
    RequireSame(CachedMemory, DirectMemory, "the cached indirect draws of a depth loaded through their record's pointer");

    Depths[0] = Clipped;
    CachedMemory.fill(Kept);
    auto once = State(TriangleList, true);
    Into(once, CachedMemory, low(Depths[1]));
    Indirect(once, 0);
    const auto reloaded = Submit(once);
    RequireDelta(Since(cached, reloaded), enabled ? Delta{1, 0, 0, 1} : Delta{}, "the same draw after the depth its record points at changed, the one its register points at unchanged, made");
    const auto covered = Covered(CachedMemory);
    Require(covered == 0, "the draw after the depth its record points at became clipped covered " + std::to_string(covered) + " bytes, expected none (a record stage kept for the old depth drew)");
    Depths[0] = 0;
    std::cout << "a record stage whose own reads changed was built again\n";
}

class TiledTarget {
public:
    TiledTarget() : data(static_cast<std::byte*>(std::aligned_alloc(TiledBytes, TiledBytes))) {
        Require(data != nullptr, "cannot allocate the tiled color target");
        GuestAllocations::Mutation mutation;
        mutation.Add(data, TiledBytes, true, true, true);
    }
    ~TiledTarget() {
        AgcDriver::Graphics::StorageTexture::FlushPending(Address(), TiledBytes, nullptr, "benchmark release");
        {
            GuestAllocations::Mutation mutation;
            mutation.Remove(data);
        }
        std::free(data);
    }
    TiledTarget(const TiledTarget&) = delete;
    TiledTarget& operator=(const TiledTarget&) = delete;
    std::uintptr_t Address() const { return reinterpret_cast<std::uintptr_t>(data); }

private:
    std::byte* data;
};

double DrawMicroseconds(const TiledTarget& target, std::uint32_t primitive, bool changing) {
    auto commands = State(primitive);
    Append(commands, 0x69, {0x318, static_cast<std::uint32_t>(target.Address() >> 8u)});
    Append(commands, 0x69, {0x390, static_cast<std::uint32_t>(target.Address() >> 40u)});
    Append(commands, 0x69, {0x3b8, TiledAttrib3});
    Append(commands, 0x76, {BaseVertexLocation, Clipped});
    for (std::uint32_t draw = 0; draw < BenchmarkDraws; ++draw) Indirect(commands, changing ? draw : 0);
    for (std::uint32_t draw = 0; draw < BenchmarkDraws; ++draw) SetRecord(draw, 3, std::bit_cast<std::uint32_t>(static_cast<float>(draw) / 512.0f));
    Packet packet{commands.data(), static_cast<std::uint32_t>(commands.size()), 0, {}};
    const auto start = std::chrono::steady_clock::now();
    Require(sceAgcDriverSubmitDcb(&packet) == 0, "the benchmark submission failed");
    AgcDriverWaitIdle_nid_postfix();
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / BenchmarkDraws;
}

void Benchmark() {
    Registered(VertexHeader, VertexCode.data(), sizeof(VertexCode), 2);
    Registered(PixelHeader, PixelCode.data(), sizeof(PixelCode), 1);
    const TiledTarget target;
    static_cast<void>(DrawMicroseconds(target, TriangleList, false));
    for (const bool changing : {false, true}) {
        std::vector<double> rounds;
        for (int round = 0; round < 5; ++round) rounds.push_back(DrawMicroseconds(target, TriangleList, changing));
        auto sorted = rounds;
        std::sort(sorted.begin(), sorted.end());
        std::printf("indirect draws %s: us per draw %.1f %.1f %.1f %.1f %.1f, median %.1f\n", changing ? "whose record changes every draw" : "repeating one record", rounds[0], rounds[1], rounds[2], rounds[3], rounds[4], sorted[2]);
    }
    const auto counters = AgcDriver::DriverDetail::Driver::Get().DrawCacheCounters();
    std::printf("draw cache: %llu lookups, %llu hits, %llu inserts; record stages %llu reused, %llu built\n", static_cast<unsigned long long>(counters.lookups), static_cast<unsigned long long>(counters.hits), static_cast<unsigned long long>(counters.inserts), static_cast<unsigned long long>(counters.indirectStagesReused), static_cast<unsigned long long>(counters.indirectStagesBuilt));
}

}

int main(int argc, char** argv) {
    try {
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
        }
        if (argc > 1 && std::string(argv[1]) == "--benchmark") {
            Benchmark();
            AgcDriverShutdown_nid_postfix();
            return 0;
        }
        Registered(VertexHeader, VertexCode.data(), sizeof(VertexCode), 2);
        Registered(LoadedDepthHeader, LoadedDepthCode.data(), sizeof(LoadedDepthCode), 2);
        Registered(PixelHeader, PixelCode.data(), sizeof(PixelCode), 1);
        CachedRecordsDrawTheirRecords(TriangleList, "a triangle list whose patched word is not folded");
        CachedRecordsDrawTheirRecords(RectList, "a rect list (non-vertex path)");
        RecordStagesRevalidateTheirReads();
        AgcDriverShutdown_nid_postfix();
        std::puts(Enabled() ? "CPU-path indirect draws reused their cached draw and record stages only while their records held" : "CPU-path indirect draws drew their records with their draw entries off");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        AgcDriverShutdown_nid_postfix();
        return 1;
    }
}
