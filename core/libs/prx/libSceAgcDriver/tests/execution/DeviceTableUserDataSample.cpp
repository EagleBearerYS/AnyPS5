#include "DeviceTableFixture.hpp"

namespace {

using namespace DeviceTable;

TestGuest::Array<std::uint32_t, Width * Height> Texels;

std::uint32_t Texel(std::uint32_t x, std::uint32_t y) {
    return (1u + 3u * x) | ((10u + 60u * y) << 8u) | ((255u - x) << 16u) | ((0x40u + x + y) << 24u);
}

const Program& SampleProgram() {
    static const Program program = [] {
        using namespace Asm;
        Program built;
        TexelCenter(built, 12);
        built << Mimg(ImageSampleLz, 0xfu, true, 4, 1, 4, 8);
        StoreLane(built, 12, 0);
        return built;
    }();
    return program;
}

void Run(AgcDriver::VulkanDevice& device) {
    for (std::uint32_t y = 0; y < Height; ++y) {
        for (std::uint32_t x = 0; x < Width; ++x) Texels[y * Width + x] = Texel(x, y);
    }
    std::array<std::uint32_t, 12> userData{};
    const auto output = Buffer(Output.data(), static_cast<std::uint32_t>(decltype(Output)::ByteSize));
    const auto image = Image(Texels.data(), Format8_8_8_8UNorm);
    std::copy(output.begin(), output.end(), userData.begin());
    std::copy(image.begin(), image.end(), userData.begin() + 4);
    std::copy(PointClamp.begin(), PointClamp.end(), userData.begin() + 8);
    const Expected texels{Height, 0, 4, [](std::uint32_t row, std::uint32_t x, std::uint32_t component, std::uint32_t word) { return SameUnorm(word, (Texel(x, row) >> (8u * component)) & 0xffu); }};

    const auto first = Compile(device, SampleProgram(), userData);
    ExpectRouted(first, "user-data sample");
    ExpectNothingCaptured(first, "user-data sample");
    const auto missed = Run(device, first, Height);
    const auto read = ExpectFirstUse(missed, texels, 1, "user-data sample: first dispatch");

    const auto second = Compile(device, SampleProgram(), userData);
    Expect(second.result->PipelineVariantId() == first.result->PipelineVariantId(), "user-data sample: the second dispatch built another pipeline");
    const auto hit = Run(device, second, Height);
    ExpectHit(hit, texels, "user-data sample: second dispatch with the same T#");
    ExpectResolved(missed, read, hit, 1, "user-data sample");
}

}

int main() {
    return Main("device table user-data sample", Run);
}
