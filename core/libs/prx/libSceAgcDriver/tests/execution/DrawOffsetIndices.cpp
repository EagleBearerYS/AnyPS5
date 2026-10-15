#include "DrawSubmitFixture.hpp"
#include <iostream>

namespace {

using namespace DrawSubmit;

constexpr std::uint32_t BaseVertexSgpr = 16;
constexpr std::uint32_t StartInstanceSgpr = 17;
constexpr std::uint32_t Vertices = 3;
constexpr std::uint32_t Instances = 2;
constexpr std::uint32_t Slots = Vertices * Instances;
constexpr std::uint32_t Unwritten = 0xdeadbeefu;
constexpr std::uint32_t VertexUserWords = 10;
constexpr std::uint32_t StaleBaseVertex = 0x51u;
constexpr std::uint32_t StaleStartInstance = 0x62u;
constexpr std::uint32_t Width = 64;
constexpr std::uint32_t Height = 32;

TestGuest::Array<std::uint32_t, Width * Height> Pixels;
TestGuest::Array<std::uint32_t, 4 * Slots> Output;
TestGuest::Array<std::uint32_t, 2048> VertexSlots;
TestGuest::Array<std::uint32_t, 60000> InstanceSlots;
TestGuest::Array<std::uint32_t, 8> BufferTable;
TestGuest::Array<std::uint16_t, 4> Indices;
TestGuest::Array<std::uint32_t, 16> Records;

Header VertexHeader;
Header PixelHeader;

const Program& FetchAddVertex() {
    static const Program program = [] {
        using namespace DrawSubmit::Isa;
        Program built;
        built << Vop1(VMovB32, 1, Vgpr(5))
              << Vop1(VMovB32, 2, Vgpr(8))
              << Vop2(VAddNcU32, 5, BaseVertexSgpr, 5)
              << Vop2(VAddNcU32, 8, StartInstanceSgpr, 8)
              << Smem(SLoadDwordX4, 20, 12, 0)
              << Smem(SLoadDwordX4, 24, 12, 16)
              << WaitAll
              << Indexed(BufferLoadFormatX, 10, 5, 20)
              << Indexed(BufferLoadFormatX, 11, 8, 24)
              << WaitAll
              << Vop2(VAddNcU32, 4, Vgpr(10), 11)
              << Vop2(VLshlRevB32, 4, InlineInteger(4), 4)
              << Vop1(VMovB32, 12, Vgpr(1))
              << Vop1(VMovB32, 13, Vgpr(2))
              << Vop1(VMovB32, 14, Vgpr(5))
              << Vop1(VMovB32, 15, Vgpr(8))
              << Mubuf(BufferStoreDwordX4, 12, 4, 8)
              << Vop1(VMovB32, 16, InlineInteger(0))
              << Vop1(VMovB32, 17, InlineInteger(0))
              << Vop1(VMovB32, 18, InlineInteger(0))
              << Vop1(VMovB32, 19, InlineOne)
              << WaitAll
              << Export(PositionTarget, 16, false)
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

std::array<std::uint32_t, 4> SlotDescriptor(const void* data, std::uint32_t records) {
    const auto address = AddressOf(data);
    return {Low(address), (High(address) & 0xffffu) | (4u << 16u), records, 0x01014facu};
}

std::array<std::uint32_t, VertexUserWords> UserData(std::uint32_t baseVertex, std::uint32_t startInstance) {
    std::array<std::uint32_t, VertexUserWords> userData{};
    const auto output = DeviceTable::Buffer(Output.data(), static_cast<std::uint32_t>(decltype(Output)::ByteSize));
    std::copy(output.begin(), output.end(), userData.begin());
    const auto table = AddressOf(BufferTable.data());
    userData[4] = Low(table);
    userData[5] = High(table);
    userData[BaseVertexSgpr - FirstVertexUserSgpr] = baseVertex;
    userData[StartInstanceSgpr - FirstVertexUserSgpr] = startInstance;
    return userData;
}

void Place(std::uint32_t firstVertexFetch, std::span<const std::uint32_t> fetchedVertices, std::uint32_t firstInstanceFetch) {
    VertexSlots.fill(Unwritten);
    InstanceSlots.fill(Unwritten);
    for (std::uint32_t vertex = 0; vertex < fetchedVertices.size(); ++vertex) VertexSlots[firstVertexFetch + fetchedVertices[vertex]] = vertex;
    for (std::uint32_t instance = 0; instance < Instances; ++instance) InstanceSlots[firstInstanceFetch + instance] = instance * Vertices;
}

void Begin(Commands& commands, std::uint32_t baseVertex, std::uint32_t startInstance) {
    Output.fill(Unwritten);
    Baseline(commands, {Pixels.data(), Width, Height}, CenterAndPosition);
    Programs(commands, FetchAddVertex(), VertexUserWords, FlatPixel(), 0u);
    VertexUserData(commands, UserData(baseVertex, startInstance));
}

void Finish(Commands& commands) {
    commands.Submit(0);
    NewPath::WaitIdle();
}

std::string Check(const std::string& what, std::span<const std::uint32_t> rawVertices, std::uint32_t baseVertex, std::uint32_t startInstance) {
    const auto output = ReadBack(Output.data(), Output.size());
    std::string failures;
    for (std::uint32_t instance = 0; instance < Instances; ++instance) {
        for (std::uint32_t vertex = 0; vertex < Vertices; ++vertex) {
            const auto slot = instance * Vertices + vertex;
            const auto* actual = output.data() + slot * 4u;
            const std::array<std::uint32_t, 4> expected{rawVertices[vertex], instance, rawVertices[vertex] + baseVertex, instance + startInstance};
            const bool same = std::equal(expected.begin(), expected.end(), actual);
            char line[320];
            std::snprintf(line, sizeof(line), "%s, vertex %u instance %u: v5 %u v8 %u, after the fetch adds %u %u; expected %u %u, %u %u\n", what.c_str(), vertex, instance, actual[0], actual[1], actual[2], actual[3], expected[0], expected[1], expected[2], expected[3]);
            std::cout << line;
            if (!same) failures += line;
        }
    }
    return failures;
}

std::string AutoDraw() {
    constexpr std::uint32_t indexOffset = 5;
    constexpr std::uint32_t baseVertex = 1000;
    constexpr std::uint32_t startInstance = 40;
    const std::array<std::uint32_t, Vertices> autoIndices{0, 1, 2};
    Place(indexOffset + baseVertex, autoIndices, startInstance);
    Commands commands;
    Begin(commands, baseVertex, startInstance);
    IndexOffset(commands, indexOffset);
    DrawSubmit::Instances(commands, Instances);
    DrawAuto(commands, Vertices);
    IndexOffset(commands, 0u);
    Finish(commands);
    const std::array<std::uint32_t, Vertices> raw{indexOffset, indexOffset + 1, indexOffset + 2};
    return Check("direct auto draw, GE_INDX_OFFSET 5, s16 1000, s17 40", raw, baseVertex, startInstance);
}

std::string IndexedDraw() {
    constexpr std::uint32_t baseVertex = 1000;
    constexpr std::uint32_t startInstance = 40;
    const std::array<std::uint32_t, Vertices> bufferIndices{Indices[0], Indices[1], Indices[2]};
    Place(baseVertex, bufferIndices, startInstance);
    Commands commands;
    Begin(commands, baseVertex, startInstance);
    DrawSubmit::Instances(commands, Instances);
    IndexType(commands, Index16);
    DrawIndex(commands, AddressOf(Indices.data()), Vertices, static_cast<std::uint32_t>(Indices.size()));
    Finish(commands);
    return Check("direct indexed draw, s16 1000, s17 40", bufferIndices, baseVertex, startInstance);
}

std::string IndexedIndirect(const std::string& what, bool locations) {
    const std::array<std::uint32_t, 5> record{Vertices, Instances, 0, 700, 59455};
    std::copy(record.begin(), record.end(), Records.begin());
    const std::array<std::uint32_t, Vertices> bufferIndices{Indices[0], Indices[1], Indices[2]};
    const auto baseVertex = locations ? record[3] : StaleBaseVertex;
    const auto startInstance = locations ? record[4] : StaleStartInstance;
    Place(baseVertex, bufferIndices, startInstance);
    Commands commands;
    Begin(commands, StaleBaseVertex, StaleStartInstance);
    IndexType(commands, Index16);
    IndexBase(commands, AddressOf(Indices.data()));
    IndexBufferSize(commands, static_cast<std::uint32_t>(Indices.size()));
    IndirectBase(commands, AddressOf(Records.data()));
    DrawIndexIndirect(commands, 0u, locations ? VertexUserDataLocation(BaseVertexSgpr) : NoLocation, locations ? VertexUserDataLocation(StartInstanceSgpr) : NoLocation);
    Finish(commands);
    return Check(what, bufferIndices, baseVertex, startInstance);
}

std::string AutoIndirect() {
    constexpr std::uint32_t recordOffset = 32;
    const std::array<std::uint32_t, 4> record{Vertices, Instances, 300, 7};
    std::copy(record.begin(), record.end(), Records.begin() + recordOffset / 4u);
    const std::array<std::uint32_t, Vertices> autoIndices{0, 1, 2};
    Place(record[2], autoIndices, record[3]);
    Commands commands;
    Begin(commands, StaleBaseVertex, StaleStartInstance);
    IndirectBase(commands, AddressOf(Records.data()));
    DrawIndirect(commands, recordOffset, VertexUserDataLocation(BaseVertexSgpr), VertexUserDataLocation(StartInstanceSgpr));
    Finish(commands);
    return Check("auto indirect record, start vertex 300 and start instance 7 into s16 and s17", autoIndices, record[2], record[3]);
}

void Run() {
    Indices = {2, 0, 1, 0};
    const auto vertex = SlotDescriptor(VertexSlots.data(), static_cast<std::uint32_t>(VertexSlots.size()));
    const auto instance = SlotDescriptor(InstanceSlots.data(), static_cast<std::uint32_t>(InstanceSlots.size()));
    std::copy(vertex.begin(), vertex.end(), BufferTable.begin());
    std::copy(instance.begin(), instance.end(), BufferTable.begin() + 4);
    Register(VertexHeader, FetchAddVertex(), VertexShaderType);
    Register(PixelHeader, FlatPixel(), PixelShaderType);
    std::string failures;
    failures += AutoDraw();
    failures += IndexedDraw();
    failures += IndexedIndirect("indexed indirect record, vertex offset 700 and start instance 59455 into s16 and s17", true);
    failures += AutoIndirect();
    failures += IndexedIndirect("indexed indirect record whose packet names no register for its offsets, user data s16 0x51, s17 0x62", false);
    Expect(failures.empty(), "vertex and instance indices differ from the guest's:\n" + failures);
}

}

int main() {
    return DrawSubmit::Main("draw offset index", Run);
}
