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
using NewPath::Require;
namespace Graphics = AgcDriver::Graphics;

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

std::vector<std::uint32_t> SampleWith(const Program& program, std::span<const std::uint32_t> image, std::uint32_t groups, const PrivatePages& output, const std::string& what) {
    std::fill(GridSrt.begin(), GridSrt.end(), 0u);
    const auto buffer = DeviceTable::Buffer(output.Data(), static_cast<std::uint32_t>(TextureBytes));
    std::copy(buffer.begin(), buffer.end(), GridSrt.begin() + DeviceTable::OutputSlot);
    std::copy(DeviceTable::PointClamp.begin(), DeviceTable::PointClamp.end(), GridSrt.begin() + DeviceTable::SamplerSlot);
    std::copy(image.begin(), image.end(), GridSrt.begin() + DeviceTable::ImageSlot);
    std::memset(output.Data(), 0xee, TextureBytes);
    const auto srt = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(GridSrt.data()));
    const std::array<std::uint32_t, 2> userData{Low(srt), High(srt)};
    Commands commands;
    commands.Dispatch(program, userData, Lanes, groups);
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

std::vector<std::uint32_t> SampleAll(std::uint64_t texture, std::uint32_t swizzle, const PrivatePages& output, const std::string& what) {
    return SampleWith(SampleGridProgram(), TextureDescriptor(texture, swizzle), Texels / Lanes, output, what);
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

constexpr std::uint32_t Standard4KSwizzle = 0x05u;
constexpr std::uint64_t Misalign = 0x3000;
constexpr std::uint32_t TileBytes = 4096;
constexpr std::uint32_t TileSide = 32;
constexpr std::uint32_t LayerSide = 128;
constexpr std::uint32_t Layers = 4;
constexpr std::uint32_t LayerTexels = LayerSide * LayerSide;
constexpr std::uint32_t OneOverLayerSide = 0x3c000000u;
constexpr std::uint32_t Type2DArray = 13;

using Expected = std::function<float(std::uint32_t index)>;
using UnitOf = std::function<std::uint32_t(std::uint32_t index)>;
using Named = std::function<std::string(std::uint32_t index)>;

std::string GridTexel(std::uint32_t index) {
    return "texel (" + std::to_string(index % Side) + ", " + std::to_string(index / Side) + ")";
}

std::string LayerTexel(std::uint32_t index) {
    return "layer " + std::to_string(index / LayerTexels) + " texel (" + std::to_string(index % LayerSide) + ", " + std::to_string((index / LayerSide) % LayerSide) + ")";
}

void ExpectTexels(const std::vector<std::uint32_t>& sampled, const Expected& expected, const UnitOf& unit, std::uint32_t units, const Named& name, const std::string& what) {
    std::vector<std::size_t> wrong(units);
    std::string first;
    for (std::uint32_t index = 0; index < sampled.size(); ++index) {
        const auto bits = std::bit_cast<std::uint32_t>(expected(index));
        if (sampled[index] == bits) continue;
        const auto at = unit(index);
        if (wrong.at(at)++ == 0u && first.empty()) first = name(index) + " of unit " + std::to_string(at) + " sampled " + Hex(sampled[index]) + ", expected " + Hex(bits);
    }
    std::size_t total = 0;
    std::string counts;
    for (const auto count : wrong) {
        total += count;
        counts += (counts.empty() ? "" : ", ") + std::to_string(count);
    }
    Expect(total == 0u, what + ": wrong texels per 64 KiB unit " + counts + "; first " + first);
}

void ExpectOneChanged(const std::vector<std::uint32_t>& sampled, const Expected& before, float touched, const UnitOf& unit, std::uint32_t touchedUnit, const Named& name, const std::string& what) {
    std::size_t hits = 0;
    std::size_t foreign = 0;
    std::string first;
    for (std::uint32_t index = 0; index < sampled.size(); ++index) {
        if (sampled[index] == std::bit_cast<std::uint32_t>(before(index))) continue;
        if (unit(index) == touchedUnit && sampled[index] == std::bit_cast<std::uint32_t>(touched)) {
            ++hits;
            continue;
        }
        if (foreign++ == 0u) first = ", first " + name(index) + " of unit " + std::to_string(unit(index)) + " sampled " + Hex(sampled[index]);
    }
    Expect(hits == 1u && foreign == 0u, what + ": " + std::to_string(hits) + " texels sampled the stored word and " + std::to_string(foreign) + " other texels changed" + first + "; expected exactly the one stored texel");
}

const Program& SampleLayersProgram() {
    static const Program program = [] {
        using namespace DeviceTable::Asm;
        constexpr std::uint32_t VLshrRevB32 = 0x16u;
        constexpr std::uint32_t VOrB32 = 0x1cu;
        constexpr std::uint32_t SLoadDwordX8 = 0x03u;
        Program built;
        built << Smem(SLoadDwordX4, 28, 0, DeviceTable::OutputSlot * 4u)
              << Smem(SLoadDwordX4, 32, 0, DeviceTable::SamplerSlot * 4u)
              << Smem(SLoadDwordX8, 36, 0, DeviceTable::ImageSlot * 4u)
              << WaitAll
              << Vop1(VMovB32, 3, 2)
              << Vop2(VAndB32, 4, InlineInteger(1), 3)
              << Vop2(VLshlRevB32, 4, InlineInteger(6), 4)
              << Vop2(VOrB32, 1, Vgpr(4), 0)
              << Vop2(VLshrRevB32, 2, InlineInteger(1), 3)
              << Vop2(VAndB32, 2, Literal, 2) << (LayerSide - 1u)
              << Vop2(VLshrRevB32, 6, InlineInteger(8), 3)
              << Vop2(VLshlRevB32, 5, InlineInteger(6), 3)
              << Vop2(VOrB32, 5, Vgpr(0), 5)
              << Vop2(VLshlRevB32, 5, InlineInteger(2), 5)
              << Vop1(VCvtF32U32, 1, Vgpr(1))
              << Vop2(VAddF32, 1, Half, 1)
              << Vop2(VMulF32, 1, Literal, 1) << OneOverLayerSide
              << Vop1(VCvtF32U32, 2, Vgpr(2))
              << Vop2(VAddF32, 2, Half, 2)
              << Vop2(VMulF32, 2, Literal, 2) << OneOverLayerSide
              << Vop1(VCvtF32U32, 3, Vgpr(6))
              << std::array<std::uint32_t, 2>{0xf0000000u | (ImageSampleLz << 18u) | (1u << 8u) | (5u << 3u), ((32u / 4u) << 21u) | ((36u / 4u) << 16u) | (7u << 8u) | 1u}
              << WaitAll
              << Mubuf(BufferStoreDword, 7, 5, 28)
              << EndProgram;
        return built;
    }();
    return program;
}

std::array<std::uint32_t, 8> LayersDescriptor(std::uint64_t address, std::uint32_t swizzle) {
    return {
        Low(address >> 8u),
        (Low(address >> 40u) & 0xffu) | (DeviceTable::Format32Float << 20u) | (((LayerSide - 1u) & 3u) << 30u),
        ((LayerSide - 1u) >> 2u) | ((LayerSide - 1u) << 14u) | (1u << 31u),
        0xfacu | (swizzle << 20u) | (Type2DArray << 28u),
        Layers - 1u,
        0u,
        0u,
        0u,
    };
}

std::vector<std::uint32_t> SampleLayers(std::uint64_t texture, std::uint32_t swizzle, const PrivatePages& output, const std::string& what) {
    return SampleWith(SampleLayersProgram(), LayersDescriptor(texture, swizzle), Layers * LayerTexels / Lanes, output, what);
}

float TileOld(std::uint32_t tile) {
    return 500.5f + static_cast<float>(tile);
}

float TileNew(std::uint32_t tile) {
    return 7000.5f + static_cast<float>(tile);
}

void UnalignedTiledStores() {
    const auto block = Graphics::ThinBlockLayout(Graphics::TextureTileMode::kStandard4KB, 4u);
    Require(block[0] == TileBytes && block[1] == TileSide && block[2] == TileSide, "sampled unit uploads: a 4 KiB tile of 32-bit texels is not 32x32");
    auto* allocation = reinterpret_cast<std::uint8_t*>(TestGuest::Allocate(TextureBytes + PageBytes, PageBytes));
    auto* texels = reinterpret_cast<std::uint32_t*>(allocation + Misalign);
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(texels));
    PrivatePages output(static_cast<std::uint32_t>(TextureBytes / PageBytes));
    constexpr std::uint32_t TileWords = TileBytes / 4u;
    constexpr std::uint32_t Tiles = static_cast<std::uint32_t>(TextureBytes / TileBytes);
    constexpr std::uint32_t Pages = static_cast<std::uint32_t>((Misalign + TextureBytes + PageBytes - 1u) / PageBytes);
    const auto tile = [](std::uint32_t index) { return (index / Side / TileSide) * (Side / TileSide) + index % Side / TileSide; };
    const UnitOf page = [&](std::uint32_t index) { return static_cast<std::uint32_t>((Misalign + std::uint64_t{tile(index)} * TileBytes) / PageBytes); };
    for (std::uint32_t at = 0; at < Tiles; ++at) std::fill(texels + at * TileWords, texels + (at + 1u) * TileWords, std::bit_cast<std::uint32_t>(TileOld(at)));
    const Expected old = [&](std::uint32_t index) { return TileOld(tile(index)); };
    const std::string kind = "a 4 KiB tiled 256x256 texture based 12 KiB past a 64 KiB boundary";
    ExpectTexels(SampleAll(address, Standard4KSwizzle, output, kind), old, page, Pages, GridTexel, kind + ", sampled first");

    constexpr std::uint32_t Rewritten = 2;
    constexpr auto FirstTile = static_cast<std::uint32_t>((Rewritten * PageBytes - Misalign) / TileBytes);
    constexpr auto PageTiles = static_cast<std::uint32_t>(PageBytes / TileBytes);
    for (std::uint32_t at = FirstTile; at < FirstTile + PageTiles; ++at) std::fill(texels + at * TileWords, texels + (at + 1u) * TileWords, std::bit_cast<std::uint32_t>(TileNew(at)));
    const Expected rewritten = [&](std::uint32_t index) { const auto at = tile(index); return at >= FirstTile && at < FirstTile + PageTiles ? TileNew(at) : TileOld(at); };
    ExpectTexels(SampleAll(address, Standard4KSwizzle, output, kind), rewritten, page, Pages, GridTexel, kind + ", sampled after the CPU rewrote the tiles of its 64 KiB page " + std::to_string(Rewritten));

    constexpr std::uint32_t Touched = 60;
    constexpr float TouchedValue = 9999.5f;
    texels[Touched * TileWords + 0x340u / 4u] = std::bit_cast<std::uint32_t>(TouchedValue);
    const auto touchedPage = static_cast<std::uint32_t>((Misalign + std::uint64_t{Touched} * TileBytes) / PageBytes);
    ExpectOneChanged(SampleAll(address, Standard4KSwizzle, output, kind), rewritten, TouchedValue, page, touchedPage, GridTexel, kind + ", sampled after the CPU stored one word into its 64 KiB page " + std::to_string(touchedPage));
}

float LayerOld(std::uint32_t layer) {
    return 300.5f + static_cast<float>(layer);
}

void LayeredTiledStores() {
    auto* texels = reinterpret_cast<std::uint32_t*>(TestGuest::Allocate(TextureBytes, PageBytes));
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(texels));
    PrivatePages output(static_cast<std::uint32_t>(TextureBytes / PageBytes));
    const UnitOf layer = [](std::uint32_t index) { return index / LayerTexels; };
    for (std::uint32_t at = 0; at < Layers; ++at) std::fill(texels + at * LayerTexels, texels + (at + 1u) * LayerTexels, std::bit_cast<std::uint32_t>(LayerOld(at)));
    const std::string kind = "a 64 KiB tiled 128x128 array of 4 layers";
    ExpectTexels(SampleLayers(address, Standard64KSwizzle, output, kind), [&](std::uint32_t index) { return LayerOld(layer(index)); }, layer, Layers, LayerTexel, kind + ", sampled first");

    constexpr std::uint32_t Rewritten = 2;
    constexpr float RewrittenValue = 4000.5f;
    std::fill(texels + Rewritten * LayerTexels, texels + (Rewritten + 1u) * LayerTexels, std::bit_cast<std::uint32_t>(RewrittenValue));
    const Expected rewritten = [&](std::uint32_t index) { return layer(index) == Rewritten ? RewrittenValue : LayerOld(layer(index)); };
    ExpectTexels(SampleLayers(address, Standard64KSwizzle, output, kind), rewritten, layer, Layers, LayerTexel, kind + ", sampled after the CPU rewrote layer " + std::to_string(Rewritten));

    constexpr std::uint32_t Touched = 3;
    constexpr float TouchedValue = 5555.5f;
    texels[Touched * LayerTexels + 0x1234u / 4u] = std::bit_cast<std::uint32_t>(TouchedValue);
    ExpectOneChanged(SampleLayers(address, Standard64KSwizzle, output, kind), rewritten, TouchedValue, layer, Touched, LayerTexel, kind + ", sampled after the CPU stored one word into layer " + std::to_string(Touched));
}

float LayeredLinear(std::uint32_t index, std::uint32_t generation) {
    return static_cast<float>(generation * 1000000u + index) + 0.25f;
}

void LayeredLinearStores() {
    const auto mips = Graphics::ComputeMipLayout(Graphics::TextureTileMode::kLinear, DeviceTable::Format32Float, LayerSide, LayerSide, 1u);
    const auto layerBytes = Graphics::ComputeSurfaceSize(mips, 1u);
    Require(mips.at(0).pitchBytes == LayerSide * 4u && layerBytes == PageBytes, "sampled unit uploads: a linear 128x128 layer of 32-bit texels is not one 64 KiB unit");
    auto* texels = reinterpret_cast<float*>(TestGuest::Allocate(TextureBytes, PageBytes));
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(texels));
    PrivatePages output(static_cast<std::uint32_t>(TextureBytes / PageBytes));
    const UnitOf layer = [](std::uint32_t index) { return index / LayerTexels; };
    for (std::uint32_t index = 0; index < Layers * LayerTexels; ++index) texels[index] = LayeredLinear(index, 0u);
    const std::string kind = "a linear 128x128 array of 4 layers";
    ExpectTexels(SampleLayers(address, LinearSwizzle, output, kind), [](std::uint32_t index) { return LayeredLinear(index, 0u); }, layer, Layers, LayerTexel, kind + ", sampled first");

    constexpr std::uint32_t Rewritten = 1;
    for (std::uint32_t index = Rewritten * LayerTexels + LayerSide * 8u; index < Rewritten * LayerTexels + LayerSide * 40u; ++index) texels[index] = LayeredLinear(index, 1u);
    const Expected rewritten = [](std::uint32_t index) { return index >= Rewritten * LayerTexels + LayerSide * 8u && index < Rewritten * LayerTexels + LayerSide * 40u ? LayeredLinear(index, 1u) : LayeredLinear(index, 0u); };
    ExpectTexels(SampleLayers(address, LinearSwizzle, output, kind), rewritten, layer, Layers, LayerTexel, kind + ", sampled after the CPU stored rows 8-39 of layer " + std::to_string(Rewritten));
}

}

#endif

int main() {
#ifdef _WIN32
    {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
    }
    for (const auto& [name, run] : std::array<std::pair<const char*, void (*)()>, 5>{{{"linear unit store", LinearUnitStore}, {"tiled unit stores", TiledUnitStores}, {"unaligned 4 KiB tiled unit stores", UnalignedTiledStores}, {"layered tiled unit stores", LayeredTiledStores}, {"layered linear unit stores", LayeredLinearStores}}}) {
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
