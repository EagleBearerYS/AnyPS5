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
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
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
constexpr std::uint32_t SetIndirectBase = 0x11u;
constexpr std::uint32_t IndexBufferSize = 0x13u;
constexpr std::uint32_t DrawIndirect = 0x24u;
constexpr std::uint32_t DrawIndexIndirect = 0x25u;
constexpr std::uint32_t IndexBase = 0x26u;
constexpr std::uint32_t IndexType = 0x2au;
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
// DB_DEPTH_CLEAR of the read surface whose HTILE the title cleared, and what the CPU wrote where no depth was drawn.
constexpr float ClearDepth = 0.375f;
constexpr std::uint32_t CpuWritten = 0x3f000000u;

alignas(256) constexpr std::array<std::uint32_t, 6> VertexCode{
    0xe0382000u, 0x80020005u, 0xbf8c3f70u, 0xf80008cfu, 0x03020100u, 0xbf810000u,
};

alignas(256) constexpr std::array<std::uint32_t, 4> PixelCode{
    0x7e0e02f2u, 0xf800180fu, 0x07070707u, 0xbf810000u,
};

constexpr std::array<std::uint32_t, 10> LoadCode{
    0x34020084u, 0xe0381000u, 0x80000201u, 0xbf8c3f70u, 0xf0001f08u, 0x00020602u, 0xbf8c3f70u, 0xe0781000u, 0x80010601u, 0xbf810000u,
};

// A raw copy of a buffer, one dword a thread, as the title's copy shader reads the write base: v1 = (group << 10 | thread) * 4,
// the group index in s16 after the 16 user SGPRs, buffer_load_dword from the V# in s[0:3] and buffer_store_dword to the V#
// in s[4:7].
constexpr std::array<std::uint32_t, 9> BufferCopyCode{
    0xd7460001u, 0x04011410u, 0x34020282u, 0xe0301000u, 0x80000201u, 0xbf8c3f70u, 0xe0701000u, 0x80010201u, 0xbf810000u,
};

// The same copy plus a global_load_dword of the zero dword at s[8:9], added to each copied dword: a global access makes the
// program address-based, so its build maps the registered heaps whole instead of binding the V# ranges.
constexpr std::array<std::uint32_t, 13> AddressBasedCopyCode{
    0xd7460001u, 0x04011410u, 0x34020282u, 0xe0301000u, 0x80000201u, 0x7e060280u, 0xdc308000u, 0x03080003u, 0xbf8c3f70u, 0x4a040702u, 0xe0701000u, 0x80010201u, 0xbf810000u,
};

// RADV's HTILE update (one 16-byte run of HTILE words a thread, the group index in s6): each word becomes (word & s5) | s4. The
// driver notes it as a depth fast clear when s4 and s5 leave ZMASK zero (HtileDepthClearAddress).
constexpr std::array<std::uint32_t, 17> HtileClearCode{
    0xd7460000u, 0x04010c06u, 0x34000084u, 0xdc388000u, 0x04020000u, 0xbf8c3f70u, 0xd7710008u, 0x00120805u, 0xd7710009u,
    0x00120a05u, 0xd771000au, 0x00120c05u, 0xd771000bu, 0x00120e05u, 0xdc788000u, 0x00020800u, 0xbf810000u,
};

using Triangle = std::array<std::array<float, 4>, 3>;

alignas(256) Triangle Vertices{{
    {-1.0f, -1.0f, 0.0f, 1.0f}, {3.0f, -1.0f, XSlope, 1.0f}, {-1.0f, 3.0f, YSlope, 1.0f},
}};

// The same gradient 0.25 deeper.
constexpr float Raise = 0.25f;
alignas(256) Triangle RaisedVertices{{
    {-1.0f, -1.0f, Raise, 1.0f}, {3.0f, -1.0f, Raise + XSlope, 1.0f}, {-1.0f, 3.0f, Raise + YSlope, 1.0f},
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

std::size_t PlaneBytes() { return static_cast<std::size_t>(DepthSliceBytes({Width, Height}, 4u)); }

void Fill(std::uint64_t address, std::size_t bytes, std::uint32_t value) {
    const std::vector<std::uint32_t> words(bytes / sizeof(std::uint32_t), value);
    std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)), words.data(), bytes);
}

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

// A compute dispatch of `groups` groups of `threads` threads; with `groupIndex` the group index follows the user SGPRs
// (COMPUTE_PGM_RSRC2.TGID_X_EN).
void Dispatch(const void* code, std::span<const std::uint32_t> userData, std::uint32_t threads, std::uint32_t groups, bool groupIndex, const std::string& what) {
    std::vector<std::uint32_t> packet;
    const auto address = Address(code);
    SetRegister(packet, SetShReg, 0x20c, static_cast<std::uint32_t>(address >> 8u));
    SetRegister(packet, SetShReg, 0x20d, static_cast<std::uint32_t>(address >> 40u) & 0xffu);
    SetRegister(packet, SetShReg, 0x207, threads);
    SetRegister(packet, SetShReg, 0x208, 1u);
    SetRegister(packet, SetShReg, 0x209, 1u);
    SetRegister(packet, SetShReg, 0x213, (static_cast<std::uint32_t>(userData.size()) << 1u) | (groupIndex ? 1u << 7u : 0u));
    for (std::uint32_t i = 0; i < userData.size(); ++i) SetRegister(packet, SetShReg, 0x240 + i, userData[i]);
    packet.insert(packet.end(), {0xc0000000u | (3u << 16u) | (DispatchDirect << 8u), groups, 1u, 1u, 0x41u});
    Submit(std::move(packet), what);
}

struct Surfaces {
    std::uint64_t depth;
    std::uint64_t stencil;
    std::uint64_t htile;
    std::uint64_t written;
};

Surfaces NewSurfaces() {
    return {
        Address(NewBlock(PlaneBytes())),
        Address(NewBlock(static_cast<std::size_t>(DepthSliceBytes({Width, Height}, 1u)))),
        Address(NewBlock(Block)),
        Address(NewBlock(PlaneBytes())),
    };
}

// How a draw is issued: DRAW_INDEX_AUTO, or DRAW_INDIRECT or DRAW_INDEX_INDIRECT with its record at offset 0 of the draw
// indirect base (SET_BASE) and, for the indexed form, three 32-bit indices.
struct Launch {
    std::uint32_t opcode = DrawIndexAuto;
    std::uint64_t record = 0;
    std::uint64_t indices = 0;
};

std::vector<std::uint32_t> DepthDraw(const Surfaces& surfaces, bool expand, bool twoSamples, const Launch& launch = {}, float clearDepth = 1.0f, const Triangle& vertices = Vertices) {
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
        {0x002, 0}, {0x007, ((Height - 1u) << 16u) | (Width - 1u)}, {0x00a, 0}, {0x00b, std::bit_cast<std::uint32_t>(clearDepth)},
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
    const auto address = Address(vertices.data());
    const std::array<std::uint32_t, 4> descriptor{static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (16u << 16u), static_cast<std::uint32_t>(vertices.size()), 0x01016facu};
    for (std::uint32_t i = 0; i < descriptor.size(); ++i) SetRegister(words, SetShReg, 0x08c + i, descriptor[i]);
    if (launch.opcode == DrawIndexAuto) {
        words.insert(words.end(), {0xc0000000u | (1u << 16u) | (DrawIndexAuto << 8u), static_cast<std::uint32_t>(Vertices.size()), 2u});
        return words;
    }
    words.insert(words.end(), {0xc0000000u | (2u << 16u) | (SetIndirectBase << 8u), 1u, static_cast<std::uint32_t>(launch.record), static_cast<std::uint32_t>(launch.record >> 32u)});
    if (launch.opcode == DrawIndexIndirect) {
        words.insert(words.end(), {0xc0000000u | (IndexType << 8u), 1u});
        words.insert(words.end(), {0xc0000000u | (1u << 16u) | (IndexBase << 8u), static_cast<std::uint32_t>(launch.indices), static_cast<std::uint32_t>(launch.indices >> 32u)});
        words.insert(words.end(), {0xc0000000u | (IndexBufferSize << 8u), static_cast<std::uint32_t>(Vertices.size())});
    }
    words.insert(words.end(), {0xc0000000u | (3u << 16u) | (launch.opcode << 8u), 0u, 0u, 0u, launch.opcode == DrawIndirect ? 2u : 0u});
    return words;
}

class TexelReader {
public:
    TexelReader()
        : kernel(NewBlock(Block)), copyKernel(NewBlock(Block)), addressBasedCopyKernel(NewBlock(Block)), htileKernel(NewBlock(Block)), coordinates(NewBlock(Block)), output(NewBlock(Block)), zero(NewBlock(Block)) {
        std::memcpy(kernel, LoadCode.data(), sizeof(LoadCode));
        std::memcpy(copyKernel, BufferCopyCode.data(), sizeof(BufferCopyCode));
        std::memcpy(addressBasedCopyKernel, AddressBasedCopyCode.data(), sizeof(AddressBasedCopyCode));
        std::memcpy(htileKernel, HtileClearCode.data(), sizeof(HtileClearCode));
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
            Dispatch(kernel, userData, Threads, 1u, false, what);
            AgcDriver::GuestMemory::Read(Address(output), std::as_writable_bytes(std::span(words)));
            for (std::uint32_t thread = 0; thread < Threads && first + thread < Texels; ++thread) texels[first + thread] = words[thread * 4u];
        }
        return texels;
    }

    // Copies `bytes` (a multiple of 4 KiB) at `source` to `destination` with raw buffer loads and stores in one dispatch; with
    // `addressBased` the program also makes a global access.
    void CopyBuffer(std::uint64_t source, std::uint64_t destination, std::uint32_t bytes, bool addressBased, const std::string& what) {
        std::array<std::uint32_t, 16> userData{};
        const auto read = BufferDescriptor(source, bytes);
        const auto written = BufferDescriptor(destination, bytes);
        std::copy(read.begin(), read.end(), userData.begin());
        std::copy(written.begin(), written.end(), userData.begin() + 4);
        userData[8] = static_cast<std::uint32_t>(Address(zero));
        userData[9] = static_cast<std::uint32_t>(Address(zero) >> 32u);
        Dispatch(addressBased ? addressBasedCopyKernel : copyKernel, userData, Threads, bytes / 4u / Threads, true, what);
    }

    // Clears ZMASK in every HTILE word of [htile, htile + bytes), as the title clears a depth surface's HTILE.
    void ClearHtile(std::uint64_t htile, std::uint32_t bytes, const std::string& what) {
        const std::array<std::uint32_t, 6> userData{0u, 0u, static_cast<std::uint32_t>(htile), static_cast<std::uint32_t>(htile >> 32u), 0u, 0x00000ff0u};
        Dispatch(htileKernel, userData, 64u, bytes / 1024u, true, what);
    }

private:
    std::uint8_t* kernel;
    std::uint8_t* copyKernel;
    std::uint8_t* addressBasedCopyKernel;
    std::uint8_t* htileKernel;
    std::uint8_t* coordinates;
    std::uint8_t* output;
    std::uint8_t* zero;
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

// The byte offset of every texel of the R32F view of a plane in its `bytes` of guest memory (the SW_64KB_Z_X equation for
// 32-bit texels).
std::vector<std::uint64_t> TexelOffsets(std::uint64_t surface, std::size_t bytes) {
    const auto descriptor = DecodeTextureResource(TextureDescriptor(surface));
    const auto geometry = DescribeSurface(descriptor);
    Require(geometry.guestBytes == bytes, "depth expand: the R32F view of the plane is not the size of the depth plane");
    const auto* equation = FindTextureSwizzleEquation(XorSwizzleMode(descriptor.tileMode), 4u);
    Require(equation != nullptr, "depth expand: the SW_64KB_Z_X equation for 32-bit texels is missing");
    const auto& mip = geometry.mips.at(0);
    const auto layout = ThinBlockLayout(descriptor.tileMode, 4u);
    std::vector<std::uint64_t> offsets(Texels);
    for (std::uint32_t y = 0; y < Height; ++y) {
        for (std::uint32_t x = 0; x < Width; ++x) {
            auto offset = mip.tiledOffset;
            if (mip.tail) offset += EquationOffset(*equation, x + mip.tailX, y + mip.tailY);
            else offset += (static_cast<std::uint64_t>(y / layout[2]) * mip.blocksPerRow + x / layout[1]) * layout[0] + EquationOffset(*equation, x, y);
            offsets[y * Width + x] = offset;
        }
    }
    return offsets;
}

std::vector<std::uint32_t> DetileRaw(std::uint64_t surface, std::span<const std::byte> raw) {
    const auto offsets = TexelOffsets(surface, raw.size());
    std::vector<std::uint32_t> texels(Texels);
    for (std::uint32_t texel = 0; texel < Texels; ++texel) std::memcpy(&texels[texel], raw.data() + offsets[texel], sizeof(std::uint32_t));
    return texels;
}

// Writes `texels` into the plane at `surface` through the CPU, tiled as the DB stores it.
void StoreTiled(std::uint64_t surface, const std::vector<std::uint32_t>& texels) {
    const auto offsets = TexelOffsets(surface, PlaneBytes());
    auto* plane = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(surface));
    for (std::uint32_t texel = 0; texel < Texels; ++texel) std::memcpy(plane + offsets[texel], &texels[texel], sizeof(std::uint32_t));
}

std::vector<std::byte> GuestBytes(std::uint64_t address, std::size_t bytes) {
    std::vector<std::byte> raw(bytes);
    AgcDriver::GuestMemory::Read(address, raw);
    return raw;
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

// The read base after the depth draw, sampled: every texel must hold the drawn gradient, `raise` deeper.
std::vector<std::uint32_t> DrawnDepth(TexelReader& reader, const Surfaces& surfaces, const std::string& name, float raise = 0.0f) {
    const auto depth = reader.Read(surfaces.depth, name + ": texture read of the read base");
    std::uint32_t drawn = 0;
    for (std::uint32_t texel = 0; texel < Texels; ++texel) {
        if (std::fabs(std::bit_cast<float>(depth[texel]) - (raise + Expected(texel % Width, texel / Width))) <= 4e-6f) ++drawn;
    }
    std::printf("  read base: %u of %u texels hold the drawn depth gradient; texel (0, 0) %s, (%u, %u) %s\n", drawn, Texels, Hex(depth.front()).c_str(), Width - 1u, Height - 1u, Hex(depth.back()).c_str());
    Require(drawn == Texels, name + ": the depth draw did not write its gradient at the read base");
    return depth;
}

// Whether the write base holds `depth` both sampled and read raw after a flush.
bool Expanded(TexelReader& reader, std::uint64_t written, const std::vector<std::uint32_t>& depth, const std::string& name) {
    const auto sampled = Compare(reader.Read(written, name + ": texture read of the write base"), depth, "  texture read of the write base (R32F, SW_64KB_Z_X)");
    const auto stored = Compare(DetileRaw(written, GuestBytes(written, PlaneBytes())), depth, "  raw read of the write base after a flush, detiled with the SW_64KB_Z_X equation");
    return sampled == Texels && stored == Texels;
}

bool Run(TexelReader& reader, bool twoSamples) {
    const auto name = std::string(twoSamples ? "the game's expand, two raster samples" : "the expand with one raster sample");
    const auto surfaces = NewSurfaces();
    std::printf("%s: read base 0x%llx, write base 0x%llx\n", name.c_str(), static_cast<unsigned long long>(surfaces.depth), static_cast<unsigned long long>(surfaces.written));
    Submit(DepthDraw(surfaces, false, false), name + ": depth draw at the read base");
    Submit(DepthDraw(surfaces, true, twoSamples), name + ": DB expand from the read base to the write base");
    return Expanded(reader, surfaces.written, DrawnDepth(reader, surfaces, name), name);
}

// The game's expand issued as DRAW_INDIRECT or DRAW_INDEX_INDIRECT, its record in guest memory: one triangle, one instance.
bool RunIndirect(TexelReader& reader, std::uint32_t opcode) {
    const bool indexed = opcode == DrawIndexIndirect;
    const auto name = std::string(indexed ? "the game's expand as DRAW_INDEX_INDIRECT" : "the game's expand as DRAW_INDIRECT");
    const auto surfaces = NewSurfaces();
    auto* record = NewBlock(Block);
    auto* indices = NewBlock(Block);
    const std::array<std::uint32_t, 5> arguments{static_cast<std::uint32_t>(Vertices.size()), 1u, 0u, 0u, 0u};
    std::memcpy(record, arguments.data(), indexed ? 20u : 16u);
    const std::array<std::uint32_t, 3> triangle{0u, 1u, 2u};
    std::memcpy(indices, triangle.data(), sizeof(triangle));
    std::printf("%s: read base 0x%llx, write base 0x%llx, record at 0x%llx\n", name.c_str(), static_cast<unsigned long long>(surfaces.depth), static_cast<unsigned long long>(surfaces.written), static_cast<unsigned long long>(Address(record)));
    Submit(DepthDraw(surfaces, false, false), name + ": depth draw at the read base");
    Submit(DepthDraw(surfaces, true, true, {opcode, Address(record), Address(indices)}), name + ": DB expand from the read base to the write base");
    return Expanded(reader, surfaces.written, DrawnDepth(reader, surfaces, name), name);
}

// The game's expand as DRAW_INDIRECT whose record draws nothing, no vertices and then no instances: the CP draws nothing, so
// the write base keeps what the CPU wrote there.
bool RunEmptyIndirect(TexelReader& reader) {
    const auto name = std::string("the game's expand as DRAW_INDIRECT with an empty record");
    const auto surfaces = NewSurfaces();
    auto* record = NewBlock(Block);
    Fill(surfaces.written, PlaneBytes(), CpuWritten);
    std::printf("%s: read base 0x%llx, write base 0x%llx, record at 0x%llx\n", name.c_str(), static_cast<unsigned long long>(surfaces.depth), static_cast<unsigned long long>(surfaces.written), static_cast<unsigned long long>(Address(record)));
    Submit(DepthDraw(surfaces, false, false), name + ": depth draw at the read base");
    for (const auto& arguments : {std::array<std::uint32_t, 4>{0u, 1u, 0u, 0u}, std::array<std::uint32_t, 4>{static_cast<std::uint32_t>(Vertices.size()), 0u, 0u, 0u}}) {
        std::memcpy(record, arguments.data(), sizeof(arguments));
        Submit(DepthDraw(surfaces, true, true, {DrawIndirect, Address(record), 0u}), name + ": DB expand of " + std::to_string(arguments[0]) + " vertices and " + std::to_string(arguments[1]) + " instances");
    }
    const auto raw = GuestBytes(surfaces.written, PlaneBytes());
    std::uint32_t kept = 0;
    for (std::size_t at = 0; at < raw.size(); at += sizeof(std::uint32_t)) {
        std::uint32_t word = 0;
        std::memcpy(&word, raw.data() + at, sizeof(word));
        if (word == CpuWritten) ++kept;
    }
    const auto words = static_cast<std::uint32_t>(raw.size() / sizeof(std::uint32_t));
    std::printf("  raw read of the write base after a flush: %u of %u dwords hold what the CPU wrote (%s)\n", kept, words, Hex(CpuWritten).c_str());
    return kept == words;
}

// The game's expand from a read base where no depth surface exists yet and whose HTILE the title cleared: every tile reads
// as the clear, so the write base takes DB_DEPTH_CLEAR whatever the read base's bytes hold.
bool RunClearedReadBase(TexelReader& reader) {
    const auto name = std::string("the game's expand of an HTILE-cleared read base before any depth surface there");
    const auto surfaces = NewSurfaces();
    Fill(surfaces.depth, PlaneBytes(), CpuWritten);
    Fill(surfaces.htile, Block, 0xffffffffu);
    std::printf("%s: read base 0x%llx, write base 0x%llx, HTILE 0x%llx\n", name.c_str(), static_cast<unsigned long long>(surfaces.depth), static_cast<unsigned long long>(surfaces.written), static_cast<unsigned long long>(surfaces.htile));
    reader.ClearHtile(surfaces.htile, static_cast<std::uint32_t>(HtileMetadataBytes({Width, Height})), name + ": HTILE clear of the read surface");
    Submit(DepthDraw(surfaces, true, true, {}, ClearDepth), name + ": DB expand from the read base to the write base");
    return Expanded(reader, surfaces.written, std::vector<std::uint32_t>(Texels, std::bit_cast<std::uint32_t>(ClearDepth)), name);
}

// The game's expand from a read base where no depth surface exists and no HTILE clear is pending (its HTILE says expanded):
// the DB writes the read base's bytes as they are, so the write base takes the read base's guest bytes.
bool RunGuestReadBase(TexelReader& reader) {
    const auto name = std::string("the game's expand of a read base the CPU wrote, before any depth surface there");
    const auto surfaces = NewSurfaces();
    Fill(surfaces.htile, Block, 0xffffffffu);
    std::vector<std::uint32_t> depth(Texels);
    for (std::uint32_t texel = 0; texel < Texels; ++texel) depth[texel] = std::bit_cast<std::uint32_t>(0.25f + Expected(texel % Width, texel / Width));
    StoreTiled(surfaces.depth, depth);
    std::printf("%s: read base 0x%llx, write base 0x%llx\n", name.c_str(), static_cast<unsigned long long>(surfaces.depth), static_cast<unsigned long long>(surfaces.written));
    Submit(DepthDraw(surfaces, true, true), name + ": DB expand from the read base to the write base");
    const bool expanded = Expanded(reader, surfaces.written, depth, name);
    const bool same = GuestBytes(surfaces.written, PlaneBytes()) == GuestBytes(surfaces.depth, PlaneBytes());
    std::printf("  the write base's %zu bytes %s the read base's\n", PlaneBytes(), same ? "equal" : "differ from");
    return expanded && same;
}

// The game's expand followed at once by a raw buffer copy of the write base, the title's copy shader's read: the copy must
// hold the read surface's depth, in the plane's swizzle. The expand leaves the plane pending; an address-based copy has it
// stored first (StoreDepthExpandWrites), a plain one stores the range it binds itself.
bool RunBufferRead(TexelReader& reader, bool addressBased) {
    const auto name = std::string(addressBased ? "an address-based buffer copy of the write base after the game's expand" : "a buffer copy of the write base after the game's expand");
    const auto surfaces = NewSurfaces();
    const auto copy = Address(NewBlock(PlaneBytes()));
    Fill(copy, PlaneBytes(), 0xffffffffu);
    std::printf("%s: read base 0x%llx, write base 0x%llx, copy at 0x%llx\n", name.c_str(), static_cast<unsigned long long>(surfaces.depth), static_cast<unsigned long long>(surfaces.written), static_cast<unsigned long long>(copy));
    Submit(DepthDraw(surfaces, false, false), name + ": depth draw at the read base");
    const auto before = DepthExpandCounts().stored;
    Submit(DepthDraw(surfaces, true, true), name + ": DB expand from the read base to the write base");
    const auto atExpand = DepthExpandCounts().stored - before;
    reader.CopyBuffer(surfaces.written, copy, static_cast<std::uint32_t>(PlaneBytes()), addressBased, name + ": raw buffer copy of the write base");
    const auto byCopy = DepthExpandCounts().stored - before - atExpand;
    std::printf("  expanded planes stored for address-based builds: %llu at the expand, %llu ahead of the copy (expected 0 and %u)\n", static_cast<unsigned long long>(atExpand), static_cast<unsigned long long>(byCopy), addressBased ? 1u : 0u);
    const auto depth = DrawnDepth(reader, surfaces, name);
    const bool copied = Compare(DetileRaw(surfaces.written, GuestBytes(copy, PlaneBytes())), depth, "  buffer copy of the write base, detiled with the SW_64KB_Z_X equation") == Texels;
    return copied && atExpand == 0 && byCopy == (addressBased ? 1u : 0u);
}

// The game's expand repeated into one write base. An expand of a surface nothing wrote since the previous expand, into an
// image nothing changed since, would write what the image holds, and is skipped: reads of the write base (sampled, raw and
// by an address-based copy) in between change neither. A redraw of the surface, or a CPU write to the write base, makes the
// next expand write the plane again.
bool RunRepeatedExpand(TexelReader& reader) {
    const auto name = std::string("the game's expand repeated");
    const auto surfaces = NewSurfaces();
    const auto copy = Address(NewBlock(PlaneBytes()));
    std::printf("%s: read base 0x%llx, write base 0x%llx\n", name.c_str(), static_cast<unsigned long long>(surfaces.depth), static_cast<unsigned long long>(surfaces.written));
    Submit(DepthDraw(surfaces, false, false), name + ": depth draw at the read base");
    const auto start = DepthExpandCounts();
    std::uint64_t expands = 0, written = 0;
    bool counted = true;
    const auto expand = [&](const std::string& after, bool writes) {
        Submit(DepthDraw(surfaces, true, true), name + ": DB expand " + after);
        ++expands;
        if (writes) ++written;
        const auto counts = DepthExpandCounts();
        const auto ran = counts.expands - start.expands;
        const auto copied = ran - (counts.skipped - start.skipped);
        std::printf("  DB expand %s: %llu expands, %llu of them wrote the plane (expected %llu and %llu)\n", after.c_str(), static_cast<unsigned long long>(ran), static_cast<unsigned long long>(copied), static_cast<unsigned long long>(expands), static_cast<unsigned long long>(written));
        counted = counted && ran == expands && copied == written;
    };
    expand("of the drawn surface", true);
    expand("again", false);
    expand("a third time", false);
    auto depth = DrawnDepth(reader, surfaces, name);
    bool expanded = Expanded(reader, surfaces.written, depth, name);
    expand("after reads of the write base", false);
    reader.CopyBuffer(surfaces.written, copy, static_cast<std::uint32_t>(PlaneBytes()), true, name + ": address-based buffer copy of the write base");
    expanded = Compare(DetileRaw(surfaces.written, GuestBytes(copy, PlaneBytes())), depth, "  address-based buffer copy of the write base, detiled with the SW_64KB_Z_X equation") == Texels && expanded;
    expand("after an address-based copy of the write base", false);
    Submit(DepthDraw(surfaces, false, false, {}, 1.0f, RaisedVertices), name + ": second depth draw at the read base, 0.25 deeper");
    expand("after a second draw to the surface", true);
    depth = DrawnDepth(reader, surfaces, name, Raise);
    expanded = Expanded(reader, surfaces.written, depth, name) && expanded;
    Fill(surfaces.written, PlaneBytes(), CpuWritten);
    expand("after a CPU write to the write base", true);
    expanded = Expanded(reader, surfaces.written, depth, name) && expanded;
    return counted && expanded;
}

struct Case {
    const char* name;
    std::function<bool(TexelReader&)> run;
    const char* failure;
};

}

int main(int argc, char** argv) {
    try {
        if (!OpenVulkanTestDevice()) return VulkanTestSkipped;
        Register(VertexShader, VertexCode.data(), sizeof(VertexCode), 2);
        Register(PixelShader, PixelCode.data(), sizeof(PixelCode), 1);
        const std::vector<Case> cases{
            {"direct", [](TexelReader& reader) { return Run(reader, false); }, "depth expand: the write base does not hold the read surface's depth after the DB expand with one raster sample"},
            {"direct-two-samples", [](TexelReader& reader) { return Run(reader, true); }, "depth expand: the write base does not hold the read surface's depth after the game's DB expand with two raster samples"},
            {"draw-indirect", [](TexelReader& reader) { return RunIndirect(reader, DrawIndirect); }, "depth expand: the write base does not hold the read surface's depth after the DB expand as DRAW_INDIRECT"},
            {"draw-index-indirect", [](TexelReader& reader) { return RunIndirect(reader, DrawIndexIndirect); }, "depth expand: the write base does not hold the read surface's depth after the DB expand as DRAW_INDEX_INDIRECT"},
            {"empty-indirect", RunEmptyIndirect, "depth expand: an indirect DB expand whose record draws nothing changed the write base"},
            {"cleared-read-base", RunClearedReadBase, "depth expand: the write base does not hold DB_DEPTH_CLEAR after the DB expand of an HTILE-cleared read base without a depth surface"},
            {"guest-read-base", RunGuestReadBase, "depth expand: the write base does not hold the read base's guest bytes after the DB expand of a read base without a depth surface"},
            {"buffer-read", [](TexelReader& reader) { return RunBufferRead(reader, false); }, "depth expand: a buffer copy of the write base right after the DB expand does not hold the read surface's depth"},
            {"address-based-buffer-read", [](TexelReader& reader) { return RunBufferRead(reader, true); }, "depth expand: an address-based buffer copy of the write base right after the DB expand does not hold the read surface's depth"},
            {"repeated-expand", RunRepeatedExpand, "depth expand: a repeated DB expand wrote the plane when nothing changed, or did not write it after a change"},
        };
        // A case name runs that case alone.
        const std::string_view only = argc == 2 ? std::string_view(argv[1]) : std::string_view();
        Require(argc <= 2 && (only.empty() || std::any_of(cases.begin(), cases.end(), [&](const Case& entry) { return only == entry.name; })), "usage: [case name]");
        TexelReader reader;
        for (const auto& entry : cases) {
            if (!only.empty() && only != entry.name) continue;
            Require(entry.run(reader), entry.failure);
        }
        AgcDriverShutdown_nid_postfix();
        std::puts("out-of-place depth expand test passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        AgcDriverShutdown_nid_postfix();
        return 1;
    }
}
