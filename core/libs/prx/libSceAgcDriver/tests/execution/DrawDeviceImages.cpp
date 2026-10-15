#include "DeviceTableFixture.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"

namespace {

using namespace DeviceTable;
using ShaderRecompiler::PixelInput;
using ShaderRecompiler::PixelInputBit;

constexpr std::uint32_t Texels = Width * Height;
constexpr std::uint32_t HalfWidth = Width / 2u;
constexpr std::uint32_t VertexTexelX = 5;
constexpr std::uint32_t VertexTexelY = 3;
constexpr std::uint32_t FlatInterpolation = 0x400u;
constexpr std::uint32_t PerspectiveCenterInput = 2u;

using Texture = TestGuest::Array<std::uint32_t, Texels>;

Texture First;
Texture Second;
Texture Third;
Texture Fourth;
TestGuest::Array<std::uint32_t, Texels> Pixels;
const TestGuest::Array<std::array<float, 4>, 3> Triangle{{-1.0f, -1.0f, 0.5f, 1.0f}, {3.0f, -1.0f, 0.5f, 1.0f}, {-1.0f, 3.0f, 0.5f, 1.0f}};

std::uint32_t Texel(std::uint32_t seed, std::uint32_t x, std::uint32_t y) {
    return ((seed * 37u + 3u * x) & 0xffu) | (((seed * 11u + 50u * y) & 0xffu) << 8u) | (((255u - x - seed) & 0xffu) << 16u) | (((0x20u + x + 7u * y + seed) & 0xffu) << 24u);
}

void Fill(Texture& texture, std::uint32_t seed) {
    for (std::uint32_t y = 0; y < Height; ++y) {
        for (std::uint32_t x = 0; x < Width; ++x) texture[y * Width + x] = Texel(seed, x, y);
    }
}

std::array<std::uint32_t, 4> VertexBuffer() {
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(Triangle.data()));
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (16u << 16u), static_cast<std::uint32_t>(Triangle.size()), 0x01016facu};
}

const Program& PositionVertex() {
    static const Program program = [] {
        using namespace Asm;
        Program built;
        built << 0xe0382000u << 0x80000005u << WaitAll << 0xf80008cfu << 0x03020100u << EndProgram;
        return built;
    }();
    return program;
}

const Program& TexelVertex() {
    static const Program program = [] {
        using namespace Asm;
        Program built;
        built << 0xe0382000u << 0x80000005u
              << Vop2(VAddNcU32, 10, InlineInteger(VertexTexelX), 5)
              << Vop1(VMovB32, 11, InlineInteger(VertexTexelY))
              << Mimg(ImageLoad, 0xfu, true, 6, 10, 4)
              << WaitAll
              << 0xf800020fu << 0x09080706u
              << 0xf80008cfu << 0x03020100u
              << EndProgram;
        return built;
    }();
    return program;
}

const Program& SamplePixel() {
    static const Program program = [] {
        using namespace Asm;
        Program built;
        built << Vop2(VMulF32, 0, Literal, 0) << OneOverWidth
              << Vop2(VMulF32, 1, Literal, 1) << OneOverHeight
              << Mimg(ImageSampleLz, 0xfu, true, 2, 0, 0, 4)
              << WaitAll
              << 0xf800180fu << 0x05040302u
              << EndProgram;
        return built;
    }();
    return program;
}

const Program& FlatPixel() {
    static const Program program = [] {
        using namespace Asm;
        Program built;
        built << 0xc8120002u << 0xc8160102u << 0xc81a0202u << 0xc81e0302u << 0xf800180fu << 0x07060504u << EndProgram;
        return built;
    }();
    return program;
}

using Stage = Compiled;

Stage Captured(const ShaderRecompiler::RecompileRequest& request) {
    return CompileRequest(request);
}

struct Shaders {
    Stage vertex;
    Stage pixel;
    std::uint32_t pixelPush = 0;
};

Shaders Compile(AgcDriver::VulkanDevice& device, const Program& vertexCode, std::span<const std::uint32_t> vertexUserData, const Program& pixelCode, std::span<const std::uint32_t> pixelUserData, bool flat) {
    const auto target = device.Target();
    Shaders shaders;
    shaders.vertex = Captured({
        {ShaderRecompiler::ShaderStage::Vertex, vertexCode.Address(), vertexCode.Code(), 0, {}},
        {64, 0, vertexUserData, std::nullopt, std::nullopt, ShaderRecompiler::ShaderVertexStageInfo{}, {}},
        target,
        {0, 0, 0, 64}
    });
    const auto vertexPush = static_cast<std::uint32_t>(shaders.vertex.result->pushConstants.size());
    ShaderRecompiler::ShaderPixelStageInfo pixel{};
    if (flat) {
        pixel.interpolatorCount = 1;
        pixel.interpolatorSettings[0] = FlatInterpolation;
        pixel.inputAddr = PerspectiveCenterInput;
        pixel.hasPerspectiveCenterVgpr = true;
    } else {
        pixel.inputAddr = PixelInputBit(PixelInput::PositionX) | PixelInputBit(PixelInput::PositionY);
        pixel.posX = true;
        pixel.posY = true;
    }
    pixel.targetOutputMode[0] = 9;
    pixel.targetExportMapping.fill(0xe4u);
    shaders.pixel = Captured({
        {ShaderRecompiler::ShaderStage::Fragment, pixelCode.Address(), pixelCode.Code(), 0, {}},
        {64, 0, pixelUserData, std::nullopt, pixel, std::nullopt, {}},
        target,
        PixelPushLayout(vertexPush, target)
    });
    shaders.pixelPush = PixelPushOffset(vertexPush, target);
    return shaders;
}

void Draw(AgcDriver::VulkanDevice& device, const Shaders& shaders, VkRect2D scissor) {
    AgcDriver::Graphics::State state{};
    state.stages = {AgcDriver::Graphics::ShaderPath::Vertex, 0u, 64u, 64u, std::nullopt, std::nullopt};
    state.color = {reinterpret_cast<std::uintptr_t>(Pixels.data()), {Width, Height}, VK_FORMAT_R8G8B8A8_UNORM, decltype(Pixels)::ByteSize, 0xe4u};
    state.colors = {state.color};
    state.hasColorTarget = true;
    state.renderExtent = {Width, Height};
    state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    state.viewport = {0, static_cast<float>(Height), static_cast<float>(Width), -static_cast<float>(Height), 0, 1};
    state.negativeOneToOne = false;
    state.scissor = scissor;
    state.cullMode = VK_CULL_MODE_NONE;
    state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    state.blend.colorWriteMask = 15;
    state.blends = {state.blend};
    state.blendConstants = {};
    const std::array<AgcDriver::Graphics::CompiledShader, 2> stages{{
        {ShaderRecompiler::ShaderStage::Vertex, shaders.vertex.result.get(), 0},
        {ShaderRecompiler::ShaderStage::Fragment, shaders.pixel.result.get(), shaders.pixelPush}
    }};
    const AgcDriver::Pm4::DrawParameters draw{0, static_cast<std::uint32_t>(Triangle.size()), 0, 1, 0, false};
    device.Draw(state, draw, stages);
}

constexpr VkRect2D Whole{{0, 0}, {Width, Height}};
constexpr VkRect2D Left{{0, 0}, {HalfWidth, Height}};
constexpr VkRect2D Right{{static_cast<std::int32_t>(HalfWidth), 0}, {HalfWidth, Height}};

struct Frame {
    std::vector<std::uint32_t> pixels;
    std::string log;
    AgcDriver::Graphics::DeviceImageTable::Counts before;
    AgcDriver::Graphics::DeviceImageTable::Counts after;

    std::uint64_t Expected() const { return after.expected - before.expected; }
    std::uint64_t Inserted() const { return after.inserted - before.inserted; }
    std::uint64_t Distinct() const { return after.distinct - before.distinct; }
    std::uint64_t Misses() const { return after.misses - before.misses; }
};

Frame Render(AgcDriver::VulkanDevice& device, std::initializer_list<std::pair<const Shaders*, VkRect2D>> draws) {
    Pixels.fill(Untouched);
    Frame frame;
    frame.before = device.DeviceImageCounts();
    NewPath::StderrCapture log;
    for (const auto& [shaders, scissor] : draws) Draw(device, *shaders, scissor);
    device.WaitIdle();
    frame.log = log.Finish();
    frame.after = device.DeviceImageCounts();
    AgcDriver::GuestMemory::FlushGpuWrites(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(Pixels.data())), decltype(Pixels)::ByteSize);
    frame.pixels.assign(Pixels.begin(), Pixels.end());
    return frame;
}

void ExpectPixels(const Frame& frame, const std::function<std::uint32_t(std::uint32_t x, std::uint32_t y)>& expected, const std::string& what) {
    Expect(frame.log.find("image table:") == std::string::npos && frame.log.find("is rejected") == std::string::npos, what + ": the draw reported a fault or a rejection:\n" + frame.log);
    for (std::uint32_t y = 0; y < Height; ++y) {
        for (std::uint32_t x = 0; x < Width; ++x) {
            const auto pixel = frame.pixels[y * Width + x];
            if (pixel == expected(x, y)) continue;
            Expect(false, what + ": pixel (" + std::to_string(x) + ", " + std::to_string(y) + ") is " + Hex(pixel) + ", expected " + Hex(expected(x, y)));
            return;
        }
    }
}

void ExpectRoutedStage(const Stage& stage, const std::string& what) {
    Expect(AgcDriver::Graphics::UsesDeviceImageTable(*stage.result), what + ": the stage does not read its image through the device image table");
    Expect(stage.result->deviceImages.size() == 1u, what + ": the CPU pre-pass named " + std::to_string(stage.result->deviceImages.size()) + " device image keys, expected 1");
}

void ExpectFirstDraw(const Frame& frame, std::uint64_t keys, const std::string& what) {
    Expect(frame.Misses() == 0u, what + ": counted " + std::to_string(frame.Misses()) + " device image table misses, expected none");
    Expect(frame.Expected() == keys && frame.Inserted() == keys, what + ": the table took " + std::to_string(frame.Expected()) + " expected keys and inserted " + std::to_string(frame.Inserted()) + ", expected " + std::to_string(keys));
}

void ExpectNoNewKey(const Frame& frame, const std::string& what) {
    Expect(frame.Misses() == 0u, what + ": counted " + std::to_string(frame.Misses()) + " device image table misses, expected none");
    Expect(frame.Expected() == 0u && frame.Inserted() == 0u && frame.Distinct() == 0u, what + ": the table added keys (" + std::to_string(frame.Expected()) + " expected, " + std::to_string(frame.Inserted()) + " inserted, " + std::to_string(frame.Distinct()) + " missed)");
}

std::array<std::uint32_t, 8> PixelUserData(const Texture& texture) {
    std::array<std::uint32_t, 8> userData{};
    const auto image = Image(texture.data(), Format8_8_8_8UNorm);
    std::copy(image.begin(), image.end(), userData.begin());
    std::copy(PointClamp.begin(), PointClamp.end(), userData.begin() + 4);
    return userData;
}

std::array<std::uint32_t, 8> VertexUserData(const Texture* texture) {
    std::array<std::uint32_t, 8> userData{};
    const auto buffer = VertexBuffer();
    std::copy(buffer.begin(), buffer.end(), userData.begin());
    if (texture != nullptr) {
        const auto image = Image(texture->data(), Format8_8_8_8UNorm);
        std::copy(image.begin(), image.end(), userData.begin() + 4);
    }
    return userData;
}

void PixelSample(AgcDriver::VulkanDevice& device) {
    const auto vertexUserData = VertexUserData(nullptr);
    const auto pixelUserData = PixelUserData(First);
    const auto first = Compile(device, PositionVertex(), std::span(vertexUserData).first(4), SamplePixel(), pixelUserData, false);
    ExpectRoutedStage(first.pixel, "pixel sample");
    const auto drawn = Render(device, {{&first, Whole}});
    ExpectPixels(drawn, [](std::uint32_t x, std::uint32_t y) { return Texel(1, x, y); }, "pixel sample: the first draw");
    ExpectFirstDraw(drawn, 1, "pixel sample: the first draw");

    const auto second = Compile(device, PositionVertex(), std::span(vertexUserData).first(4), SamplePixel(), pixelUserData, false);
    Expect(second.pixel.result->PipelineVariantId() == first.pixel.result->PipelineVariantId(), "pixel sample: the second draw built another pixel pipeline");
    const auto again = Render(device, {{&second, Whole}});
    ExpectPixels(again, [](std::uint32_t x, std::uint32_t y) { return Texel(1, x, y); }, "pixel sample: the second draw with the same T#");
    ExpectNoNewKey(again, "pixel sample: the second draw with the same T#");
}

void TwoTextures(AgcDriver::VulkanDevice& device) {
    const auto vertexUserData = VertexUserData(nullptr);
    const auto secondUserData = PixelUserData(Second);
    const auto thirdUserData = PixelUserData(Third);
    const auto left = Compile(device, PositionVertex(), std::span(vertexUserData).first(4), SamplePixel(), secondUserData, false);
    const auto right = Compile(device, PositionVertex(), std::span(vertexUserData).first(4), SamplePixel(), thirdUserData, false);
    ExpectRoutedStage(right.pixel, "two textures");
    Expect(left.pixel.result->deviceImages != right.pixel.result->deviceImages, "two textures: both T#s gave the same device image key");
    const auto drawn = Render(device, {{&left, Left}, {&right, Right}});
    ExpectPixels(drawn, [](std::uint32_t x, std::uint32_t y) { return Texel(x < HalfWidth ? 2u : 3u, x, y); }, "two textures: a draw per T# in one render target, the T# changed between them");
    ExpectFirstDraw(drawn, 2, "two textures: two draws with new T#s");
    const auto swapped = Render(device, {{&right, Left}, {&left, Right}});
    ExpectPixels(swapped, [](std::uint32_t x, std::uint32_t y) { return Texel(x < HalfWidth ? 3u : 2u, x, y); }, "two textures: the same draws with the T#s swapped");
    ExpectNoNewKey(swapped, "two textures: the same draws with the T#s swapped");
}

void VertexTexel(AgcDriver::VulkanDevice& device) {
    const auto vertexUserData = VertexUserData(&Fourth);
    const std::array<std::uint32_t, 0> pixelUserData{};
    const auto first = Compile(device, TexelVertex(), vertexUserData, FlatPixel(), pixelUserData, true);
    ExpectRoutedStage(first.vertex, "vertex image load");
    const auto drawn = Render(device, {{&first, Whole}});
    const auto texel = Texel(4, VertexTexelX, VertexTexelY);
    ExpectPixels(drawn, [texel](std::uint32_t, std::uint32_t) { return texel; }, "vertex image load: the first draw's flat parameter from the provoking vertex's texel");
    ExpectFirstDraw(drawn, 1, "vertex image load: the first draw");
    const auto again = Render(device, {{&first, Whole}});
    ExpectPixels(again, [texel](std::uint32_t, std::uint32_t) { return texel; }, "vertex image load: the second draw");
    ExpectNoNewKey(again, "vertex image load: the second draw");
}

void Run(AgcDriver::VulkanDevice& device) {
    Fill(First, 1);
    Fill(Second, 2);
    Fill(Third, 3);
    Fill(Fourth, 4);
    PixelSample(device);
    TwoTextures(device);
    VertexTexel(device);
}

}

int main() {
    return Main("draw device images", Run);
}
