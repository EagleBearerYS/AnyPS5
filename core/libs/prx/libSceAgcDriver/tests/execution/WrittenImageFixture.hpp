#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_TESTS_EXECUTION_WRITTENIMAGEFIXTURE_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_TESTS_EXECUTION_WRITTENIMAGEFIXTURE_HPP

#include "DeviceTableFixture.hpp"
#include <cstring>

namespace WrittenImages {

using namespace DeviceTable;
using NewPath::Commands;

constexpr std::uint32_t Textures = 4;
constexpr std::uint32_t TextureWords = Width * Height;
constexpr std::uint32_t MaxDispatches = 64;
constexpr std::uint32_t DispatchWords = Height * Lanes * LaneWords;
constexpr std::uint32_t SamplerWord = 4;
constexpr std::uint32_t GroupSgpr = 8;

inline TestGuest::Array<std::uint32_t, Textures * TextureWords> Texels;
inline TestGuest::Array<std::uint32_t, Textures * 8> Descriptors;
inline TestGuest::Array<std::uint32_t, 32> Root;
inline TestGuest::Array<std::uint32_t, MaxDispatches * DispatchWords> Results;

namespace Ops {

using namespace DeviceTable::Asm;

constexpr std::uint32_t SLoadDwordX2 = 0x01u;
constexpr std::uint32_t SLoadDwordX8 = 0x03u;
constexpr std::uint32_t GroupOffset = 0x8f108508u;

constexpr std::array<std::uint32_t, 2> SmemRegisterOffset(std::uint32_t opcode, std::uint32_t sdata, std::uint32_t sbase, std::uint32_t soffset, std::uint32_t offset) {
    return {0xf4000000u | (opcode << 18u) | (sdata << 6u) | (sbase / 2u), (soffset << 25u) | (offset & 0x1fffffu)};
}

}

inline std::uint32_t Texel(std::uint32_t texture, std::uint32_t x, std::uint32_t y) {
    return (0x20u + x + 37u * texture) | ((0x10u + 50u * y + 9u * texture) << 8u) | ((0xf0u - 3u * x) << 16u) | ((0x40u + 40u * texture + y) << 24u);
}

inline const void* TextureData(std::uint32_t texture) {
    return Texels.data() + texture * TextureWords;
}

inline std::array<std::uint32_t, 8> TextureDescriptor(std::uint32_t texture) {
    const auto words = Image(TextureData(texture), Format8_8_8_8UNorm);
    return {words[0], words[1], words[2], words[3], 0u, 0u, 0u, 0u};
}

inline void FillTextures() {
    for (std::uint32_t texture = 0; texture < Textures; ++texture) {
        for (std::uint32_t y = 0; y < Height; ++y) {
            for (std::uint32_t x = 0; x < Width; ++x) Texels[texture * TextureWords + y * Width + x] = Texel(texture, x, y);
        }
    }
    Descriptors.fill(0u);
    Root.fill(0u);
    const auto table = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(Descriptors.data()));
    Root[0] = static_cast<std::uint32_t>(table);
    Root[1] = static_cast<std::uint32_t>(table >> 32u);
    std::copy(PointClamp.begin(), PointClamp.end(), Root.begin() + SamplerWord);
    std::fill(Results.begin(), Results.end(), Untouched);
}

inline void WriteDescriptor(std::uint32_t slot, std::uint32_t texture) {
    const auto words = TextureDescriptor(texture);
    std::copy(words.begin(), words.end(), Descriptors.begin() + slot * 8u);
}

inline const Program& WriterProgram() {
    static const Program program = [] {
        using namespace Ops;
        Program built;
        built << Vop1(VMovB32, 8, InlineInteger(0));
        for (std::uint32_t word = 0; word < 4u; ++word) built << Vop1(VMovB32, 4 + word, 4 + word);
        built << Mubuf(BufferStoreDwordX4, 4, 8, 0, false, 0);
        for (std::uint32_t word = 0; word < 4u; ++word) built << Vop1(VMovB32, 4 + word, 8 + word);
        built << Mubuf(BufferStoreDwordX4, 4, 8, 0, false, 16)
              << EndProgram;
        return built;
    }();
    return program;
}

inline void Sample(Program& built) {
    using namespace Ops;
    TexelCenter(built, GroupSgpr);
    built << Mimg(ImageSampleLz, 0xfu, false, 4, 1, 36, 32);
    StoreLane(built, GroupSgpr, 4);
}

inline const Program& ChainProgram() {
    static const Program program = [] {
        using namespace Ops;
        Program built;
        built << Smem(SLoadDwordX2, 20, 0, 0)
              << Smem(SLoadDwordX4, 32, 0, SamplerWord * 4u)
              << WaitAll
              << Smem(SLoadDwordX8, 36, 20, 0);
        Sample(built);
        return built;
    }();
    return program;
}

inline const Program& GroupProgram() {
    static const Program program = [] {
        using namespace Ops;
        Program built;
        built << GroupOffset
              << Smem(SLoadDwordX2, 20, 0, 0)
              << Smem(SLoadDwordX4, 32, 0, SamplerWord * 4u)
              << WaitAll
              << SmemRegisterOffset(SLoadDwordX8, 36, 20, 16, 0);
        Sample(built);
        return built;
    }();
    return program;
}

inline std::array<std::uint32_t, 12> WriterUserData(std::uint32_t texture) {
    std::array<std::uint32_t, 12> userData{};
    const auto table = Buffer(Descriptors.data(), static_cast<std::uint32_t>(decltype(Descriptors)::ByteSize));
    const auto words = TextureDescriptor(texture);
    std::copy(table.begin(), table.end(), userData.begin());
    std::copy(words.begin(), words.end(), userData.begin() + 4);
    return userData;
}

inline std::array<std::uint32_t, 8> ReaderUserData(std::uint32_t dispatch) {
    std::array<std::uint32_t, 8> userData{};
    const auto root = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(Root.data()));
    userData[0] = static_cast<std::uint32_t>(root);
    userData[1] = static_cast<std::uint32_t>(root >> 32u);
    const auto output = Buffer(Results.data() + dispatch * DispatchWords, DispatchWords * 4u);
    std::copy(output.begin(), output.end(), userData.begin() + 4);
    return userData;
}

inline void ExpectSampled(std::uint32_t dispatch, const std::function<std::uint32_t(std::uint32_t row)>& texture, const std::string& what) {
    const auto* words = Results.data() + dispatch * DispatchWords;
    AgcDriver::GuestMemory::FlushGpuWrites(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(words)), DispatchWords * 4u);
    std::size_t wrong = 0;
    std::size_t zeros = 0;
    std::size_t untouched = 0;
    std::string first;
    for (std::uint32_t row = 0; row < Height; ++row) {
        for (std::uint32_t x = 0; x < Lanes; ++x) {
            for (std::uint32_t component = 0; component < LaneWords; ++component) {
                const auto word = words[(row * Lanes + x) * LaneWords + component];
                if (SameUnorm(word, (Texel(texture(row), x, row) >> (8u * component)) & 0xffu)) continue;
                zeros += word == 0u ? 1u : 0u;
                untouched += word == Untouched ? 1u : 0u;
                if (wrong++ == 0u) first = "row " + std::to_string(row) + " lane " + std::to_string(x) + " component " + std::to_string(component) + " is " + Hex(word);
            }
        }
    }
    Expect(wrong == 0u, what + ": " + std::to_string(wrong) + " of " + std::to_string(DispatchWords) + " words differ from the texels (" + std::to_string(zeros) + " zero, " + std::to_string(untouched) + " untouched), first " + first);
}

inline void ExpectDescriptor(std::uint32_t slot, std::uint32_t texture, const std::string& what) {
    AgcDriver::GuestMemory::FlushGpuWrites(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(Descriptors.data() + slot * 8u)), 32u);
    const auto words = TextureDescriptor(texture);
    for (std::uint32_t word = 0; word < 8u; ++word) {
        if (Descriptors[slot * 8u + word] == words[word]) continue;
        Expect(false, what + ": T# word " + std::to_string(word) + " in guest memory is " + Hex(Descriptors[slot * 8u + word]) + ", expected " + Hex(words[word]));
        return;
    }
}

inline int Main(const char* name, void (*run)()) {
    {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
    }
    try {
        FillTextures();
        run();
    } catch (const std::exception& error) {
        Expect(false, std::string("exception: ") + error.what());
    }
    try {
        AgcDriverShutdown_nid_postfix();
    } catch (const std::exception& error) {
        Expect(false, std::string("shutdown: ") + error.what());
    }
    return Finish(name);
}

}

#endif
