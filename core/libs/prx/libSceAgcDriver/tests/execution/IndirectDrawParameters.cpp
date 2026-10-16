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
#include <cstring>
#include <ctime>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Indirect draws whose CP-patched SGPRs the vertex stage reads as data (not as a vertex offset),
// drawn GPU-side with the SGPRs read from the draw's parameters, against the CPU path that folds
// each record into its own stage. --benchmark (not registered) times packets of 1, 8 and 64
// records both ways.

namespace {

using AgcDriver::Graphics::Require;

constexpr std::byte Kept{0x40};
constexpr std::uint32_t BaseVertexLocation = 0x8c;
constexpr std::uint32_t StartInstanceLocation = 0x8d;
constexpr std::uint32_t DrawIndexLocation = 0x8e;
constexpr std::uint32_t DrawIndexEnable = 1u << 31u;
constexpr std::uint32_t TriangleList = 4;
constexpr std::uint32_t Stale = 0x7777u;
constexpr std::size_t BlockBytes = 65536;
constexpr std::uint32_t TiledAttrib3 = 0x09000000u | (0x1au << 14u);

// The target is 64 x 4 pixels, two rows of sixteen 4 x 2 pixel cells. Each (record, instance)
// draws half a cell: column s8 + v8 + 2 * s10 (base vertex + instance + 2 * draw index), row s9
// (start instance), the corner from the vertex index (bit 0: right, bit 1: up). None of the SGPRs
// is added to the vertex index, so none folds, and a wrong base vertex, start instance, draw
// index, vertex index or instance index moves a triangle.
alignas(256) constexpr std::array<std::uint32_t, 30> DrawParameterCode{
    0x36020a81u, // v_and_b32 v1, 1, v5
    0x2c040a81u, // v_lshrrev_b32 v2, 1, v5
    0x4a020208u, // v_add_nc_u32 v1, s8, v1
    0x4a021101u, // v_add_nc_u32 v1, v1, v8
    0x7e06020au, // v_mov_b32 v3, s10
    0x34060681u, // v_lshlrev_b32 v3, 1, v3
    0x4a020701u, // v_add_nc_u32 v1, v1, v3
    0x4a040409u, // v_add_nc_u32 v2, s9, v2
    0x7e020d01u, // v_cvt_f32_u32 v1, v1
    0x7e040d02u, // v_cvt_f32_u32 v2, v2
    0x100202ffu, 0x3e000000u, // v_mul_f32 v1, 0.125, v1
    0x060202f3u, // v_add_f32 v1, -1.0, v1
    0x060404f3u, // v_add_f32 v2, -1.0, v2
    0x7e060280u, // v_mov_b32 v3, 0
    0x7e0802f2u, // v_mov_b32 v4, 1.0
    0x7e0c02ffu, 0x44800000u, // v_mov_b32 v6, 1024.0
    0xf80000d1u, 0x00000006u, // exp pos1 v6
    0xf80008cfu, 0x04030201u, // exp pos0 v1, v2, v3, v4 done
    0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u,
};
// BF-015's stage: the record's patched base vertex word (s8) is the depth.
alignas(256) constexpr std::array<std::uint32_t, 20> DepthCode{
    0x7e000d05u, 0x080200f4u, 0x10020300u, 0x100202f4u, 0x060202f3u, 0x060400f3u, 0x10040500u, 0x060404f3u,
    0x7e060208u, 0x7e0802f2u, 0x7e0a02ffu, 0x44800000u, 0xf80000d1u, 0x00000005u, 0xf80008cfu, 0x04030201u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u,
};
alignas(256) constexpr std::array<std::uint32_t, 8> PixelCode{0x7e000200u, 0x7e020280u, 0xf800180fu, 0x00010100u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u};

// Lower-left (0, 1, 2) and upper-right (1, 3, 0) halves of a cell.
alignas(256) std::array<std::uint32_t, 6> Indices{0, 1, 2, 1, 3, 0};

constexpr std::array<std::uint32_t, 16> Strip{
    3, 2, 0, 0, // columns 0, 1 in row 0 (draw 0)
    3, 2, 3, 1, // columns 5, 6 in row 1 (draw 1)
    3, 1, 7, 0, // column 11 in row 0 (draw 2)
    3, 1, 1, 1, // column 7 in row 1 (draw 3)
};
constexpr std::array<std::uint32_t, 15> IndexedStrip{
    3, 1, 0, 2, 0, // column 2, row 0, lower-left (draw 0)
    3, 2, 3, 5, 1, // columns 7, 8, row 1, upper-right (draw 1)
    3, 1, 0, 9, 1, // column 13, row 1, lower-left (draw 2)
};

using Target = std::array<std::byte, 1024>;
alignas(256) Target CpuStrip{};
alignas(256) Target GpuStrip{};
alignas(256) Target FallbackStrip{};
alignas(256) Target CpuIndexed{};
alignas(256) Target GpuIndexed{};
alignas(256) Target FallbackIndexed{};
// Not a guest allocation, so the GPU cannot read these records in place.
alignas(256) std::array<std::uint32_t, 32> UnimportedRecords{};

struct Header {
    Shader shader{};
    ShaderUserData userData{};
};

alignas(8) Header DrawParameterHeader{};
alignas(8) Header DepthHeader{};
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

// A guest allocation the GPU can import (records read in place, a tiled target).
class GuestBlock {
public:
    GuestBlock() : data(static_cast<std::byte*>(std::aligned_alloc(BlockBytes, BlockBytes))) {
        Require(data != nullptr, "cannot allocate a guest block");
        std::memset(data, 0, BlockBytes);
        GuestAllocations::Mutation mutation;
        mutation.Add(data, BlockBytes, true, true, true);
    }
    ~GuestBlock() {
        AgcDriver::Graphics::StorageTexture::FlushPending(Address(), BlockBytes, nullptr, "test release");
        {
            GuestAllocations::Mutation mutation;
            mutation.Remove(data);
        }
        std::free(data);
    }
    GuestBlock(const GuestBlock&) = delete;
    GuestBlock& operator=(const GuestBlock&) = delete;
    std::uintptr_t Address() const { return reinterpret_cast<std::uintptr_t>(data); }
    std::uint32_t* Words() const { return reinterpret_cast<std::uint32_t*>(data); }

private:
    std::byte* data;
};

std::vector<std::uint32_t> State(const std::uint32_t* vertexCode, std::uint32_t userSgprs, std::uintptr_t records) {
    const auto vertex = reinterpret_cast<std::uintptr_t>(vertexCode);
    const auto pixel = reinterpret_cast<std::uintptr_t>(PixelCode.data());
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
        {0xc8, static_cast<std::uint32_t>(vertex >> 8u)}, {0xc9, static_cast<std::uint32_t>(vertex >> 40u)}, {0x8b, userSgprs << 1u},
        {0x008, static_cast<std::uint32_t>(pixel >> 8u)}, {0x009, static_cast<std::uint32_t>(pixel >> 40u)}, {0x00b, 1u << 1u}, {0x00c, std::bit_cast<std::uint32_t>(1.0f)},
    };
    std::vector<std::uint32_t> commands;
    Append(commands, 0x79, {0x242, TriangleList});
    Append(commands, 0x79, {0x24a, 0});
    for (const auto& [offset, value] : context) Append(commands, 0x69, {offset, value});
    for (const auto& [offset, value] : shader) Append(commands, 0x76, {offset, value});
    // Stale words: the CP overwrites them per record.
    for (std::uint32_t sgpr = 0; sgpr < userSgprs; ++sgpr) Append(commands, 0x76, {BaseVertexLocation + sgpr, Stale});
    Append(commands, 0x11, {1, static_cast<std::uint32_t>(records), static_cast<std::uint32_t>(records >> 32u)});
    return commands;
}

void Into(std::vector<std::uint32_t>& commands, std::uintptr_t color) {
    Append(commands, 0x69, {0x318, static_cast<std::uint32_t>(color >> 8u)});
    Append(commands, 0x69, {0x390, static_cast<std::uint32_t>(color >> 40u)});
}

// DRAW_INDIRECT_MULTI (0x2c) or DRAW_INDEX_INDIRECT_MULTI (0x38) of `count` records at `offset`.
void Multi(std::vector<std::uint32_t>& commands, bool indexed, std::uint32_t offset, std::uint32_t count, bool drawParameters) {
    const std::uint32_t stride = indexed ? 20 : 16;
    const std::uint32_t initiator = indexed ? 0 : 2;
    if (drawParameters) Append(commands, indexed ? 0x38 : 0x2c, {offset, BaseVertexLocation, StartInstanceLocation, DrawIndexLocation | DrawIndexEnable, count, 0, 0, stride, initiator});
    else Append(commands, indexed ? 0x38 : 0x2c, {offset, BaseVertexLocation, 0, 0, count, 0, 0, stride, initiator});
}

AgcDriver::DriverDetail::DrawEntryCounters Submit(std::vector<std::uint32_t> commands) {
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

// Covered pixels of a 4 x 2 cell (column, row) of the 64 x 4 RGBA8 target (row 0 at the bottom:
// the viewport's Y scale is negative).
std::size_t CellCovered(const Target& memory, std::uint32_t column, std::uint32_t row) {
    std::size_t covered = 0;
    for (std::uint32_t y = (1 - row) * 2; y < (1 - row) * 2 + 2; ++y) {
        for (std::uint32_t x = column * 4; x < column * 4 + 4; ++x) {
            if (memory[(y * 64 + x) * 4] != Kept) ++covered;
        }
    }
    return covered;
}

std::string Picture(const Target& memory) {
    std::string text;
    for (std::uint32_t y = 0; y < 4; ++y) {
        for (std::uint32_t x = 0; x < 64; ++x) text += memory[(y * 64 + x) * 4] != Kept ? '#' : '.';
        text += '\n';
    }
    return text;
}

void RequireSame(Target& actual, Target& expected, const std::string& what) {
    Flush(actual);
    Flush(expected);
    for (std::size_t index = 0; index < actual.size(); ++index) {
        Require(actual[index] == expected[index], what + ": byte " + std::to_string(index) + " is " + std::to_string(static_cast<unsigned>(actual[index])) + ", the CPU path wrote " + std::to_string(static_cast<unsigned>(expected[index])) + "\n" + Picture(actual) + "the CPU path's:\n" + Picture(expected));
    }
}

void SetDrawParameters(bool on) {
    if (on) unsetenv("APS5_NO_INDIRECT_DRAW_PARAMETERS");
    else setenv("APS5_NO_INDIRECT_DRAW_PARAMETERS", "1", 1);
}

std::uint64_t Entries(const AgcDriver::DriverDetail::DrawEntryCounters& counters) {
    return counters.hits + counters.inserts;
}

// Draws `records` (`count` of them) three times into `target` from `at` (guest memory or not).
std::uint64_t DrawInto(Target& target, bool indexed, std::uintptr_t at, std::uint32_t count) {
    target.fill(Kept);
    auto commands = State(DrawParameterCode.data(), 3, at);
    if (indexed) {
        const auto indices = reinterpret_cast<std::uintptr_t>(Indices.data());
        Append(commands, 0x2a, {1});
        Append(commands, 0x26, {static_cast<std::uint32_t>(indices), static_cast<std::uint32_t>(indices >> 32u)});
        Append(commands, 0x13, {static_cast<std::uint32_t>(Indices.size())});
    }
    Into(commands, reinterpret_cast<std::uintptr_t>(target.data()));
    for (int repeat = 0; repeat < 3; ++repeat) Multi(commands, indexed, 0, count, true);
    const auto before = AgcDriver::DriverDetail::Driver::Get().DrawCacheCounters();
    const auto after = Submit(std::move(commands));
    return Entries(after) - Entries(before);
}

void RecordsDrawTheirParameters(bool indexed) {
    const std::string what = indexed ? "DRAW_INDEX_INDIRECT_MULTI" : "DRAW_INDIRECT_MULTI";
    const std::span<const std::uint32_t> records = indexed ? std::span<const std::uint32_t>(IndexedStrip) : std::span<const std::uint32_t>(Strip);
    const auto count = static_cast<std::uint32_t>(records.size() / (indexed ? 5 : 4));
    auto& cpu = indexed ? CpuIndexed : CpuStrip;
    auto& gpu = indexed ? GpuIndexed : GpuStrip;
    auto& fallback = indexed ? FallbackIndexed : FallbackStrip;
    const GuestBlock block;
    std::copy(records.begin(), records.end(), block.Words());
    std::copy(records.begin(), records.end(), UnimportedRecords.begin());

    SetDrawParameters(false);
    const auto cpuEntries = DrawInto(cpu, indexed, block.Address(), count);
    Require(cpuEntries == 0, what + ": the CPU path used a draw entry (" + std::to_string(cpuEntries) + " hits and inserts)");
    const auto covered = Covered(cpu);
    Require(covered != 0, what + ": the CPU path drew nothing");
    // The cells the records name, from the CPU path (the reference) itself.
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> cells = indexed ? std::vector<std::pair<std::uint32_t, std::uint32_t>>{{2, 0}, {7, 1}, {8, 1}, {13, 1}} : std::vector<std::pair<std::uint32_t, std::uint32_t>>{{0, 0}, {1, 0}, {5, 1}, {6, 1}, {11, 0}, {7, 1}};
    std::size_t inCells = 0;
    for (const auto& [column, row] : cells) {
        const auto cell = CellCovered(cpu, column, row);
        Require(cell != 0, what + ": the CPU path left cell (" + std::to_string(column) + ", " + std::to_string(row) + ") empty\n" + Picture(cpu));
        inCells += cell;
    }
    Require(inCells * 4 == covered, what + ": the CPU path drew outside the records' cells\n" + Picture(cpu));

    SetDrawParameters(true);
    const auto gpuEntries = DrawInto(gpu, indexed, block.Address(), count);
    Require(gpuEntries != 0, what + ": the draws read their parameters but did not use a draw entry (still on the CPU path)");
    RequireSame(gpu, cpu, what + " read in place with the draw's parameters");
    const auto fallbackEntries = DrawInto(fallback, indexed, reinterpret_cast<std::uintptr_t>(UnimportedRecords.data()), count);
    Require(fallbackEntries != 0, what + ": the draws of unimported records did not use a draw entry");
    RequireSame(fallback, cpu, what + " read by the CPU for one multi-draw (records not imported)");
    std::cout << what << ": " << count << " records drew the CPU path's image GPU-side, in place and from CPU-read records\n" << Picture(gpu);
}

double ProcessSeconds() {
    timespec time{};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time);
    return static_cast<double>(time.tv_sec) + static_cast<double>(time.tv_nsec) * 1e-9;
}

struct Timing {
    double wallPerPacket;
    double cpuPerPacket;
};

// `packets` DRAW_INDIRECT_MULTI packets of `records` records each, as BF-015's benchmark: the
// depth stage into a tiled target, every record a different depth.
Timing DrawPackets(const GuestBlock& target, const GuestBlock& records, std::uint32_t perPacket, std::uint32_t packets) {
    auto commands = State(DepthCode.data(), 1, records.Address());
    Append(commands, 0x69, {0x318, static_cast<std::uint32_t>(target.Address() >> 8u)});
    Append(commands, 0x69, {0x390, static_cast<std::uint32_t>(target.Address() >> 40u)});
    Append(commands, 0x69, {0x3b8, TiledAttrib3});
    for (std::uint32_t packet = 0; packet < packets; ++packet) Multi(commands, false, 0, perPacket, false);
    for (std::uint32_t record = 0; record < perPacket; ++record) {
        const std::array<std::uint32_t, 4> words{3, 1, std::bit_cast<std::uint32_t>(static_cast<float>(record) / 128.0f), 0};
        std::copy(words.begin(), words.end(), records.Words() + record * 4);
    }
    Packet packet{commands.data(), static_cast<std::uint32_t>(commands.size()), 0, {}};
    const auto cpuStart = ProcessSeconds();
    const auto start = std::chrono::steady_clock::now();
    Require(sceAgcDriverSubmitDcb(&packet) == 0, "the benchmark submission failed");
    AgcDriverWaitIdle_nid_postfix();
    const auto wall = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
    return {wall / packets, (ProcessSeconds() - cpuStart) * 1e6 / packets};
}

void Benchmark() {
    Registered(DepthHeader, DepthCode.data(), sizeof(DepthCode), 2);
    Registered(PixelHeader, PixelCode.data(), sizeof(PixelCode), 1);
    const GuestBlock target;
    const GuestBlock records;
    constexpr std::uint32_t Packets = 64;
    for (const bool gpu : {false, true}) {
        SetDrawParameters(gpu);
        for (const std::uint32_t perPacket : {1u, 8u, 64u}) {
            static_cast<void>(DrawPackets(target, records, perPacket, Packets));
            std::vector<Timing> rounds;
            for (int round = 0; round < 5; ++round) rounds.push_back(DrawPackets(target, records, perPacket, Packets));
            std::sort(rounds.begin(), rounds.end(), [](const Timing& a, const Timing& b) { return a.wallPerPacket < b.wallPerPacket; });
            const auto& median = rounds[2];
            std::printf("%s, %2u records per packet: wall %.1f us per packet (%.2f per record), process CPU %.1f us per packet (%.2f per record); wall per packet min %.1f max %.1f\n", gpu ? "gpu-side (draw parameters)" : "cpu path", perPacket, median.wallPerPacket, median.wallPerPacket / perPacket, median.cpuPerPacket, median.cpuPerPacket / perPacket, rounds.front().wallPerPacket, rounds.back().wallPerPacket);
        }
    }
    const auto counters = AgcDriver::DriverDetail::Driver::Get().DrawCacheCounters();
    std::printf("draw cache: %llu lookups, %llu hits, %llu inserts\n", static_cast<unsigned long long>(counters.lookups), static_cast<unsigned long long>(counters.hits), static_cast<unsigned long long>(counters.inserts));
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
        Registered(DrawParameterHeader, DrawParameterCode.data(), sizeof(DrawParameterCode), 2);
        Registered(PixelHeader, PixelCode.data(), sizeof(PixelCode), 1);
        RecordsDrawTheirParameters(false);
        RecordsDrawTheirParameters(true);
        AgcDriverShutdown_nid_postfix();
        std::puts("indirect draws whose patched SGPRs the stage reads as data drew the CPU path's image GPU-side");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        AgcDriverShutdown_nid_postfix();
        return 1;
    }
}
