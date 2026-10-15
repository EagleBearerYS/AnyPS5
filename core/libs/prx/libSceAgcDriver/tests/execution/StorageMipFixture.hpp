#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_TESTS_EXECUTION_STORAGEMIPFIXTURE_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_TESTS_EXECUTION_STORAGEMIPFIXTURE_HPP

#include "DeviceTableFixture.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestTextureResource.hpp"
#include "prx/libSceAgcDriver/Graphics/include/TextureTiling.hpp"
#include <map>
#include <tuple>

namespace StorageMips {

using namespace DeviceTable;
using AgcDriver::Graphics::Require;

constexpr std::uint32_t Side = 8;
constexpr std::uint32_t Levels = 4;
constexpr std::uint32_t Groups = 2;
constexpr std::uint32_t Entries = Groups * Lanes;
constexpr std::uint32_t FirstSlot = 0;
constexpr std::uint32_t SecondSlot = 8;
constexpr std::uint32_t InputSlot = 16;
constexpr std::uint32_t SelectSlot = 20;
constexpr std::uint32_t SeedSlot = 21;
constexpr std::uint32_t RootWords = 32;
constexpr std::size_t TextureBytes = 65536;
constexpr std::uint32_t Marker = 0x0bad0000u;

namespace Ops {

using namespace DeviceTable::Asm;

constexpr std::uint32_t SLoadDword = 0x00u;
constexpr std::uint32_t SLoadDwordX8 = 0x03u;
constexpr std::uint32_t VCndmaskB32 = 0x01u;
constexpr std::uint32_t BufferLoadDwordX4 = 0x0eu;
constexpr std::uint32_t ImageStoreMip = 0x09u;
constexpr std::uint32_t CmpGreater = 0x7d880000u;

}

inline const Program& StoreMipProgram() {
    static const Program program = [] {
        using namespace Ops;
        Program built;
        built << Smem(SLoadDwordX8, 40, 0, FirstSlot * 4u)
              << Smem(SLoadDwordX8, 48, 0, SecondSlot * 4u)
              << Smem(SLoadDwordX4, 24, 0, InputSlot * 4u)
              << Smem(SLoadDword, 20, 0, SelectSlot * 4u)
              << Smem(SLoadDword, 21, 0, SeedSlot * 4u)
              << WaitAll
              << (CmpGreater | 20u);
        for (std::uint32_t word = 0; word < 8u; ++word) {
            built << Vop1(VMovB32, 10, 40 + word)
                  << Vop2(VCndmaskB32, 10, 48 + word, 10)
                  << Vop1(VReadFirstLaneB32, 56 + word, Vgpr(10));
        }
        built << Vop1(VMovB32, 9, 2)
              << Vop2(VLshlRevB32, 9, InlineInteger(6), 9)
              << Vop2(VAddNcU32, 9, Vgpr(0), 9)
              << Vop2(VLshlRevB32, 9, InlineInteger(4), 9)
              << Mubuf(BufferLoadDwordX4, 1, 9, 24)
              << WaitAll
              << Vop2(VAddNcU32, 4, 21, 4)
              << Mimg(ImageStoreMip, 1u, false, 4, 1, 56, 0, true)
              << EndProgram;
        return built;
    }();
    return program;
}

struct Texture {
    std::uint32_t index;
    std::uint32_t* words;
};

inline std::array<std::uint32_t, 8> Descriptor(const Texture& texture, std::uint32_t base, std::uint32_t last) {
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(texture.words));
    return {
        static_cast<std::uint32_t>(address >> 8u),
        static_cast<std::uint32_t>((address >> 40u) & 0xffu) | (Format32UInt << 20u) | (((Side - 1u) & 3u) << 30u),
        ((Side - 1u) >> 2u) | ((Side - 1u) << 14u) | (1u << 31u),
        0xfacu | (base << 12u) | (last << 16u) | (Type2D << 28u),
        0u,
        (Levels - 1u) << 4u,
        0u,
        0u,
    };
}

inline const AgcDriver::Graphics::SurfaceGeometry& Surface(const Texture* texture = nullptr) {
    static const AgcDriver::Graphics::SurfaceGeometry surface = [texture] {
        Require(texture != nullptr, "storage mips: the surface is described before any texture exists");
        const auto geometry = AgcDriver::Graphics::DescribeSurface(AgcDriver::Graphics::DecodeTextureResource(Descriptor(*texture, 0, Levels - 1u)));
        Require(geometry.mips.size() == Levels && geometry.guestBytes <= TextureBytes, "storage mips: the surface does not have four levels within its allocation");
        return geometry;
    }();
    return surface;
}

inline std::uint32_t LevelSide(std::uint32_t level) {
    return std::max(Side >> level, 1u);
}

inline std::uint32_t& At(const Texture& texture, std::uint32_t level, std::uint32_t x, std::uint32_t y) {
    const auto& mip = Surface().mips.at(level);
    return *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(texture.words) + mip.tiledOffset + y * mip.pitchBytes + x * 4u);
}

inline std::uint32_t Value(const Texture& texture, std::uint32_t level, std::uint32_t x, std::uint32_t y) {
    return (texture.index << 28u) | (level << 24u) | (y << 12u) | x;
}

using Model = std::map<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>, std::uint32_t>;

struct View {
    const Texture* texture;
    std::uint32_t base;
    std::uint32_t last;
};

class Dispatch {
public:
    Dispatch(const View& firstView, const View& secondView, bool selectFirst, std::uint32_t seed)
        : root(reinterpret_cast<std::uint32_t*>(TestGuest::Allocate(RootWords * 4u, 256))), input(reinterpret_cast<std::uint32_t*>(TestGuest::Allocate(Entries * 16u, 256))) {
        std::fill(root, root + RootWords, 0u);
        const auto firstWords = Descriptor(*firstView.texture, firstView.base, firstView.last);
        const auto secondWords = Descriptor(*secondView.texture, secondView.base, secondView.last);
        std::copy(firstWords.begin(), firstWords.end(), root + FirstSlot);
        std::copy(secondWords.begin(), secondWords.end(), root + SecondSlot);
        const auto buffer = Buffer(input, Entries * 16u);
        std::copy(buffer.begin(), buffer.end(), root + InputSlot);
        root[SelectSlot] = selectFirst ? Lanes : 0u;
        root[SeedSlot] = seed;
        const auto& selected = selectFirst ? firstView : secondView;
        const auto base = selected.base;
        const auto last = selected.last;
        std::uint32_t entry = 0;
        const auto put = [&](std::uint32_t x, std::uint32_t y, std::uint32_t mip, std::uint32_t value) {
            Require(entry < Entries, "storage mips: too many entries for one dispatch");
            input[entry * 4u + 0u] = x;
            input[entry * 4u + 1u] = y;
            input[entry * 4u + 2u] = mip;
            input[entry * 4u + 3u] = value;
            ++entry;
        };
        const auto range = last >= base ? last - base + 1u : 0u;
        for (std::uint32_t mip = 0; mip < range; ++mip) {
            const auto level = base + mip;
            for (std::uint32_t y = 0; y < LevelSide(level); ++y) {
                for (std::uint32_t x = 0; x < LevelSide(level); ++x) {
                    put(x, y, mip, Value(*selected.texture, level, x, y));
                    written[{level, x, y}] = Value(*selected.texture, level, x, y) + seed;
                }
            }
        }
        for (std::uint32_t index = 0; entry < Entries; ++index) put(index % 2u, (index / 2u) % 2u, range + index % 3u, Marker + index);
    }

    std::array<std::uint32_t, 2> UserData() const {
        const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(root));
        return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>(address >> 32u)};
    }

    void Apply(Model& model) const {
        for (const auto& [key, value] : written) model[key] = value;
    }

private:
    std::uint32_t* root;
    std::uint32_t* input;
    Model written;
};

inline Texture NewTexture(std::uint32_t index) {
    Texture texture{index, reinterpret_cast<std::uint32_t*>(TestGuest::Allocate(TextureBytes, TextureBytes))};
    std::fill(texture.words, texture.words + TextureBytes / 4u, Untouched);
    return texture;
}

inline void ExpectTexture(const Texture& texture, const Model& model, const std::string& what) {
    AgcDriver::GuestMemory::FlushGpuWrites(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(texture.words)), Surface().guestBytes);
    std::size_t wrong = 0;
    std::string first;
    for (std::uint32_t level = 0; level < Levels; ++level) {
        for (std::uint32_t y = 0; y < LevelSide(level); ++y) {
            for (std::uint32_t x = 0; x < LevelSide(level); ++x) {
                const auto found = model.find({level, x, y});
                const auto expected = found != model.end() ? found->second : Untouched;
                const auto actual = At(texture, level, x, y);
                if (actual == expected) continue;
                if (wrong++ == 0u) first = "level " + std::to_string(level) + " texel (" + std::to_string(x) + ", " + std::to_string(y) + ") is " + Hex(actual) + ", expected " + Hex(expected);
            }
        }
    }
    Expect(wrong == 0u, what + ": " + std::to_string(wrong) + " texels differ, first " + first);
}

inline Compiled CompileFor(AgcDriver::VulkanDevice& device, const Dispatch& dispatch, const std::string& what) {
    const auto userData = dispatch.UserData();
    auto compiled = Compile(device, StoreMipProgram(), userData);
    const auto& plan = *compiled.capture->plan;
    const bool dynamic = plan.info.images.size() == 1u && plan.info.images[0].deviceTable && plan.info.images[0].mipMode == ShaderRecompiler::ImageMipMode::DynamicStorage;
    Expect(plan.srtReads.empty() && dynamic, what + ": the plan reads " + std::to_string(plan.srtReads.size()) + " SRT words in the capture, or the storage image does not go through the device image table with dynamic mips");
    return compiled;
}

inline std::string FirstLine(const std::string& text) {
    return text.substr(0, text.find('\n'));
}

inline Outcome RunOne(AgcDriver::VulkanDevice& device, const Compiled& compiled) {
    AgcDriver::GuestMemory::BumpCollectEpoch();
    return DeviceTable::Run(device, compiled, Groups);
}

}

#endif
