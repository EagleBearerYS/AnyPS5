#include "DeviceTableFixture.hpp"
#include <bit>
#include <functional>
#include <string>
#include <vector>

#ifdef _WIN32

namespace {

using DeviceTable::Expect;
using NewPath::Commands;
using NewPath::Hex;
using NewPath::High;
using NewPath::Low;
using NewPath::PageBytes;
using NewPath::PrivatePages;
using NewPath::Program;

constexpr std::uint32_t Side = 256;
constexpr std::uint32_t Texels = Side * Side;
constexpr std::uint64_t TextureBytes = std::uint64_t{Texels} * 4u;
constexpr std::uint32_t Lanes = 64;
constexpr std::uint32_t LinearSwizzle = 0x00u;
constexpr std::uint32_t Standard64KSwizzle = 0x09u;
constexpr std::uint32_t BlockSide = 128;
constexpr std::uint32_t OneOverSide = 0x3b800000u;

const Program& SampleGridProgram() {
    static const Program program = [] {
        using namespace DeviceTable::Asm;
        constexpr std::uint32_t VLshrRevB32 = 0x16u;
        constexpr std::uint32_t VOrB32 = 0x1cu;
        Program built;
        LoadSrt(built, {{28, DeviceTable::OutputSlot}, {32, DeviceTable::SamplerSlot}, {36, DeviceTable::ImageSlot}});
        built << Vop1(VMovB32, 3, 2)
              << Vop2(VAndB32, 4, InlineInteger(3), 3)
              << Vop2(VLshlRevB32, 4, InlineInteger(6), 4)
              << Vop2(VOrB32, 1, Vgpr(4), 0)
              << Vop2(VLshrRevB32, 2, InlineInteger(2), 3)
              << Vop2(VLshlRevB32, 5, InlineInteger(8), 2)
              << Vop2(VOrB32, 5, Vgpr(1), 5)
              << Vop2(VLshlRevB32, 5, InlineInteger(2), 5)
              << Vop1(VCvtF32U32, 1, Vgpr(1))
              << Vop2(VAddF32, 1, Half, 1)
              << Vop2(VMulF32, 1, Literal, 1) << OneOverSide
              << Vop1(VCvtF32U32, 2, Vgpr(2))
              << Vop2(VAddF32, 2, Half, 2)
              << Vop2(VMulF32, 2, Literal, 2) << OneOverSide
              << Mimg(ImageSampleLz, 1u, true, 6, 1, 36, 32)
              << WaitAll
              << Mubuf(BufferStoreDword, 6, 5, 28)
              << EndProgram;
        return built;
    }();
    return program;
}

TestGuest::Array<std::uint32_t, DeviceTable::SrtWords> GridSrt;

std::array<std::uint32_t, 4> TextureDescriptor(std::uint64_t address, std::uint32_t swizzle) {
    return {
        Low(address >> 8u),
        (Low(address >> 40u) & 0xffu) | (DeviceTable::Format32Float << 20u) | (((Side - 1u) & 3u) << 30u),
        ((Side - 1u) >> 2u) | ((Side - 1u) << 14u) | (1u << 31u),
        0xfacu | (swizzle << 20u) | (DeviceTable::Type2D << 28u),
    };
}

std::vector<std::uint32_t> SampleAll(std::uint64_t texture, std::uint32_t swizzle, const PrivatePages& output, const std::string& what) {
    std::fill(GridSrt.begin(), GridSrt.end(), 0u);
    const auto buffer = DeviceTable::Buffer(output.Data(), static_cast<std::uint32_t>(TextureBytes));
    std::copy(buffer.begin(), buffer.end(), GridSrt.begin() + DeviceTable::OutputSlot);
    std::copy(DeviceTable::PointClamp.begin(), DeviceTable::PointClamp.end(), GridSrt.begin() + DeviceTable::SamplerSlot);
    const auto image = TextureDescriptor(texture, swizzle);
    std::copy(image.begin(), image.end(), GridSrt.begin() + DeviceTable::ImageSlot);
    std::memset(output.Data(), 0xee, TextureBytes);
    const auto srt = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(GridSrt.data()));
    const std::array<std::uint32_t, 2> userData{Low(srt), High(srt)};
    Commands commands;
    commands.Dispatch(SampleGridProgram(), userData, Lanes, Texels / Lanes);
    std::string failure;
    NewPath::StderrCapture capture;
    try {
        commands.Submit(0);
        NewPath::WaitIdle();
    } catch (const std::exception& error) {
        failure = error.what();
    }
    const auto log = capture.Finish();
    Expect(failure.empty() && log.find("failed") == std::string::npos, what + ": the sampling dispatch failed: " + failure + "\n" + log);
    AgcDriver::GuestMemory::FlushGpuWrites(output.Address(), TextureBytes);
    std::vector<std::uint32_t> sampled(Texels);
    std::memcpy(sampled.data(), output.Data(), TextureBytes);
    return sampled;
}

void ExpectSampled(const std::vector<std::uint32_t>& sampled, const std::function<float(std::uint32_t x, std::uint32_t y)>& expected, const std::function<std::uint32_t(std::uint32_t x, std::uint32_t y)>& unit, const std::string& what) {
    std::array<std::size_t, 4> wrong{};
    std::string first;
    for (std::uint32_t y = 0; y < Side; ++y) {
        for (std::uint32_t x = 0; x < Side; ++x) {
            const auto bits = std::bit_cast<std::uint32_t>(expected(x, y));
            const auto actual = sampled[y * Side + x];
            if (actual == bits) continue;
            const auto at = unit(x, y);
            if (wrong[at]++ == 0u && first.empty()) first = "texel (" + std::to_string(x) + ", " + std::to_string(y) + ") of unit " + std::to_string(at) + " sampled " + Hex(actual) + ", expected " + Hex(bits);
        }
    }
    const auto total = wrong[0] + wrong[1] + wrong[2] + wrong[3];
    Expect(total == 0u, what + ": wrong texels per 64 KiB unit " + std::to_string(wrong[0]) + ", " + std::to_string(wrong[1]) + ", " + std::to_string(wrong[2]) + ", " + std::to_string(wrong[3]) + "; first " + first);
}

float LinearOld(std::uint32_t x, std::uint32_t y) {
    return static_cast<float>(x + Side * y) + 0.25f;
}

float LinearNew(std::uint32_t x, std::uint32_t y) {
    return static_cast<float>(100000u + x + Side * y);
}

void LinearUnitStore() {
    auto* texels = reinterpret_cast<float*>(TestGuest::Allocate(TextureBytes, PageBytes));
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(texels));
    PrivatePages output(static_cast<std::uint32_t>(TextureBytes / PageBytes));
    const auto unit = [](std::uint32_t, std::uint32_t y) { return y / (Side / 4u); };
    constexpr std::uint32_t Stored = 2;
    for (std::uint32_t y = 0; y < Side; ++y) {
        for (std::uint32_t x = 0; x < Side; ++x) texels[y * Side + x] = LinearOld(x, y);
    }
    ExpectSampled(SampleAll(address, LinearSwizzle, output, "a linear texture"), LinearOld, unit, "a linear 256x256 texture of four 64 KiB units, sampled first");
    for (std::uint32_t y = Stored * (Side / 4u); y < (Stored + 1u) * (Side / 4u); ++y) {
        for (std::uint32_t x = 0; x < Side; ++x) texels[y * Side + x] = LinearNew(x, y);
    }
    const auto expected = [&](std::uint32_t x, std::uint32_t y) { return unit(x, y) == Stored ? LinearNew(x, y) : LinearOld(x, y); };
    ExpectSampled(SampleAll(address, LinearSwizzle, output, "a linear texture"), expected, unit, "a linear texture sampled after the CPU stored new texels into its 64 KiB unit " + std::to_string(Stored));
}

float TiledOld(std::uint32_t unit) {
    return 1000.5f + static_cast<float>(unit);
}

void TiledUnitStores() {
    auto* texels = reinterpret_cast<std::uint32_t*>(TestGuest::Allocate(TextureBytes, PageBytes));
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(texels));
    PrivatePages output(static_cast<std::uint32_t>(TextureBytes / PageBytes));
    const auto unit = [](std::uint32_t x, std::uint32_t y) { return (y / BlockSide) * (Side / BlockSide) + x / BlockSide; };
    constexpr std::uint32_t UnitWords = static_cast<std::uint32_t>(PageBytes / 4u);
    for (std::uint32_t block = 0; block < 4u; ++block) std::fill(texels + block * UnitWords, texels + (block + 1u) * UnitWords, std::bit_cast<std::uint32_t>(TiledOld(block)));
    ExpectSampled(SampleAll(address, Standard64KSwizzle, output, "a 64 KiB tiled texture"), [&](std::uint32_t x, std::uint32_t y) { return TiledOld(unit(x, y)); }, unit, "a 64 KiB tiled 256x256 texture of four blocks, sampled first");

    constexpr std::uint32_t Rewritten = 1;
    constexpr float RewrittenValue = 2001.5f;
    std::fill(texels + Rewritten * UnitWords, texels + (Rewritten + 1u) * UnitWords, std::bit_cast<std::uint32_t>(RewrittenValue));
    const auto afterUnit = [&](std::uint32_t x, std::uint32_t y) { return unit(x, y) == Rewritten ? RewrittenValue : TiledOld(unit(x, y)); };
    ExpectSampled(SampleAll(address, Standard64KSwizzle, output, "a 64 KiB tiled texture"), afterUnit, unit, "a 64 KiB tiled texture sampled after the CPU rewrote its unit " + std::to_string(Rewritten));

    constexpr std::uint32_t Touched = 3;
    constexpr std::uint32_t TouchedWord = 0x2340u / 4u;
    constexpr float TouchedValue = 3333.5f;
    texels[Touched * UnitWords + TouchedWord] = std::bit_cast<std::uint32_t>(TouchedValue);
    const auto sampled = SampleAll(address, Standard64KSwizzle, output, "a 64 KiB tiled texture");
    std::array<std::size_t, 4> changed{};
    std::size_t touched = 0;
    std::size_t foreign = 0;
    for (std::uint32_t y = 0; y < Side; ++y) {
        for (std::uint32_t x = 0; x < Side; ++x) {
            const auto actual = sampled[y * Side + x];
            const auto at = unit(x, y);
            if (actual == std::bit_cast<std::uint32_t>(afterUnit(x, y))) continue;
            ++changed[at];
            if (at == Touched && actual == std::bit_cast<std::uint32_t>(TouchedValue)) ++touched;
            else ++foreign;
        }
    }
    const std::string what = "a 64 KiB tiled texture sampled after the CPU stored one word into its unit " + std::to_string(Touched);
    Expect(touched == 1u && foreign == 0u, what + ": " + std::to_string(touched) + " texels sampled the stored word and " + std::to_string(foreign) + " other texels changed (per unit " + std::to_string(changed[0]) + ", " + std::to_string(changed[1]) + ", " + std::to_string(changed[2]) + ", " + std::to_string(changed[3]) + "), expected exactly the one stored texel");
}

}

#endif

int main() {
#ifdef _WIN32
    {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
    }
    for (const auto& [name, run] : std::array<std::pair<const char*, void (*)()>, 2>{{{"linear unit store", LinearUnitStore}, {"tiled unit stores", TiledUnitStores}}}) {
        try {
            run();
        } catch (const std::exception& error) {
            Expect(false, std::string(name) + ": exception: " + error.what());
        }
    }
    try {
        AgcDriverShutdown_nid_postfix();
    } catch (const std::exception& error) {
        Expect(false, std::string("shutdown: ") + error.what());
    }
    return DeviceTable::Finish("sampled unit upload");
#else
    std::printf("skipped, the guest arena is Windows-only here\n");
    return VulkanTestSkipped;
#endif
}
