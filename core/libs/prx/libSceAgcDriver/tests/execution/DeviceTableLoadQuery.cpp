#include "DeviceTableFixture.hpp"

namespace {

using namespace DeviceTable;

TestGuest::Array<std::uint32_t, Width * Height> Texels;

std::uint32_t Texel(std::uint32_t x, std::uint32_t y) {
    return 0x51000u + x + 0x100u * y;
}

const Program& LoadProgram() {
    static const Program program = [] {
        using namespace Asm;
        Program built;
        LoadSrt(built, {{28, OutputSlot}, {36, ImageSlot}});
        TexelIndex(built, 2);
        built << Mimg(ImageLoad, 0xfu, true, 4, 1, 36);
        StoreLane(built, 2, 28);
        return built;
    }();
    return program;
}

const Program& QueryProgram() {
    static const Program program = [] {
        using namespace Asm;
        Program built;
        LoadSrt(built, {{28, OutputSlot}, {36, ImageSlot}});
        built << Vop1(VMovB32, 1, InlineInteger(0));
        built << Mimg(ImageGetResinfo, 0xfu, true, 4, 1, 36);
        StoreLane(built, 2, 28);
        return built;
    }();
    return program;
}

void Check(AgcDriver::VulkanDevice& device, const Program& program, std::span<const std::uint32_t> userData, std::uint32_t groups, const Expected& expected, const std::string& what) {
    const auto first = Compile(device, program, userData);
    ExpectRouted(first, what);
    ExpectNothingCaptured(first, what);
    const auto missed = Run(device, first, groups);
    const auto read = ExpectFirstUse(missed, expected, 1, what + ": first dispatch");
    const auto second = Compile(device, program, userData);
    const auto hit = Run(device, second, groups);
    ExpectHit(hit, expected, what + ": second dispatch with the same T#");
    ExpectResolved(missed, read, hit, 1, what);
}

void Run(AgcDriver::VulkanDevice& device) {
    for (std::uint32_t y = 0; y < Height; ++y) {
        for (std::uint32_t x = 0; x < Width; ++x) Texels[y * Width + x] = Texel(x, y);
    }
    Srt.fill(0u);
    const auto userData = SrtUserData();
    Place(ImageSlot, Image(Texels.data(), Format32UInt));
    Check(device, LoadProgram(), userData, Height, {Height, 0, 1, [](std::uint32_t row, std::uint32_t x, std::uint32_t, std::uint32_t word) { return word == Texel(x, row); }}, "texel load");
    Check(device, QueryProgram(), userData, 1, {1, 0, 2, [](std::uint32_t, std::uint32_t, std::uint32_t component, std::uint32_t word) { return word == (component == 0u ? Width : Height); }}, "image query");
}

}

int main() {
    return Main("device table load and query", Run);
}
