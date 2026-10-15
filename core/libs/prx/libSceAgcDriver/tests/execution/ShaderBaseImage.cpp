#include "DeviceTableFixture.hpp"

namespace {

using namespace DeviceTable;
using AgcDriver::Graphics::Require;

constexpr std::uint32_t CodeWords = 128;
constexpr std::uint32_t BaseSgpr = 20;
constexpr std::uint32_t GetPc = 0xbe801f00u | (BaseSgpr << 16u);
constexpr std::uint32_t SLoadDwordX8 = 0x03u;

TestGuest::Array<std::uint32_t, Width * Height> Texels;
TestGuest::Array<std::uint32_t, Width * Height> OtherTexels;
TestGuest::Array<std::uint32_t, CodeWords> Code;
TestGuest::Array<std::uint32_t, CodeWords> CopiedCode;

std::uint32_t Texel(std::uint32_t x, std::uint32_t y) {
    return (0x30u + x + 40u * y) | ((200u - 2u * x) << 8u) | ((7u + x * y) << 16u) | ((0xffu - 9u * y) << 24u);
}

std::uint32_t OtherTexel(std::uint32_t x, std::uint32_t y) {
    return (0xc0u - x) | ((0x11u + 3u * y) << 8u) | ((0x80u ^ x) << 16u) | ((0x20u + x + y) << 24u);
}

struct Layout {
    std::vector<std::uint32_t> words;
    std::uint32_t descriptorWord = 0;
};

const Layout& EmbeddedProgram() {
    static const Layout layout = [] {
        using namespace Asm;
        Program built;
        built << GetPc;
        const auto load = built.size;
        built << Smem(SLoadDwordX8, 36, BaseSgpr, 0u);
        LoadSrt(built, {{28, OutputSlot}, {32, SamplerSlot}});
        TexelCenter(built, 2);
        built << Mimg(ImageSampleLz, 0xfu, false, 4, 1, 36, 32);
        StoreLane(built, 2, 28);
        Layout made;
        made.descriptorWord = static_cast<std::uint32_t>(built.size);
        made.words.assign(built.words.begin(), built.words.begin() + static_cast<std::ptrdiff_t>(built.size));
        made.words[load + 1u] = (NullSgpr << 25u) | (made.descriptorWord * 4u - 4u);
        made.words.resize(made.descriptorWord + 8u, 0u);
        return made;
    }();
    return layout;
}

void Embed(TestGuest::Array<std::uint32_t, CodeWords>& code, const void* texels) {
    const auto& layout = EmbeddedProgram();
    Require(layout.words.size() <= CodeWords, "shader base image: the program does not fit");
    code.fill(0u);
    std::copy(layout.words.begin(), layout.words.end(), code.begin());
    const auto image = Image(texels, Format8_8_8_8UNorm);
    std::copy(image.begin(), image.end(), code.begin() + layout.descriptorWord);
}

Compiled CompileAt(AgcDriver::VulkanDevice& device, const TestGuest::Array<std::uint32_t, CodeWords>& code, std::span<const std::uint32_t> userData) {
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(code.data()));
    const std::span<const std::uint32_t> words(code.data(), EmbeddedProgram().words.size());
    const ShaderRecompiler::ShaderComputeStageInfo compute{{Lanes, 1, 1}, 0u, {true, false, false}, false, 1};
    const ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, address, words, 0, {}},
        {64, 0, userData, compute, std::nullopt, std::nullopt, {}},
        device.Target(),
        {0, 0, 0, 128}
    };
    return CompileRequest(request);
}

Expected Sampled(std::uint32_t (*texel)(std::uint32_t, std::uint32_t)) {
    return {Height, 0, 4, [texel](std::uint32_t row, std::uint32_t x, std::uint32_t component, std::uint32_t word) { return SameUnorm(word, (texel(x, row) >> (8u * component)) & 0xffu); }};
}

void ExpectAtRunTime(const Compiled& compiled, const std::string& what) {
    ExpectRouted(compiled, what);
    Expect(compiled.plan->srtReads.empty(), what + ": the plan reads " + std::to_string(compiled.plan->srtReads.size()) + " SRT words in the capture");
}

void SampleTwice(AgcDriver::VulkanDevice& device, const TestGuest::Array<std::uint32_t, CodeWords>& code, std::span<const std::uint32_t> userData, const Expected& expected, const std::string& what) {
    const auto first = CompileAt(device, code, userData);
    ExpectAtRunTime(first, what);
    AgcDriver::GuestMemory::BumpCollectEpoch();
    const auto missed = DeviceTable::Run(device, first, Height);
    const auto read = ExpectFirstUse(missed, expected, 1, what + ": first dispatch");
    const auto second = CompileAt(device, code, userData);
    ExpectAtRunTime(second, what + ": second capture");
    AgcDriver::GuestMemory::BumpCollectEpoch();
    const auto hit = DeviceTable::Run(device, second, Height);
    ExpectHit(hit, expected, what + ": second dispatch");
    ExpectResolved(missed, read, hit, 1, what);
}

void Run(AgcDriver::VulkanDevice& device) {
    for (std::uint32_t y = 0; y < Height; ++y) {
        for (std::uint32_t x = 0; x < Width; ++x) {
            Texels[y * Width + x] = Texel(x, y);
            OtherTexels[y * Width + x] = OtherTexel(x, y);
        }
    }
    Srt.fill(0u);
    const auto userData = SrtUserData();
    Place(SamplerSlot, PointClamp);

    Embed(Code, Texels.data());
    Embed(CopiedCode, Texels.data());
    SampleTwice(device, Code, userData, Sampled(Texel), "shader base image: a T# after s_endpgm loaded relative to s_getpc_b64");

    Embed(Code, OtherTexels.data());
    SampleTwice(device, Code, userData, Sampled(OtherTexel), "shader base image: the same program after the CPU rewrote its embedded T#");

    SampleTwice(device, CopiedCode, userData, Sampled(Texel), "shader base image: a copy of the first program at another address, whose words equal the first program's before the rewrite");
}

}

int main() {
    return Main("shader base image", Run);
}
