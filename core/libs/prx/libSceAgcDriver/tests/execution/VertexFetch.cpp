#include "DrawSubmitFixture.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"

namespace {

using namespace DrawSubmit;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Extent = 64u;
constexpr std::uint32_t VertexUserWords = 4;
constexpr std::uint32_t Records = 8;
constexpr std::uint32_t FirstVertex = 1;
constexpr std::uint32_t StartInstance = 1;
constexpr std::uint32_t Format16_16_16_16Float = 71;
constexpr std::uint32_t Format32_32_32_32Float = 77;
constexpr std::uint32_t Red = 0xff0000ffu;

TestGuest::Array<std::uint32_t, Extent * Extent> Pixels;
TestGuest::Array<std::byte, 256> Vertices;
TestGuest::Array<std::byte, 256> MovedVertices;
TestGuest::Array<std::uint32_t, 4> Table;
constexpr std::array<std::array<float, 4>, 3> Corners{{{-1.0f, -1.0f, 0.0f, 1.0f}, {3.0f, -1.0f, 0.0f, 1.0f}, {-1.0f, 3.0f, 0.0f, 1.0f}}};

Header VertexHeader;
Header PixelHeader;

const Program& OffsetFetchVertex() {
    static const Program program = [] {
        using namespace DrawSubmit::Isa;
        Program built;
        built << Smem(SLoadDwordX4, 16, 8, 0)
              << Vop2(VAddNcU32, 5, 10, 5)
              << Vop2(VAddNcU32, 5, Vgpr(8), 5)
              << Vop2(VAddNcU32, 5, 11, 5)
              << WaitAll
              << Indexed(BufferLoadFormatXyzw, 0, 5, 16)
              << WaitAll
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

void RejectIncompatibleAbi() {
    const auto device = OpenVulkanTestDevice();
    if (!device) return;
    ShaderRecompiler::RecompileResult incompatible;
    incompatible.runtimeAbiVersion = ShaderRecompiler::RuntimeAbi::Version + 1u;
    const auto rejectAbi = [](const char* what, auto action) {
        try {
            action();
        } catch (const std::runtime_error& error) {
            Expect(std::string(error.what()).find("incompatible version") != std::string::npos, std::string(what) + ": unexpected runtime ABI validation error: " + error.what());
            return;
        }
        Expect(false, std::string(what) + ": the device accepted an incompatible shader runtime ABI");
    };
    rejectAbi("PrepareDispatch", [&] { device->PrepareDispatch(incompatible); });
    rejectAbi("Dispatch", [&] { device->Dispatch(incompatible, 1u, 1u, 1u); });
    rejectAbi("DispatchIndirect", [&] { device->DispatchIndirect(incompatible, 0u); });
    const std::array<AgcDriver::Graphics::CompiledShader, 1> incompatibleShaders{{{ShaderStage::Vertex, &incompatible, 0u}}};
    rejectAbi("Draw", [&] { device->Draw({}, {}, incompatibleShaders); });
}

struct Case {
    std::uint32_t stride;
    std::uint32_t format;
    bool moved;
    std::uint32_t offset;
};

std::uint16_t Half(float value) {
    return value == -1.0f ? 0xbc00u : value == 3.0f ? 0x4200u : value == 1.0f ? 0x3c00u : 0u;
}

void Draw(const Case& value) {
    auto& storage = value.moved ? MovedVertices : Vertices;
    storage.fill(std::byte{0});
    const auto firstRecord = FirstVertex + value.offset + StartInstance;
    for (std::size_t index = 0; index < Corners.size(); ++index) {
        auto* destination = storage.data() + (firstRecord + index) * value.stride;
        if (value.format == Format32_32_32_32Float) {
            std::memcpy(destination, Corners[index].data(), 16u);
        } else {
            for (std::size_t component = 0; component < 4u; ++component) {
                const auto half = Half(Corners[index][component]);
                std::memcpy(destination + component * 2u, &half, sizeof(half));
            }
        }
    }
    const auto address = AddressOf(storage.data());
    Table = {Low(address), (High(address) & 0xffffu) | (value.stride << 16u), Records, 0x01000facu | (value.format << 12u)};
    const auto table = AddressOf(Table.data());
    const std::array<std::uint32_t, VertexUserWords> userData{Low(table), High(table), value.offset, StartInstance};
    Pixels.fill(0u);
    Commands commands;
    Baseline(commands, {Pixels.data(), Extent, Extent}, CenterAndPosition);
    IndexOffset(commands, FirstVertex);
    Programs(commands, OffsetFetchVertex(), VertexUserWords, RedPixel(), 0u);
    Instances(commands, 1u);
    VertexUserData(commands, userData);
    DrawAuto(commands, static_cast<std::uint32_t>(Corners.size()));
    commands.Submit(0);
    NewPath::WaitIdle();
}

void Check(const Case& value, const std::string& what) {
    const auto pixels = ReadBack(Pixels.data(), Pixels.size());
    const auto wrong = static_cast<std::size_t>(std::ranges::count_if(pixels, [](std::uint32_t pixel) { return pixel != Red; }));
    Expect(wrong == 0u, what + " (stride " + std::to_string(value.stride) + ", format " + std::to_string(value.format) + ", offset " + std::to_string(value.offset) + (value.moved ? ", the other buffer" : "") + "): " + std::to_string(wrong) + " of " + std::to_string(pixels.size()) + " pixels are not the triangle's red");
}

void Run() {
    RejectIncompatibleAbi();
    Register(VertexHeader, OffsetFetchVertex(), VertexShaderType);
    Register(PixelHeader, RedPixel(), PixelShaderType);
    constexpr std::array<Case, 5> cases{{
        {16u, Format32_32_32_32Float, false, 1u},
        {32u, Format32_32_32_32Float, true, 0u},
        {8u, Format16_16_16_16Float, false, 1u},
        {16u, Format16_16_16_16Float, true, 0u},
        {16u, Format32_32_32_32Float, false, 1u},
    }};
    const auto before = StageCompiles();
    for (std::size_t index = 0; index < cases.size(); ++index) {
        Draw(cases[index]);
        Check(cases[index], "runtime vertex fetch, draw " + std::to_string(index) + ": the triangle from records past GE_INDX_OFFSET, the user data offset and start instance");
    }
    ExpectStageCompiles(before, 2u, "runtime vertex fetch: five V# formats, strides and buffers drawn with one program pair");
}

}

int main() {
    return DrawSubmit::Main("runtime vertex fetch", Run);
}
