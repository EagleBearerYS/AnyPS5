#include "DrawSubmitFixture.hpp"

namespace {

using namespace DrawSubmit;

constexpr std::uint32_t Width = 64;
constexpr std::uint32_t Height = 32;
constexpr std::uint32_t Vertices = 3;
constexpr std::uint32_t IdentitySelectors = 0xfacu;
constexpr std::uint32_t ReversedSelectors = 7u | (6u << 3u) | (1u << 6u) | (0u << 9u);
constexpr std::uint32_t VertexUserWords = 8;

TestGuest::Array<std::uint32_t, Width * Height> Pixels;
TestGuest::Array<std::uint32_t, Vertices * 4> Output;
TestGuest::Array<float, 4> Element;

Header VertexHeader;
Header PixelHeader;

struct Case {
    std::uint32_t select;
    std::uint32_t records;
    bool inRange;
    const char* what;
    std::uint32_t selectors = IdentitySelectors;
};

constexpr std::array<Case, 6> Fetches{{
    {2u, 1u, true, "OOB_SELECT 2, one record"},
    {2u, 0u, false, "OOB_SELECT 2, no records"},
    {3u, 16u, true, "OOB_SELECT 3, whole element in range"},
    {3u, 0u, false, "OOB_SELECT 3, no records"},
    {3u, 16u, true, "OOB_SELECT 3, selectors W Z 1 0", ReversedSelectors},
    {3u, 0u, false, "OOB_SELECT 3, no records, selectors W Z 1 0", ReversedSelectors},
}};

const Program& FetchVertex() {
    static const Program program = [] {
        using namespace DrawSubmit::Isa;
        Program built;
        built << Vop2(VLshlRevB32, 1, InlineInteger(4), 5)
              << Indexed(BufferLoadFormatXyzw, 8, 5, 8)
              << WaitAll
              << Mubuf(BufferStoreDwordX4, 8, 1, 12)
              << Export(PositionTarget, 8, false)
              << EndProgram;
        return built;
    }();
    return program;
}

const Program& ConstantPixel() {
    static const Program program = [] {
        using namespace DrawSubmit::Isa;
        Program built;
        built << Vop1(VMovB32, 7, InlineOne)
              << Vop1(VMovB32, 8, InlineOne)
              << Vop1(VMovB32, 9, InlineOne)
              << Vop1(VMovB32, 10, InlineOne)
              << Export(ColorTarget, 7, true)
              << EndProgram;
        return built;
    }();
    return program;
}

std::array<std::uint32_t, VertexUserWords> UserData(const Case& value) {
    const auto element = AddressOf(Element.data());
    const auto output = DeviceTable::Buffer(Output.data(), static_cast<std::uint32_t>(decltype(Output)::ByteSize));
    return {Low(element), High(element) & 0xffffu, value.records, 0x0104d000u | value.selectors | (value.select << 28u), output[0], output[1], output[2], output[3]};
}

void Draw(const Case& value) {
    Pixels.fill(0x40404040u);
    Output.fill(DeviceTable::Untouched);
    Commands commands;
    Baseline(commands, {Pixels.data(), Width, Height}, CenterAndPosition);
    Programs(commands, FetchVertex(), VertexUserWords, ConstantPixel(), 0u);
    Instances(commands, 1u);
    VertexUserData(commands, UserData(value));
    DrawAuto(commands, Vertices);
    commands.Submit(0);
    NewPath::WaitIdle();
}

void Check(const Case& value) {
    const auto output = ReadBack(Output.data(), Output.size());
    for (std::uint32_t vertex = 0; vertex < Vertices; ++vertex) {
        for (std::uint32_t component = 0; component < 4u; ++component) {
            const auto selector = (value.selectors >> (component * 3u)) & 7u;
            const auto selected = selector >= 4u ? std::bit_cast<std::uint32_t>(Element[selector - 4u]) : selector == 1u ? Bits(1.0f) : 0u;
            const auto expected = value.inRange || selector < 4u ? selected : 0u;
            const auto actual = output[vertex * 4u + component];
            Expect(actual == expected, std::string("zero-stride vertex fetch, ") + value.what + ": vertex " + std::to_string(vertex) + " component " + std::to_string(component) + " is " + Hex(actual) + ", expected " + Hex(expected));
        }
    }
}

void Run() {
    Element[0] = 0.25f;
    Element[1] = -2.0f;
    Element[2] = 7.5f;
    Element[3] = 1.0f;
    Register(VertexHeader, FetchVertex(), VertexShaderType);
    Register(PixelHeader, ConstantPixel(), PixelShaderType);
    const auto before = StageCompiles();
    for (const auto& value : Fetches) {
        Draw(value);
        Check(value);
    }
    ExpectStageCompiles(before, 2u, "zero-stride vertex fetch: six V#s drawn with one program pair");
}

}

int main() {
    return DrawSubmit::Main("zero-stride vertex fetch", Run);
}
