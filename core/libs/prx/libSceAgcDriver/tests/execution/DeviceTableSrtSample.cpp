#include "DeviceTableFixture.hpp"

namespace {

using namespace DeviceTable;

TestGuest::Array<std::uint32_t, Width * Height> Texels;

std::uint32_t Texel(std::uint32_t x, std::uint32_t y) {
    return (0x30u + x + 40u * y) | ((200u - 2u * x) << 8u) | ((7u + x * y) << 16u) | ((0xffu - 9u * y) << 24u);
}

const Program& SampleProgram() {
    static const Program program = [] {
        using namespace Asm;
        Program built;
        LoadSrt(built, {{28, OutputSlot}, {32, SamplerSlot}, {36, ImageSlot}});
        TexelCenter(built, 2);
        built << Mimg(ImageSampleLz, 0xfu, true, 4, 1, 36, 32);
        StoreLane(built, 2, 28);
        return built;
    }();
    return program;
}

void Run(AgcDriver::VulkanDevice& device) {
    for (std::uint32_t y = 0; y < Height; ++y) {
        for (std::uint32_t x = 0; x < Width; ++x) Texels[y * Width + x] = Texel(x, y);
    }
    Srt.fill(0u);
    const auto userData = SrtUserData();
    Place(SamplerSlot, PointClamp);
    Place(ImageSlot, Image(Texels.data(), Format8_8_8_8UNorm));
    const Expected texels{Height, 0, 4, [](std::uint32_t row, std::uint32_t x, std::uint32_t component, std::uint32_t word) { return SameUnorm(word, (Texel(x, row) >> (8u * component)) & 0xffu); }};

    const auto first = Compile(device, SampleProgram(), userData);
    ExpectRouted(first, "SRT sample");
    ExpectNothingCaptured(first, "SRT sample");
    const auto missed = Run(device, first, Height);
    const auto read = ExpectFirstUse(missed, texels, 1, "SRT sample: first dispatch");

    const auto second = Compile(device, SampleProgram(), userData);
    ExpectNothingCaptured(second, "SRT sample: second capture");
    Expect(second.result->PipelineVariantId() == first.result->PipelineVariantId(), "SRT sample: the second dispatch built another pipeline");
    const auto hit = Run(device, second, Height);
    ExpectHit(hit, texels, "SRT sample: second dispatch with the same T#");
    ExpectResolved(missed, read, hit, 1, "SRT sample");
}

}

int main() {
    return Main("device table SRT sample", Run);
}
