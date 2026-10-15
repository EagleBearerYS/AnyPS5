#include "DrawSubmitFixture.hpp"

namespace {

using namespace DrawSubmit;

constexpr std::uint32_t Format16_16SNorm = 24;
constexpr std::uint32_t Format16_16Float = 29;
constexpr std::uint32_t Format10_10_10_2UNorm = 50;
constexpr std::uint32_t Format8_8_8_8UNorm = 56;
constexpr std::uint32_t Format32_32_32_32Float = 77;
constexpr std::uint32_t AllComponents = 0xfacu;
constexpr std::uint32_t XyZeroOne = 4u | (5u << 3u) | (0u << 6u) | (1u << 9u);
constexpr std::uint32_t Elements = 64;
constexpr std::uint32_t SlotsPerInstance = 64;
constexpr std::uint32_t MaxInstances = 4;
constexpr std::uint32_t SlotWords = 8;
constexpr std::uint32_t OutputWords = MaxInstances * SlotsPerInstance * SlotWords;
constexpr std::uint32_t RegionBytes = 4096;
constexpr std::uint32_t LinkWords = 64;
constexpr std::uint32_t InstanceElements = 4;
constexpr std::uint32_t VertexUserWords = 8;
constexpr std::uint32_t Index16Offset = 6;
constexpr std::uint32_t Index32Offset = 9;
constexpr std::uint32_t IndexCapacity = 64;
constexpr std::uint32_t ComputedVertices = 24;
constexpr std::uint32_t FirstSeed = 0x40u;
constexpr std::uint32_t SecondSeed = 0x90u;

enum class Kind { Auto, Indexed16, Indexed32 };

struct Pulled {
    const char* what;
    std::uint32_t format;
    std::uint32_t stride;
    std::uint32_t offset;
    std::uint32_t records;
    std::uint32_t selectors;
    std::uint32_t vertices;
    std::uint32_t instances;
    std::uint32_t instanceRecords;
    Kind kind;
    bool computed;
};

constexpr std::array<Pulled, 9> Draws{{
    {"32_32_32_32 float, stride 16", Format32_32_32_32Float, 16, 0, Elements, AllComponents, 24, 1, InstanceElements, Kind::Auto, false},
    {"8_8_8_8 unorm, stride 12, offset 8, two instances", Format8_8_8_8UNorm, 12, 8, Elements, AllComponents, 24, 2, InstanceElements, Kind::Auto, false},
    {"16_16 snorm, stride 8, offset 4, selectors X Y 0 1", Format16_16SNorm, 8, 4, Elements, XyZeroOne, 24, 1, InstanceElements, Kind::Auto, false},
    {"10_10_10_2 unorm (X in the low 10 bits), stride 20, offset 12, three instances, two instance records", Format10_10_10_2UNorm, 20, 12, Elements, AllComponents, 24, 3, 2, Kind::Auto, false},
    {"16_16 float, stride 4, selectors X Y 0 1", Format16_16Float, 4, 0, Elements, XyZeroOne, 24, 1, InstanceElements, Kind::Auto, false},
    {"32_32_32_32 float, stride 32, offset 16, 10 records for 24 vertices", Format32_32_32_32Float, 32, 16, 10, AllComponents, 24, 1, InstanceElements, Kind::Auto, false},
    {"16-bit indices in guest memory at an index offset of 6 (DRAW_INDEX_2)", Format32_32_32_32Float, 16, 0, Elements, AllComponents, 18, 1, InstanceElements, Kind::Indexed16, false},
    {"32-bit indices from INDEX_BASE at an index offset of 9 (DRAW_INDEX_OFFSET_2), two instances", Format8_8_8_8UNorm, 8, 4, Elements, AllComponents, 15, 2, InstanceElements, Kind::Indexed32, false},
    {"a vertex buffer a compute dispatch wrote earlier in the submission", Format32_32_32_32Float, 16, 0, ComputedVertices, AllComponents, 24, 1, InstanceElements, Kind::Auto, true},
}};

TestGuest::Array<std::byte, Draws.size() * RegionBytes> VertexMemory;
TestGuest::Array<std::array<float, 4>, ComputedVertices> Computed;
TestGuest::Array<std::array<float, 4>, Draws.size() + InstanceElements> InstanceData;
TestGuest::Array<std::uint32_t, Draws.size() * LinkWords> AttributeTables;
TestGuest::Array<std::uint32_t, Draws.size() * OutputWords> Outputs;
TestGuest::Array<std::uint16_t, IndexCapacity> Indices16;
TestGuest::Array<std::uint32_t, IndexCapacity> Indices32;
TestGuest::Array<std::uint32_t, 64 * 4> Pixels;

Header VertexHeader;
Header PixelHeader;

std::uint32_t Byte(std::uint32_t draw, std::uint32_t vertex, std::uint32_t component) {
    return (0x10u + 7u * draw + 13u * vertex + 59u * component) & 0xffu;
}

float Wide(std::uint32_t draw, std::uint32_t vertex, std::uint32_t component) {
    return 0.5f * static_cast<float>(4u * vertex + component) + 100.0f * static_cast<float>(draw) - 37.0f;
}

std::int32_t Snorm(std::uint32_t draw, std::uint32_t vertex, std::uint32_t component) {
    return static_cast<std::int32_t>((1031u * vertex + 7919u * component + 313u * draw) % 65536u) - 32768;
}

std::uint32_t Packed(std::uint32_t draw, std::uint32_t vertex, std::uint32_t component) {
    if (component == 3u) return (vertex + draw) & 3u;
    return (97u * vertex + 331u * component + 41u * draw) % 1024u;
}

std::uint16_t Half(std::uint32_t draw, std::uint32_t vertex, std::uint32_t component) {
    return static_cast<std::uint16_t>((((vertex + component) & 1u) << 15u) | 0x3c00u | ((7u * vertex + 19u * component + 3u * draw) & 0x3ffu));
}

float HalfValue(std::uint16_t half) {
    const auto magnitude = 1.0f + static_cast<float>(half & 0x3ffu) / 1024.0f;
    return (half & 0x8000u) != 0u ? -magnitude : magnitude;
}

float InstanceValue(std::uint32_t element, std::uint32_t component) {
    return -static_cast<float>(8u * element + component) - 0.25f;
}

float ComputedValue(std::uint32_t vertex, std::uint32_t component, std::uint32_t seed) {
    const std::array<std::uint32_t, 4> values{vertex, vertex + seed, seed, 2u * vertex};
    return static_cast<float>(values[component]);
}

std::byte* Region(std::uint32_t draw) {
    return VertexMemory.data() + draw * RegionBytes;
}

void FillElements(std::uint32_t draw) {
    const auto& pulled = Draws[draw];
    auto* base = Region(draw);
    std::fill(base, base + RegionBytes, std::byte{0xcd});
    for (std::uint32_t vertex = 0; vertex < Elements; ++vertex) {
        auto* element = base + pulled.offset + vertex * pulled.stride;
        if (pulled.format == Format32_32_32_32Float) {
            for (std::uint32_t component = 0; component < 4u; ++component) {
                const auto value = Wide(draw, vertex, component);
                std::memcpy(element + component * 4u, &value, sizeof(value));
            }
        } else if (pulled.format == Format8_8_8_8UNorm) {
            for (std::uint32_t component = 0; component < 4u; ++component) element[component] = static_cast<std::byte>(Byte(draw, vertex, component));
        } else if (pulled.format == Format16_16SNorm) {
            for (std::uint32_t component = 0; component < 2u; ++component) {
                const auto value = static_cast<std::int16_t>(Snorm(draw, vertex, component));
                std::memcpy(element + component * 2u, &value, sizeof(value));
            }
        } else if (pulled.format == Format10_10_10_2UNorm) {
            const auto word = Packed(draw, vertex, 0) | (Packed(draw, vertex, 1) << 10u) | (Packed(draw, vertex, 2) << 20u) | (Packed(draw, vertex, 3) << 30u);
            std::memcpy(element, &word, sizeof(word));
        } else {
            for (std::uint32_t component = 0; component < 2u; ++component) {
                const auto value = Half(draw, vertex, component);
                std::memcpy(element + component * 2u, &value, sizeof(value));
            }
        }
    }
}

bool SameElement(std::uint32_t draw, std::uint32_t vertex, std::uint32_t component, std::uint32_t word, std::uint32_t seed) {
    const auto& pulled = Draws[draw];
    if (pulled.computed) return DeviceTable::SameFloat(word, ComputedValue(vertex, component, seed));
    switch (pulled.format) {
    case Format32_32_32_32Float: return DeviceTable::SameFloat(word, Wide(draw, vertex, component));
    case Format8_8_8_8UNorm: return SameScaled(word, static_cast<std::int32_t>(Byte(draw, vertex, component)), 255.0f);
    case Format16_16SNorm: {
        const auto value = Snorm(draw, vertex, component);
        return value == -32768 ? DeviceTable::SameFloat(word, -1.0f) : SameScaled(word, value, 32767.0f);
    }
    case Format10_10_10_2UNorm: return SameScaled(word, static_cast<std::int32_t>(Packed(draw, vertex, component)), component == 3u ? 3.0f : 1023.0f);
    default: return DeviceTable::SameFloat(word, HalfValue(Half(draw, vertex, component)));
    }
}

bool SameAttribute(std::uint32_t draw, std::uint32_t vertex, std::uint32_t component, std::uint32_t word, std::uint32_t seed) {
    const auto& pulled = Draws[draw];
    const auto selector = (pulled.selectors >> (3u * component)) & 7u;
    if (selector == 0u) return word == 0u;
    if (selector == 1u) return DeviceTable::SameFloat(word, 1.0f);
    if (vertex >= pulled.records) return word == 0u;
    return SameElement(draw, vertex, selector - 4u, word, seed);
}

bool SameInstance(std::uint32_t draw, std::uint32_t instance, std::uint32_t component, std::uint32_t word) {
    if (instance >= Draws[draw].instanceRecords) return word == 0u;
    return DeviceTable::SameFloat(word, InstanceValue(draw + instance, component));
}

std::array<std::uint32_t, 4> Formatted(const void* base, std::uint32_t stride, std::uint32_t records, std::uint32_t format, std::uint32_t selectors) {
    const auto address = AddressOf(base);
    return {Low(address), (High(address) & 0xffffu) | (stride << 16u), records, 0x01000000u | (format << 12u) | selectors};
}

void FillTable(std::uint32_t draw) {
    const auto& pulled = Draws[draw];
    auto* table = AttributeTables.data() + draw * LinkWords;
    std::fill(table, table + LinkWords, 0u);
    const void* base = pulled.computed ? static_cast<const void*>(Computed.data()) : static_cast<const void*>(Region(draw) + pulled.offset);
    const auto vertex = Formatted(base, pulled.stride, pulled.records, pulled.format, pulled.selectors);
    const auto instance = Formatted(InstanceData.data() + draw, 16u, pulled.instanceRecords, Format32_32_32_32Float, AllComponents);
    std::copy(vertex.begin(), vertex.end(), table);
    std::copy(instance.begin(), instance.end(), table + 4);
}

void FillIndices() {
    Indices16.fill(60u);
    Indices32.fill(61u);
    for (std::uint32_t index = 0; index < Index16Offset; ++index) Indices16[index] = static_cast<std::uint16_t>(50u + index);
    for (std::uint32_t index = 0; index < Draws[6].vertices; ++index) Indices16[Index16Offset + index] = static_cast<std::uint16_t>((7u * index + 3u) % 40u);
    for (std::uint32_t index = 0; index < Index32Offset; ++index) Indices32[index] = 49u + index;
    for (std::uint32_t index = 0; index < Draws[7].vertices; ++index) Indices32[Index32Offset + index] = (11u * index + 5u) % 48u;
}

std::vector<std::uint32_t> Referenced(std::uint32_t draw) {
    const auto& pulled = Draws[draw];
    std::vector<std::uint32_t> vertices;
    for (std::uint32_t index = 0; index < pulled.vertices; ++index) {
        if (pulled.kind == Kind::Indexed16) vertices.push_back(Indices16[Index16Offset + index]);
        else if (pulled.kind == Kind::Indexed32) vertices.push_back(Indices32[Index32Offset + index]);
        else vertices.push_back(index);
    }
    return vertices;
}

const Program& PullingVertex() {
    static const Program program = [] {
        using namespace DrawSubmit::Isa;
        Program built;
        built << Smem(SLoadDwordX4, 20, 8, 0)
              << Smem(SLoadDwordX4, 24, 8, 16)
              << WaitAll
              << Indexed(BufferLoadFormatXyzw, 10, 5, 20)
              << Indexed(BufferLoadFormatXyzw, 14, 8, 24)
              << Vop2(VLshlRevB32, 20, InlineInteger(6), 8)
              << Vop2(VAddNcU32, 20, Vgpr(5), 20)
              << Vop2(VLshlRevB32, 20, InlineInteger(5), 20)
              << WaitAll
              << Mubuf(DeviceTable::Asm::BufferStoreDwordX4, 10, 20, 12)
              << Mubuf(DeviceTable::Asm::BufferStoreDwordX4, 14, 20, 12, false, 16)
              << Vop1(VMovB32, 21, InlineInteger(0))
              << Vop1(VMovB32, 22, InlineInteger(0))
              << Vop1(VMovB32, 23, InlineHalf)
              << Vop1(VMovB32, 24, InlineOne)
              << WaitAll
              << Export(PositionTarget, 21, false)
              << EndProgram;
        return built;
    }();
    return program;
}

const Program& FlatPixel() {
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

const Program& VertexWriterProgram() {
    static const Program program = [] {
        using namespace DrawSubmit::Isa;
        Program built;
        built << Vop1(VCvtF32U32, 1, Vgpr(0))
              << Vop2(VAddNcU32, 2, 4, 0)
              << Vop1(VCvtF32U32, 2, Vgpr(2))
              << Vop1(VCvtF32U32, 3, 4)
              << Vop2(VLshlRevB32, 4, InlineInteger(1), 0)
              << Vop1(VCvtF32U32, 4, Vgpr(4))
              << Indexed(DeviceTable::Asm::BufferStoreDwordX4, 1, 0, 0)
              << EndProgram;
        return built;
    }();
    return program;
}

std::array<std::uint32_t, 5> WriterUserData(std::uint32_t seed) {
    const auto address = AddressOf(Computed.data());
    return {Low(address), (High(address) & 0xffffu) | (16u << 16u), ComputedVertices, 0x01016facu, seed};
}

std::array<std::uint32_t, VertexUserWords> PullingUserData(std::uint32_t draw) {
    const auto table = AddressOf(AttributeTables.data() + draw * LinkWords);
    const auto output = DeviceTable::Buffer(Outputs.data() + draw * OutputWords, OutputWords * 4u);
    return {Low(table), High(table), 0u, 0u, output[0], output[1], output[2], output[3]};
}

void Record(Commands& commands, std::uint32_t draw) {
    const auto& pulled = Draws[draw];
    VertexUserData(commands, PullingUserData(draw));
    Instances(commands, pulled.instances);
    if (pulled.kind == Kind::Indexed16) {
        IndexType(commands, Index16);
        DrawIndex(commands, AddressOf(Indices16.data() + Index16Offset), pulled.vertices, IndexCapacity - Index16Offset);
    } else if (pulled.kind == Kind::Indexed32) {
        IndexType(commands, Index32);
        IndexBase(commands, AddressOf(Indices32.data()));
        IndexBufferSize(commands, IndexCapacity);
        DrawIndexOffset(commands, Index32Offset, pulled.vertices, IndexCapacity);
    } else {
        DrawAuto(commands, pulled.vertices);
    }
}

void Submit(std::uint32_t seed, bool reversed) {
    Outputs.fill(DeviceTable::Untouched);
    for (auto& vertex : Computed) vertex = {-7.0f, -7.0f, -7.0f, -7.0f};
    Commands commands;
    commands.Dispatch(VertexWriterProgram(), WriterUserData(seed), DeviceTable::Lanes, 1);
    commands.Barrier();
    Baseline(commands, {Pixels.data(), 64u, 4u}, CenterAndPosition);
    Programs(commands, PullingVertex(), VertexUserWords, FlatPixel(), 0u);
    for (std::uint32_t step = 0; step < Draws.size(); ++step) Record(commands, reversed ? static_cast<std::uint32_t>(Draws.size()) - 1u - step : step);
    commands.Submit(0);
    NewPath::WaitIdle();
}

void ExpectDraws(std::uint32_t seed, const std::string& what) {
    const auto outputs = ReadBack(Outputs.data(), Outputs.size());
    for (std::uint32_t draw = 0; draw < Draws.size(); ++draw) {
        const auto& pulled = Draws[draw];
        const auto* words = outputs.data() + draw * OutputWords;
        std::vector<bool> expected(MaxInstances * SlotsPerInstance, false);
        std::size_t wrong = 0;
        std::string first;
        const auto mismatch = [&](std::uint32_t slot, std::uint32_t word, const std::string& where) {
            if (wrong++ == 0u) first = "slot " + std::to_string(slot) + " word " + std::to_string(word) + " is " + Hex(words[slot * SlotWords + word]) + " (" + where + ")";
        };
        for (std::uint32_t instance = 0; instance < pulled.instances; ++instance) {
            for (const auto vertex : Referenced(draw)) {
                const auto slot = instance * SlotsPerInstance + vertex;
                expected[slot] = true;
                for (std::uint32_t component = 0; component < 4u; ++component) {
                    if (!SameAttribute(draw, vertex, component, words[slot * SlotWords + component], seed)) mismatch(slot, component, "vertex " + std::to_string(vertex) + " instance " + std::to_string(instance) + ", vertex-rate attribute");
                    if (!SameInstance(draw, instance, component, words[slot * SlotWords + 4u + component])) mismatch(slot, 4u + component, "vertex " + std::to_string(vertex) + " instance " + std::to_string(instance) + ", instance-rate attribute");
                }
            }
        }
        for (std::uint32_t slot = 0; slot < expected.size(); ++slot) {
            if (expected[slot]) continue;
            for (std::uint32_t word = 0; word < SlotWords; ++word) {
                if (words[slot * SlotWords + word] != DeviceTable::Untouched) mismatch(slot, word, "a vertex the draw does not reference");
            }
        }
        Expect(wrong == 0u, what + ": draw " + std::to_string(draw) + ", " + pulled.what + ": " + std::to_string(wrong) + " words differ, first " + first);
    }
}

void PulledDraws() {
    const auto before = StageCompiles();
    Submit(FirstSeed, false);
    ExpectDraws(FirstSeed, "vertex pulling: one vertex program, nine draws in one submission, each with its own attribute table, V# words and output");
    ExpectStageCompiles(before, 2u, "vertex pulling: nine draws of one vertex and pixel program pair");
}

void ReversedDraws() {
    const auto before = StageCompiles();
    Submit(SecondSeed, true);
    ExpectDraws(SecondSeed, "vertex pulling, reversed: the next submission draws the same tables in reverse order after the compute dispatch rewrote its vertex buffer");
    ExpectStageCompiles(before, 0u, "vertex pulling, reversed: the same program pair and state");
}

void Run() {
    for (std::uint32_t draw = 0; draw < Draws.size(); ++draw) {
        FillElements(draw);
        FillTable(draw);
    }
    for (std::uint32_t element = 0; element < InstanceData.size(); ++element) {
        for (std::uint32_t component = 0; component < 4u; ++component) InstanceData[element][component] = InstanceValue(element, component);
    }
    FillIndices();
    Register(VertexHeader, PullingVertex(), VertexShaderType);
    Register(PixelHeader, FlatPixel(), PixelShaderType);
    Cases({
        {"pulled draws", PulledDraws},
        {"reversed draws", ReversedDraws},
    });
}

}

int main() {
    return DrawSubmit::Main("vertex pulling", Run);
}
