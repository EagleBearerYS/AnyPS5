#include "DrawSubmitFixture.hpp"
#include <iostream>

namespace {

using namespace DrawSubmit;

constexpr std::uint32_t Extent = 64u;
constexpr std::uint32_t Stride = 16u;
constexpr std::uint32_t MaxVertices = 8u;
constexpr std::uint32_t VertexUserWords = 8;
constexpr std::uint32_t RestartIndexCount = 6;

using Position = std::array<float, 4>;

TestGuest::Array<std::uint32_t, Extent * Extent> Pixels;
TestGuest::Array<Position, MaxVertices> Positions;
TestGuest::Array<std::uint32_t, 4> Table;
TestGuest::Array<std::uint32_t, MaxVertices * 4> Output;
TestGuest::Array<std::uint16_t, RestartIndexCount> RestartIndices;

Header VertexHeader;
Header PixelHeader;

const Program& PositionVertex() {
    static const Program program = [] {
        using namespace DrawSubmit::Isa;
        Program built;
        built << Smem(SLoadDwordX4, 16, 8, 0)
              << Vop2(VLshlRevB32, 4, InlineInteger(4), 5)
              << WaitAll
              << Indexed(BufferLoadFormatXyzw, 0, 5, 16)
              << WaitAll
              << Mubuf(BufferStoreDwordX4, 0, 4, 12)
              << Export(PositionTarget, 0, false)
              << EndProgram;
        return built;
    }();
    return program;
}

const Program& RedPixel() {
    static const Program program = [] {
        using namespace DrawSubmit::Isa;
        Program built;
        built << Vop1(VMovB32, 0, InlineOne)
              << Vop1(VMovB32, 1, InlineInteger(0))
              << Vop1(VMovB32, 2, InlineInteger(0))
              << Vop1(VMovB32, 3, InlineOne)
              << Export(ColorTarget, 0, true)
              << EndProgram;
        return built;
    }();
    return program;
}

struct Counts {
    unsigned total = 0;
    unsigned upper = 0;
    std::array<std::array<unsigned, 16>, 16> map{};
};

std::array<std::uint32_t, VertexUserWords> UserData() {
    const auto table = AddressOf(Table.data());
    const auto output = DeviceTable::Buffer(Output.data(), static_cast<std::uint32_t>(decltype(Output)::ByteSize));
    return {Low(table), High(table), 0u, 0u, output[0], output[1], output[2], output[3]};
}

void Prepare(std::uint32_t records, const std::vector<Position>& positions, std::size_t stored) {
    Positions.fill(Position{});
    for (std::size_t index = 0; index < stored; ++index) Positions[index] = positions[index];
    const auto address = AddressOf(Positions.data());
    Table = {Low(address), (High(address) & 0xffffu) | (Stride << 16u), records, 0x01000facu | (77u << 12u)};
    Pixels.fill(0u);
    Output.fill(DeviceTable::Untouched);
}

void Begin(Commands& commands, std::uint32_t topology) {
    Baseline(commands, {Pixels.data(), Extent, Extent}, CenterAndPosition);
    PrimitiveType(commands, topology);
    Programs(commands, PositionVertex(), VertexUserWords, RedPixel(), 0u);
    Instances(commands, 1u);
    VertexUserData(commands, UserData());
}

Counts CountPixels(const char* name, std::uint32_t records) {
    const auto pixels = ReadBack(Pixels.data(), Pixels.size());
    Counts counts;
    for (std::uint32_t y = 0; y < Extent; ++y) {
        for (std::uint32_t x = 0; x < Extent; ++x) {
            const auto pixel = pixels[y * Extent + x];
            if ((pixel & 0xffu) == 0u && (pixel >> 24u) == 0u) continue;
            ++counts.total;
            const float ndcX = (static_cast<float>(x) + 0.5f) / Extent * 2.0f - 1.0f;
            const float ndcY = 1.0f - (static_cast<float>(y) + 0.5f) / Extent * 2.0f;
            if (ndcX + ndcY > -1.0f) ++counts.upper;
            ++counts.map[y / 4u][x / 4u];
        }
    }
    std::cout << "case " << name << ": records=" << records << " total=" << counts.total << " outsideFirstTriangle(x+y>-1)=" << counts.upper << "\n";
    for (const auto& row : counts.map) {
        std::string line;
        for (const auto count : row) line += count == 0u ? '.' : count == 16u ? '#' : '+';
        std::cout << "  " << line << "\n";
    }
    return counts;
}

void ExpectFetched(const char* name, std::uint32_t records, const std::vector<Position>& positions, std::size_t stored) {
    const auto output = ReadBack(Output.data(), Output.size());
    for (std::uint32_t vertex = 0; vertex < positions.size(); ++vertex) {
        for (std::uint32_t component = 0; component < 4u; ++component) {
            const auto expected = vertex < records && vertex < stored ? Bits(positions[vertex][component]) : 0u;
            const auto actual = output[vertex * 4u + component];
            Expect(actual == expected, std::string("case ") + name + ": vertex " + std::to_string(vertex) + " fetched component " + std::to_string(component) + " as " + Hex(actual) + ", expected " + Hex(expected));
        }
    }
}

Counts RunCase(const char* name, std::uint32_t records, const std::vector<Position>& positions, std::size_t stored, std::uint32_t topology = TriangleStrip) {
    Prepare(records, positions, stored);
    Commands commands;
    Begin(commands, topology);
    DrawAuto(commands, static_cast<std::uint32_t>(positions.size()));
    commands.Submit(0);
    NewPath::WaitIdle();
    ExpectFetched(name, records, positions, stored);
    return CountPixels(name, records);
}

void OnlyRestartIndices() {
    Prepare(4u, {{-1.0f, -1.0f, 0.0f, 1.0f}, {3.0f, -1.0f, 0.0f, 1.0f}, {-1.0f, 3.0f, 0.0f, 1.0f}}, 3u);
    RestartIndices.fill(0xffffu);
    Commands commands;
    Begin(commands, TriangleStrip);
    PrimitiveRestart(commands, true);
    IndexType(commands, Index16);
    DrawIndex(commands, AddressOf(RestartIndices.data()), RestartIndexCount, RestartIndexCount);
    PrimitiveRestart(commands, false);
    commands.Submit(0);
    NewPath::WaitIdle();
    const auto output = ReadBack(Output.data(), Output.size());
    Expect(std::ranges::all_of(output, [](std::uint32_t word) { return word == DeviceTable::Untouched; }), "an indexed strip of only restart indices ran its vertex program");
    const auto counts = CountPixels("R strip of only restart indices", 4u);
    Expect(counts.total == 0u, "an indexed strip of only restart indices wrote " + std::to_string(counts.total) + " pixels");
}

void Run() {
    Register(VertexHeader, PositionVertex(), VertexShaderType);
    Register(PixelHeader, RedPixel(), PixelShaderType);
    const Position v0{-1.0f, -1.0f, 0.0f, 1.0f};
    const Position v1{0.0f, -1.0f, 0.0f, 1.0f};
    const Position v2{-1.0f, 0.0f, 0.0f, 1.0f};
    const auto make = [&](const Position& last) { return std::vector<Position>{v0, v1, v2, last}; };
    const auto a = RunCase("A out-of-range vertex 3, stored as (1,1,0,1)", 3u, make({1.0f, 1.0f, 0.0f, 1.0f}), 4u);
    const auto b = RunCase("B in-range (0,0,0,0)", 4u, make({0.0f, 0.0f, 0.0f, 0.0f}), 4u);
    const auto c = RunCase("C repeat of v1", 4u, make(v1), 4u);
    const auto d = RunCase("D (0,0,0,1) at the centre", 4u, make({0.0f, 0.0f, 0.0f, 1.0f}), 4u);
    const auto e = RunCase("E (1,1,0,1) real triangle", 4u, make({1.0f, 1.0f, 0.0f, 1.0f}), 4u);
    const Position zero{0.0f, 0.0f, 0.0f, 0.0f};
    const auto f = RunCase("F negative zeros", 4u, make({-0.0f, 0.0f, -0.0f, -0.0f}), 4u);
    const auto g = RunCase("G triangle list, second triangle has (0,0,0,0)", 6u, {v0, v1, v2, v1, v2, zero}, 6u, TriangleList);
    const auto h = RunCase("H strip with two (0,0,0,0) vertices", 5u, {v0, v1, v2, zero, zero}, 5u);
    Expect(e.upper > 0u, "probe cannot see the second triangle");
    Expect(c.total > 0u && c.upper == 0u, "the first triangle is not drawn alone");
    Expect(a.upper == 0u && a.total == c.total, "a vertex fetched past num_records must clip the primitive away");
    Expect(b.upper == 0u && b.total == c.total, "a stored (0,0,0,0) vertex must clip the primitive away");
    Expect(f.upper == 0u && f.total == c.total, "a stored (-0,0,-0,-0) vertex must clip the primitive away");
    Expect(g.upper == 0u && g.total == c.total, "a triangle list with a (0,0,0,0) vertex must keep only the first triangle");
    Expect(h.upper == 0u && h.total == c.total, "a strip with two (0,0,0,0) vertices must keep only the first triangle");
    Expect(d.upper == 496u, "a (0,0,0,1) vertex must not be clipped away");
    OnlyRestartIndices();
}

}

int main() {
    return DrawSubmit::Main("zero position vertex", Run);
}
