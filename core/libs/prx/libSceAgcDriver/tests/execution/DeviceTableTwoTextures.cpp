#include "DeviceTableFixture.hpp"

namespace {

using namespace DeviceTable;

enum Texture : std::uint32_t { First, Second, Third, Fourth, Textures };

std::array<TestGuest::Array<float, Width * Height>, Textures> Texels;

float Texel(std::uint32_t texture, std::uint32_t x, std::uint32_t y) {
    switch (texture) {
    case First: return static_cast<float>(x) + 100.0f * static_cast<float>(y) + 0.25f;
    case Second: return -static_cast<float>(x + 1u) - 1000.0f * static_cast<float>(y) - 0.5f;
    case Third: return 3.0f * static_cast<float>(x) + 500.0f * static_cast<float>(y) + 7.75f;
    default: return 0.5f * static_cast<float>(x) + 10.0f * static_cast<float>(y) + 20000.0f;
    }
}

const Program& SampleProgram() {
    static const Program program = [] {
        using namespace Asm;
        Program built;
        LoadSrt(built, {{28, OutputSlot}, {32, SamplerSlot}, {36, ImageSlot}, {40, SecondImageSlot}});
        TexelCenter(built, 2);
        built << Mimg(ImageSampleLz, 1u, true, 4, 1, 36, 32)
              << Mimg(ImageSampleLz, 1u, true, 5, 1, 40, 32)
              << Vop1(VMovB32, 6, InlineInteger(0))
              << Vop1(VMovB32, 7, InlineInteger(0));
        StoreLane(built, 2, 28);
        return built;
    }();
    return program;
}

Expected Component(std::uint32_t component, std::uint32_t texture) {
    return {Height, component, 1, [texture](std::uint32_t row, std::uint32_t x, std::uint32_t, std::uint32_t word) { return SameFloat(word, Texel(texture, x, row)); }};
}

Expected Both(std::uint32_t second) {
    return {Height, 0, 2, [second](std::uint32_t row, std::uint32_t x, std::uint32_t component, std::uint32_t word) { return SameFloat(word, Texel(component == 0u ? First : second, x, row)); }};
}

void ExpectTexels(const Outcome& outcome, const Expected& expected, const std::string& what) {
    std::string mismatch;
    const auto read = Classify(outcome, expected, mismatch);
    Expect(read == Read::Expected, what + ": " + mismatch);
}

void Run(AgcDriver::VulkanDevice& device) {
    for (std::uint32_t texture = 0; texture < Textures; ++texture) {
        for (std::uint32_t y = 0; y < Height; ++y) {
            for (std::uint32_t x = 0; x < Width; ++x) Texels[texture][y * Width + x] = Texel(texture, x, y);
        }
    }
    Srt.fill(0u);
    const auto userData = SrtUserData();
    Place(SamplerSlot, PointClamp);
    Place(ImageSlot, Image(Texels[First].data(), Format32Float));
    Place(SecondImageSlot, Image(Texels[Second].data(), Format32Float));

    const auto first = Compile(device, SampleProgram(), userData);
    ExpectRouted(first, "two textures");
    ExpectNothingCaptured(first, "two textures");
    const auto missed = Run(device, first, Height);
    const auto read = ExpectFirstUse(missed, Both(Second), 2, "two textures: first dispatch");
    const auto hit = Run(device, Compile(device, SampleProgram(), userData), Height);
    ExpectHit(hit, Both(Second), "two textures: second dispatch");
    ExpectResolved(missed, read, hit, 2, "two textures");

    Place(SecondImageSlot, Image(Texels[Third].data(), Format32Float));
    const auto changed = Compile(device, SampleProgram(), userData);
    Expect(changed.result->PipelineVariantId() == first.result->PipelineVariantId(), "two textures: another T# built another pipeline");
    const auto changedMiss = Run(device, changed, Height);
    ExpectTexels(changedMiss, Component(0, First), "two textures: the unchanged T# beside a changed one");
    const auto changedRead = ExpectFirstUse(changedMiss, Component(1, Third), 1, "two textures: the first dispatch with another second T#");
    const auto changedHit = Run(device, Compile(device, SampleProgram(), userData), Height);
    ExpectHit(changedHit, Both(Third), "two textures: the dispatch after the T# change");
    ExpectResolved(changedMiss, changedRead, changedHit, 1, "two textures: after the T# change");

    Place(SecondImageSlot, Image(Texels[Fourth].data(), Format32Float));
    const auto reusedMiss = Run(device, first, Height);
    ExpectTexels(reusedMiss, Component(0, First), "two textures: a program compiled before the changes, reused, with its first T# unchanged");
    const auto reusedRead = ExpectFirstUse(reusedMiss, Component(1, Fourth), 1, "two textures: a program compiled before the changes, reused (nothing captured), reading a T# written to the SRT since");
    const auto reusedHit = Run(device, first, Height);
    ExpectHit(reusedHit, Both(Fourth), "two textures: the reused program's dispatch after that");
    ExpectResolved(reusedMiss, reusedRead, reusedHit, 1, "two textures: the reused program");
}

}

int main() {
    return Main("device table two textures", Run);
}
