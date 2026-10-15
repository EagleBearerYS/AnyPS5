#include "DeviceTableFixture.hpp"

namespace {

using namespace DeviceTable;

constexpr std::uint32_t WrittenTexel = 2 * Width + 17;
constexpr float WrittenValue = 4242.5f;

TestGuest::Array<float, Width * Height> Texels;
std::array<float, Width * Height> Model;

const Program& SampleProgram() {
    static const Program program = [] {
        using namespace Asm;
        Program built;
        LoadSrt(built, {{28, OutputSlot}, {32, SamplerSlot}, {36, ImageSlot}});
        TexelCenter(built, 2);
        built << Mimg(ImageSampleLz, 1u, true, 4, 1, 36, 32);
        StoreLane(built, 2, 28);
        return built;
    }();
    return program;
}

Expected Modelled() {
    return {Height, 0, 1, [](std::uint32_t row, std::uint32_t x, std::uint32_t, std::uint32_t word) { return SameFloat(word, Model[row * Width + x]); }};
}

void Write(std::uint32_t texel, float value) {
    Texels[texel] = value;
    Model[texel] = value;
}

void Run(AgcDriver::VulkanDevice& device) {
    for (std::uint32_t texel = 0; texel < Width * Height; ++texel) Write(texel, static_cast<float>(texel) + 0.75f);
    Srt.fill(0u);
    const auto userData = SrtUserData();
    Place(SamplerSlot, PointClamp);
    Place(ImageSlot, Image(Texels.data(), Format32Float));

    const auto first = Compile(device, SampleProgram(), userData);
    ExpectRouted(first, "CPU writes");
    ExpectNothingCaptured(first, "CPU writes");
    const auto missed = Run(device, first, Height);
    const auto read = ExpectFirstUse(missed, Modelled(), 1, "CPU writes: first dispatch");
    const auto hit = Run(device, Compile(device, SampleProgram(), userData), Height);
    ExpectHit(hit, Modelled(), "CPU writes: second dispatch");
    ExpectResolved(missed, read, hit, 1, "CPU writes");

    for (std::uint32_t texel = 0; texel < Width * Height; ++texel) Write(texel, -2.0f * static_cast<float>(texel) - 1.5f);
    const auto rewritten = Run(device, Compile(device, SampleProgram(), userData), Height);
    ExpectHit(rewritten, Modelled(), "CPU writes: the dispatch after the CPU rewrote every texel");
    Expect(rewritten.Refreshed() != 0u, "CPU writes: the device table entry was not refreshed after the CPU rewrote every texel");

    Write(WrittenTexel, WrittenValue);
    const auto patched = Run(device, Compile(device, SampleProgram(), userData), Height);
    ExpectHit(patched, Modelled(), "CPU writes: the dispatch after the CPU wrote one texel");
    Expect(patched.Refreshed() != 0u, "CPU writes: the device table entry was not refreshed after the CPU wrote one texel");

    const auto reused = Run(device, first, Height);
    ExpectHit(reused, Modelled(), "CPU writes: the program compiled before the writes, reused");
}

}

int main() {
    return Main("device table CPU writes", Run);
}
