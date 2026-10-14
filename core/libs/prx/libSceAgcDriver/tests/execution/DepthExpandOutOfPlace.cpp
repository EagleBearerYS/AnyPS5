#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/DepthSurface.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestTextureResource.hpp"
#include "prx/libSceAgcDriver/Graphics/include/TextureSwizzleEquations.hpp"
#include "prx/libSceAgcDriver/Graphics/include/TextureTiling.hpp"
#include "prx/libSceAgcDriver/Submit/include/Dcb.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "VulkanTestDevice.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace AgcDriver::Graphics;

constexpr std::uint32_t Width = 160;
constexpr std::uint32_t Height = 96;
constexpr std::uint32_t Texels = Width * Height;
constexpr std::uint32_t Threads = 1024;
constexpr std::uint32_t Format32Float = 22;
constexpr std::uint32_t Type2D = 9;
constexpr std::uint32_t SetContextReg = 0x69u;
constexpr std::uint32_t SetShReg = 0x76u;
constexpr std::uint32_t SetUconfigReg = 0x79u;
constexpr std::uint32_t DrawIndexAuto = 0x2du;
constexpr std::uint32_t DispatchDirect = 0x15u;
constexpr std::size_t Block = 65536;
constexpr std::uint32_t CompressDisable = 0x60u;
constexpr std::uint32_t ZInfo = 0x20000183u;
constexpr std::uint32_t StencilInfo = 0x20000181u;
constexpr std::uint32_t ColorDisabled = 0x00cc0000u;
constexpr std::uint32_t TwoSamples = 0x00108001u;
constexpr std::uint32_t TwoSampleLocations = 0x0000cc44u;
constexpr std::uint32_t DepthWriteAlways = 0x76u;
constexpr float XSlope = 2.0f * Width / 65536.0f;
constexpr float YSlope = 512.0f * Height / 65536.0f;

alignas(256) constexpr std::array<std::uint32_t, 6> VertexCode{
    0xe0382000u, 0x80020005u, 0xbf8c3f70u, 0xf80008cfu, 0x03020100u, 0xbf810000u,
};

alignas(256) constexpr std::array<std::uint32_t, 4> PixelCode{
    0x7e0e02f2u, 0xf800180fu, 0x07070707u, 0xbf810000u,
};

constexpr std::array<std::uint32_t, 10> LoadCode{
    0x34020084u, 0xe0381000u, 0x80000201u, 0xbf8c3f70u, 0xf0001f08u, 0x00020602u, 0xbf8c3f70u, 0xe0781000u, 0x80010601u, 0xbf810000u,
};

alignas(256) std::array<std::array<float, 4>, 3> Vertices{{
    {-1.0f, -1.0f, 0.0f, 1.0f}, {3.0f, -1.0f, XSlope, 1.0f}, {-1.0f, 3.0f, YSlope, 1.0f},
}};

struct RegisteredShader {
    Shader shader;
    ShaderUserData userData;
};
RegisteredShader VertexShader{};
RegisteredShader PixelShader{};

std::uint8_t* NewBlock(std::size_t bytes) {
#ifdef _WIN32
    auto* block = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
    auto* block = static_cast<std::uint8_t*>(std::aligned_alloc(Block, bytes));
#endif
    Require(block != nullptr, "depth expand: cannot allocate a guest block");
    std::memset(block, 0, bytes);
    GuestAllocations::Mutation().Add(block, bytes, true, true, true);
    return block;
}

std::uint64_t Address(const void* data) { return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(data)); }

void Register(RegisteredShader& registered, const void* code, std::uint32_t bytes, std::uint8_t type) {
    registered.shader.file_header = 0x34333231u;
    registered.shader.version = 0x18u;
    registered.shader.user_data = &registered.userData;
    registered.shader.code = code;
    registered.shader.header_size = sizeof(RegisteredShader);
    registered.shader.shader_size = bytes;
    registered.shader.type = type;
    AgcDriverRegisterShader_nid_postfix(&registered.shader);
}

void SetRegister(std::vector<std::uint32_t>& words, std::uint32_t opcode, std::uint32_t offset, std::uint32_t value) {
    words.push_back(0xc0010000u | (opcode << 8u));
    words.push_back(offset);
    words.push_back(value);
}

void SetBase(std::vector<std::uint32_t>& words, std::uint32_t low, std::uint32_t high, std::uint64_t address) {
    SetRegister(words, SetContextReg, low, static_cast<std::uint32_t>(address >> 8u));
    SetRegister(words, SetContextReg, high, static_cast<std::uint32_t>(address >> 40u));
}

void Submit(std::vector<std::uint32_t> words, const std::string& what) {
    Packet packet{words.data(), static_cast<std::uint32_t>(words.size()), 0, {}};
    Require(sceAgcDriverSubmitDcb(&packet) == 0, what + ": the DCB submission failed");
    AgcDriverWaitIdle_nid_postfix();
}

std::array<std::uint32_t, 4> BufferDescriptor(std::uint64_t address, std::uint32_t bytes) {
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, 0x31016facu};
}

std::array<std::uint32_t, 8> TextureDescriptor(std::uint64_t address) {
    return {
        static_cast<std::uint32_t>(address >> 8u),
        static_cast<std::uint32_t>((address >> 40u) & 0xffu) | (Format32Float << 20u) | (((Width - 1u) & 3u) << 30u),
        ((Width - 1u) >> 2u) | ((Height - 1u) << 14u),
        0xfacu | ((ZInfo >> 4u) & 0x1fu) << 20u | (Type2D << 28u),
        0u, 0u, 0u, 0u,
    };
}

struct Surfaces {
    std::uint64_t depth;
    std::uint64_t stencil;
    std::uint64_t htile;
    std::uint64_t written;
};

std::vector<std::uint32_t> DepthDraw(const Surfaces& surfaces, bool expand, bool twoSamples) {
    const auto extent = (Height << 16u) | Width;
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> context{
        {0x2d5, 0x2000}, {0x1b6, 0}, {0x203, 0}, {0x2dc, 0xaa00}, {0x293, 0},
        {0x80, 0}, {0x8d, 0}, {0x83, 0xffff}, {0x8c, 0xa}, {0x2f9, 0x2d}, {0x313, 0x6000}, {0x30e, 0xffffffff}, {0x30f, 0xffffffff},
        {0x206, 0x43f}, {0x204, 0x80000}, {0x205, 0x240}, {0x8f, 0}, {0x202, ColorDisabled},
        {0x1c4, 0}, {0x1c5, 0}, {0x1c3, 4}, {0x207, 0}, {0xc, 0}, {0x81, 0x80000000}, {0x90, 0x80000000}, {0x94, 0x80000000},
        {0xb4, 0}, {0xb5, std::bit_cast<std::uint32_t>(1.0f)}, {0x114, 0}, {0x113, std::bit_cast<std::uint32_t>(1.0f)},
        {0x10f, std::bit_cast<std::uint32_t>(Width / 2.0f)}, {0x110, std::bit_cast<std::uint32_t>(Width / 2.0f)},
        {0x111, std::bit_cast<std::uint32_t>(Height / 2.0f)}, {0x112, std::bit_cast<std::uint32_t>(Height / 2.0f)},
        {0xd, extent}, {0x82, extent}, {0x91, extent}, {0x95, extent},
        {0x002, 0}, {0x007, ((Height - 1u) << 16u) | (Width - 1u)}, {0x00a, 0}, {0x00b, std::bit_cast<std::uint32_t>(1.0f)},
        {0x010, ZInfo}, {0x011, StencilInfo}, {0x201, 0}, {0x10b, 0}, {0x10c, 0}, {0x10d, 0},
        {0x000, expand ? CompressDisable : 0u}, {0x200, expand ? 0u : DepthWriteAlways},
        {0x8e, expand ? 0xffffffffu : 0u}, {0x2f8, twoSamples ? TwoSamples : 0u}, {0x292, twoSamples ? 3u : 2u},
    };
    std::vector<std::uint32_t> words;
    for (const auto& [offset, value] : context) SetRegister(words, SetContextReg, offset, value);
    for (const auto offset : {0x2feu, 0x302u, 0x306u, 0x30au}) SetRegister(words, SetContextReg, offset, twoSamples ? TwoSampleLocations : 0u);
    SetBase(words, 0x005, 0x01e, surfaces.htile);
    SetBase(words, 0x012, 0x01a, surfaces.depth);
    SetBase(words, 0x014, 0x01c, expand ? surfaces.written : surfaces.depth);
    SetBase(words, 0x013, 0x01b, surfaces.stencil);
    SetBase(words, 0x015, 0x01d, surfaces.stencil);
    SetRegister(words, SetUconfigReg, 0x242, 4u);
    const auto pixel = expand ? Address(PixelCode.data()) : 0u;
    const auto vertex = Address(VertexCode.data());
    SetRegister(words, SetShReg, 0x008, static_cast<std::uint32_t>(pixel >> 8u));
    SetRegister(words, SetShReg, 0x009, static_cast<std::uint32_t>(pixel >> 40u));
    SetRegister(words, SetShReg, 0x00b, 0u);
    SetRegister(words, SetShReg, 0x0c8, static_cast<std::uint32_t>(vertex >> 8u));
    SetRegister(words, SetShReg, 0x0c9, static_cast<std::uint32_t>(vertex >> 40u));
    SetRegister(words, SetShReg, 0x08b, 4u << 1u);
    const auto address = Address(Vertices.data());
    const std::array<std::uint32_t, 4> descriptor{static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (16u << 16u), static_cast<std::uint32_t>(Vertices.size()), 0x01016facu};
    for (std::uint32_t i = 0; i < descriptor.size(); ++i) SetRegister(words, SetShReg, 0x08c + i, descriptor[i]);
    words.insert(words.end(), {0xc0000000u | (1u << 16u) | (DrawIndexAuto << 8u), static_cast<std::uint32_t>(Vertices.size()), 2u});
    return words;
}

class TexelReader {
public:
    TexelReader() : kernel(NewBlock(Block)), coordinates(NewBlock(Block)), output(NewBlock(Block)) {
        std::memcpy(kernel, LoadCode.data(), sizeof(LoadCode));
    }

    std::vector<std::uint32_t> Read(std::uint64_t surface, const std::string& what) {
        std::vector<std::uint32_t> texels(Texels);
        std::array<std::uint32_t, Threads * 4> words{};
        for (std::uint32_t first = 0; first < Texels; first += Threads) {
            words.fill(0);
            for (std::uint32_t thread = 0; thread < Threads; ++thread) {
                const auto texel = std::min(first + thread, Texels - 1u);
                words[thread * 4u] = texel % Width;
                words[thread * 4u + 1u] = texel / Width;
            }
            std::memcpy(coordinates, words.data(), sizeof(words));
            std::memset(output, 0xff, sizeof(words));
            std::array<std::uint32_t, 16> userData{};
            const auto input = BufferDescriptor(Address(coordinates), sizeof(words));
            const auto stored = BufferDescriptor(Address(output), sizeof(words));
            const auto texture = TextureDescriptor(surface);
            std::copy(input.begin(), input.end(), userData.begin());
            std::copy(stored.begin(), stored.end(), userData.begin() + 4);
            std::copy(texture.begin(), texture.end(), userData.begin() + 8);
            std::vector<std::uint32_t> packet;
            const auto code = Address(kernel);
            SetRegister(packet, SetShReg, 0x20c, static_cast<std::uint32_t>(code >> 8u));
            SetRegister(packet, SetShReg, 0x20d, static_cast<std::uint32_t>(code >> 40u) & 0xffu);
            SetRegister(packet, SetShReg, 0x207, Threads);
            SetRegister(packet, SetShReg, 0x208, 1u);
            SetRegister(packet, SetShReg, 0x209, 1u);
            SetRegister(packet, SetShReg, 0x213, 16u << 1u);
            for (std::uint32_t i = 0; i < userData.size(); ++i) SetRegister(packet, SetShReg, 0x240 + i, userData[i]);
            packet.insert(packet.end(), {0xc0000000u | (3u << 16u) | (DispatchDirect << 8u), 1u, 1u, 1u, 0x41u});
            Submit(std::move(packet), what);
            AgcDriver::GuestMemory::Read(Address(output), std::as_writable_bytes(std::span(words)));
            for (std::uint32_t thread = 0; thread < Threads && first + thread < Texels; ++thread) texels[first + thread] = words[thread * 4u];
        }
        return texels;
    }

private:
    std::uint8_t* kernel;
    std::uint8_t* coordinates;
    std::uint8_t* output;
};

std::uint64_t EquationOffset(const TextureSwizzleEquation& equation, std::uint32_t x, std::uint32_t y) {
    std::uint64_t offset = 0;
    for (std::uint32_t bit = 0; bit < 16; ++bit) {
        const auto mask = equation.bits[bit];
        const auto selected = (x & (mask & 0xfffu)) ^ ((y << 12) & (mask & 0xfff000u));
        offset |= static_cast<std::uint64_t>(std::popcount(selected) & 1u) << bit;
    }
    return offset;
}

std::vector<std::uint32_t> DetileRaw(std::uint64_t surface, std::span<const std::byte> raw) {
    const auto descriptor = DecodeTextureResource(TextureDescriptor(surface));
    const auto geometry = DescribeSurface(descriptor);
    Require(geometry.guestBytes == raw.size(), "depth expand: the R32F view of the plane is not the size of the depth plane");
    const auto* equation = FindTextureSwizzleEquation(XorSwizzleMode(descriptor.tileMode), 4u);
    Require(equation != nullptr, "depth expand: the SW_64KB_Z_X equation for 32-bit texels is missing");
    const auto& mip = geometry.mips.at(0);
    const auto layout = ThinBlockLayout(descriptor.tileMode, 4u);
    std::vector<std::uint32_t> texels(Texels);
    for (std::uint32_t y = 0; y < Height; ++y) {
        for (std::uint32_t x = 0; x < Width; ++x) {
            auto offset = mip.tiledOffset;
            if (mip.tail) offset += EquationOffset(*equation, x + mip.tailX, y + mip.tailY);
            else offset += (static_cast<std::uint64_t>(y / layout[2]) * mip.blocksPerRow + x / layout[1]) * layout[0] + EquationOffset(*equation, x, y);
            std::memcpy(&texels[y * Width + x], raw.data() + offset, sizeof(std::uint32_t));
        }
    }
    return texels;
}

float Expected(std::uint32_t x, std::uint32_t y) {
    return (static_cast<float>(x) + 0.5f + 256.0f * (static_cast<float>(y) + 0.5f)) / 65536.0f;
}

std::string Hex(std::uint32_t value) {
    char text[48];
    std::snprintf(text, sizeof(text), "0x%08x (%.9g)", value, static_cast<double>(std::bit_cast<float>(value)));
    return text;
}

std::uint32_t Compare(const std::vector<std::uint32_t>& actual, const std::vector<std::uint32_t>& expected, const char* what) {
    std::uint32_t matching = 0;
    std::uint32_t first = Texels;
    for (std::uint32_t texel = 0; texel < Texels; ++texel) {
        if (actual[texel] == expected[texel]) ++matching;
        else if (first == Texels) first = texel;
    }
    std::printf("%s: %u of %u texels hold the read surface's depth", what, matching, Texels);
    if (first != Texels) std::printf("; first difference at (%u, %u): %s, expected %s", first % Width, first / Width, Hex(actual[first]).c_str(), Hex(expected[first]).c_str());
    std::printf("\n");
    return matching;
}

bool Run(TexelReader& reader, bool twoSamples) {
    const auto name = std::string(twoSamples ? "the game's expand, two raster samples" : "the expand with one raster sample");
    const auto planeBytes = static_cast<std::size_t>(DepthSliceBytes({Width, Height}, 4u));
    const Surfaces surfaces{
        Address(NewBlock(planeBytes)),
        Address(NewBlock(static_cast<std::size_t>(DepthSliceBytes({Width, Height}, 1u)))),
        Address(NewBlock(Block)),
        Address(NewBlock(planeBytes)),
    };
    std::printf("%s: read base 0x%llx, write base 0x%llx\n", name.c_str(), static_cast<unsigned long long>(surfaces.depth), static_cast<unsigned long long>(surfaces.written));
    Submit(DepthDraw(surfaces, false, false), name + ": depth draw at the read base");
    Submit(DepthDraw(surfaces, true, twoSamples), name + ": DB expand from the read base to the write base");
    const auto depth = reader.Read(surfaces.depth, name + ": texture read of the read base");
    std::uint32_t drawn = 0;
    for (std::uint32_t texel = 0; texel < Texels; ++texel) {
        if (std::fabs(std::bit_cast<float>(depth[texel]) - Expected(texel % Width, texel / Width)) <= 4e-6f) ++drawn;
    }
    std::printf("  read base: %u of %u texels hold the drawn depth gradient; texel (0, 0) %s, (%u, %u) %s\n", drawn, Texels, Hex(depth.front()).c_str(), Width - 1u, Height - 1u, Hex(depth.back()).c_str());
    Require(drawn == Texels, name + ": the depth draw did not write its gradient at the read base");
    const auto sampled = Compare(reader.Read(surfaces.written, name + ": texture read of the write base"), depth, "  texture read of the write base (R32F, SW_64KB_Z_X)");
    std::vector<std::byte> raw(planeBytes);
    AgcDriver::GuestMemory::Read(surfaces.written, raw);
    const auto stored = Compare(DetileRaw(surfaces.written, raw), depth, "  raw read of the write base after a flush, detiled with the SW_64KB_Z_X equation");
    return sampled == Texels && stored == Texels;
}

}

int main() {
    try {
        if (!OpenVulkanTestDevice()) return VulkanTestSkipped;
        Register(VertexShader, VertexCode.data(), sizeof(VertexCode), 2);
        Register(PixelShader, PixelCode.data(), sizeof(PixelCode), 1);
        TexelReader reader;
        Require(Run(reader, false), "depth expand: the write base does not hold the read surface's depth after the DB expand with one raster sample");
        Require(Run(reader, true), "depth expand: the write base does not hold the read surface's depth after the game's DB expand with two raster samples");
        AgcDriverShutdown_nid_postfix();
        std::puts("out-of-place depth expand test passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        AgcDriverShutdown_nid_postfix();
        return 1;
    }
}
