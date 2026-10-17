#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/BindlessHeap.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ColorTargetLayout.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp"
#include "prx/libSceAgcDriver/Submit/include/Dcb.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libc/include/GuestWriteWatch.hpp"
#include "VulkanTestDevice.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr std::size_t BlockBytes = std::size_t{2} << 20u;
constexpr std::size_t TextureOffset = 0;
constexpr std::size_t WordsOffset = 0x10000;
constexpr std::size_t ColorOffset = 0x80000;
constexpr std::size_t CodeOffset = 0x20000;
constexpr std::size_t HeaderOffset = 0x21000;
constexpr std::uint32_t Side = 4;
constexpr std::uint32_t ColorWidth = 64;
constexpr std::uint32_t ColorHeight = 4;
constexpr std::uint32_t RenderTargetAttrib3 = (static_cast<std::uint32_t>(AgcDriver::Graphics::ColorTileMode::RenderTarget) << 14u) | (1u << 24u) | (1u << 27u);
constexpr std::array<std::uint8_t, 4> Texel{0x10u, 0x80u, 0xc0u, 0xffu};
constexpr std::byte Kept{0x40};

alignas(256) constexpr std::array<std::uint32_t, 16> VertexCode{
    0x7e000d05u, 0x080200f4u, 0x10020300u, 0x100202f4u, 0x060202f3u, 0x060400f3u, 0x10040500u, 0x060404f3u,
    0x7e060280u, 0x7e0802f2u, 0xf80008cfu, 0x04030201u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u,
};
alignas(256) constexpr std::array<std::uint32_t, 16> BdaPixelCode{0x7e0002f0u, 0x7e0202f0u, 0x7e08020cu, 0x7e0a020du, 0xdc308000u, 0x067d0004u, 0xf09c0f08u, 0x00400000u, 0xbf8c0000u, 0x7e060306u, 0xf800180fu, 0x03020100u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u};

class GuestBlock {
public:
    GuestBlock() {
#ifdef _WIN32
        block = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(BlockBytes, 65536));
        if (block != nullptr) GuestArena::GuestArenaCommit_nid_postfix(block, BlockBytes, PAGE_READWRITE, BlockBytes);
#else
        constexpr std::uintptr_t alignment = 65536;
        void* mapped = mmap(nullptr, BlockBytes + alignment, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapped != MAP_FAILED) {
            const auto begin = reinterpret_cast<std::uintptr_t>(mapped);
            const auto aligned = (begin + alignment - 1) & ~(alignment - 1);
            if (aligned != begin) munmap(mapped, aligned - begin);
            if (aligned + BlockBytes != begin + BlockBytes + alignment) munmap(reinterpret_cast<void*>(aligned + BlockBytes), begin + alignment - aligned);
            block = reinterpret_cast<std::uint8_t*>(aligned);
            GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(block, BlockBytes);
        }
#endif
        Require(block != nullptr, "cannot allocate the guest block");
        std::memset(block, 0, BlockBytes);
        GuestAllocations::Mutation().Add(block, BlockBytes, true, true, true);
    }

    ~GuestBlock() {
        GuestAllocations::Mutation().Remove(block);
#ifdef _WIN32
        GuestArena::GuestArenaReset_nid_postfix(block, BlockBytes);
        GuestArena::GuestArenaRelease_nid_postfix(block, BlockBytes);
#else
        munmap(block, BlockBytes);
        GuestWriteWatch::GuestWriteWatchUnregister_nid_postfix(block, BlockBytes);
#endif
    }

    GuestBlock(const GuestBlock&) = delete;
    GuestBlock& operator=(const GuestBlock&) = delete;

    std::uint8_t* Texture() const { return block + TextureOffset; }
    float* Words() const { return reinterpret_cast<float*>(block + WordsOffset); }
    std::uint8_t* Color() const { return block + ColorOffset; }
    std::uint32_t* Code(std::size_t index) const { return reinterpret_cast<std::uint32_t*>(block + CodeOffset + index * 0x100u); }
    std::uint8_t* Headers() const { return block + HeaderOffset; }

private:
    std::uint8_t* block = nullptr;
};

struct Header {
    Shader shader{};
    ShaderUserData userData{};
};

void Registered(Header& header, const std::uint32_t* code, std::size_t bytes, std::uint8_t type) {
    header.shader.file_header = 0x34333231u;
    header.shader.version = 0x18u;
    header.shader.header_size = sizeof(Header);
    header.shader.shader_size = static_cast<std::uint32_t>(bytes);
    header.shader.code = code;
    header.shader.type = type;
    header.shader.user_data = &header.userData;
    AgcDriverRegisterShader_nid_postfix(&header.shader);
}

void Append(std::vector<std::uint32_t>& commands, std::uint32_t opcode, std::initializer_list<std::uint32_t> payload) {
    commands.push_back(0xc0000000u | (static_cast<std::uint32_t>(payload.size() - 1) << 16u) | (opcode << 8u));
    commands.insert(commands.end(), payload);
}

AgcDriver::Graphics::ColorTargetLayout ColorLayout() {
    return AgcDriver::Graphics::ColorTargetLayout(ColorWidth, ColorHeight, AgcDriver::Graphics::ColorTileMode::RenderTarget, 4);
}

std::vector<std::uint32_t> DrawCommands(const GuestBlock& block, std::size_t word) {
    const auto vertex = reinterpret_cast<std::uintptr_t>(block.Code(0));
    const auto pixel = reinterpret_cast<std::uintptr_t>(block.Code(1));
    const auto color = reinterpret_cast<std::uintptr_t>(block.Color());
    const auto texture = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(block.Texture()));
    const auto pointer = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(block.Words() + word));
    const std::array<std::uint32_t, 14> userData{
        static_cast<std::uint32_t>(texture >> 8u), static_cast<std::uint32_t>((texture >> 40u) & 0xffu) | (56u << 20u) | (((Side - 1u) & 3u) << 30u), ((Side - 1u) >> 2u) | ((Side - 1u) << 14u), 0xfacu | (9u << 28u), 0u, 0u, 0u, 0u,
        0u, 0u, 0u, 0u,
        static_cast<std::uint32_t>(pointer), static_cast<std::uint32_t>(pointer >> 32u),
    };
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> context{
        {0x2d5, 0x2000},
        {0x1b6, 0}, {0x207, 0}, {0x200, 0}, {0x203, 0x800},
        {0x2dc, 0xaa00}, {0x2f8, 0}, {0x293, 0},
        {0x80, 0}, {0x8d, 0}, {0x83, 0xffff}, {0x8c, 0xa},
        {0x2f9, 0x2d}, {0x313, 0x6000}, {0x30e, 0xffffffff}, {0x30f, 0xffffffff},
        {0x206, 0x43f}, {0x204, 0x80000}, {0x205, 0x240},
        {0x8e, 0xf}, {0x8f, 0xf}, {0x202, 0xcc0010},
        {0x1c4, 0}, {0x1c5, 9}, {0x1c3, 4}, {0x31c, 0x28028},
        {0x31b, 0}, {0x31d, 0}, {0x3b0, (63u << 14u) | 3u},
        {0x3b8, RenderTargetAttrib3}, {0x1e0, 0},
        {0xc, 0}, {0xd, 0x40040},
        {0x81, 0x80000000}, {0x82, 0x40040},
        {0x90, 0x80000000}, {0x91, 0x40040},
        {0x94, 0x80000000}, {0x95, 0x40040},
        {0x10f, std::bit_cast<std::uint32_t>(32.0f)}, {0x110, std::bit_cast<std::uint32_t>(32.0f)},
        {0x111, std::bit_cast<std::uint32_t>(-2.0f)}, {0x112, std::bit_cast<std::uint32_t>(2.0f)},
        {0x113, std::bit_cast<std::uint32_t>(1.0f)}, {0x114, 0},
        {0xb4, 0}, {0xb5, std::bit_cast<std::uint32_t>(1.0f)},
        {0x2fe, 0}, {0x302, 0}, {0x306, 0}, {0x30a, 0},
        {0x318, static_cast<std::uint32_t>(color >> 8u)}, {0x390, static_cast<std::uint32_t>(color >> 40u)}, {0x292, 2},
    };
    std::vector<std::pair<std::uint32_t, std::uint32_t>> shader{
        {0xc8, static_cast<std::uint32_t>(vertex >> 8u)}, {0xc9, static_cast<std::uint32_t>(vertex >> 40u)}, {0x8b, 1u << 1u}, {0x8c, 0},
        {0x008, static_cast<std::uint32_t>(pixel >> 8u)}, {0x009, static_cast<std::uint32_t>(pixel >> 40u)}, {0x00b, static_cast<std::uint32_t>(userData.size()) << 1u},
    };
    for (std::uint32_t index = 0; index < userData.size(); ++index) shader.emplace_back(0x00cu + index, userData[index]);
    std::vector<std::uint32_t> commands;
    Append(commands, 0x79, {0x242, 4});
    for (const auto& [offset, value] : context) Append(commands, 0x69, {offset, value});
    for (const auto& [offset, value] : shader) Append(commands, 0x76, {offset, value});
    Append(commands, 0x2d, {3, 2});
    return commands;
}

std::uint8_t DrawAlpha(const GuestBlock& block, std::size_t word, const std::string& what) {
    std::memset(block.Color(), static_cast<int>(Kept), ColorLayout().Bytes());
    auto commands = DrawCommands(block, word);
    Packet packet{commands.data(), static_cast<std::uint32_t>(commands.size()), 0, {}};
    Require(sceAgcDriverSubmitDcb(&packet) == 0, what + ": the submission failed");
    AgcDriverWaitIdle_nid_postfix();
    std::vector<std::byte> stored(ColorLayout().Bytes());
    AgcDriver::GuestMemory::Read(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(block.Color())), stored);
    const auto layout = ColorLayout();
    std::optional<std::uint8_t> alpha;
    for (std::uint32_t y = 0; y < ColorHeight; ++y) {
        for (std::uint32_t x = 0; x < ColorWidth; ++x) {
            const auto offset = layout.Offset(x, y);
            if (std::all_of(stored.begin() + static_cast<std::ptrdiff_t>(offset), stored.begin() + static_cast<std::ptrdiff_t>(offset + 4), [](std::byte byte) { return byte == Kept; })) continue;
            for (std::size_t channel = 0; channel < 3; ++channel) Require(static_cast<std::uint8_t>(stored[offset + channel]) == Texel[channel], what + ": pixel (" + std::to_string(x) + ", " + std::to_string(y) + ") channel " + std::to_string(channel) + " is not the texture's");
            const auto value = static_cast<std::uint8_t>(stored[offset + 3]);
            Require(!alpha.has_value() || *alpha == value, what + ": the drawn pixels disagree on the word they read");
            alpha = value;
        }
    }
    Require(alpha.has_value(), what + ": the draw covered no pixel");
    return *alpha;
}

void Write(const GuestBlock& block, std::size_t word, float value) {
    block.Words()[word] = value;
}

float Value(std::uint8_t byte) {
    return static_cast<float>(byte) / 255.0f;
}

void CurrentWords(const GuestBlock& block) {
    constexpr std::array<std::uint8_t, 6> values{51u, 102u, 153u, 204u, 77u, 179u};
    constexpr std::size_t word = 1;
    for (std::size_t index = 0; index < values.size(); ++index) {
        Write(block, word, Value(values[index]));
        for (int repeat = 0; repeat < 2; ++repeat) {
            const auto what = "draw " + std::to_string(index) + (repeat == 0 ? " after the CPU wrote " : " again with ") + std::to_string(values[index]) + "/255";
            const auto alpha = DrawAlpha(block, word, what);
            Require(alpha == values[index], what + ": the draw read " + std::to_string(alpha) + "/255, expected the current word's " + std::to_string(values[index]));
        }
    }
    constexpr std::size_t other = 64;
    Write(block, other, Value(128u));
    Require(DrawAlpha(block, other, "a draw at another word") == 128u, "a draw at another word did not read it");
    Write(block, word, Value(230u));
    Require(DrawAlpha(block, word, "the first word after the other draw") == 230u, "the first word, written after a draw that read another, was read stale");
    std::printf("%zu values written between draws, each read by the draws after its write (GPU heap %s, draw pipeline %s, space reuse %s)\n", values.size() + 2, AgcDriver::Graphics::GpuHeapEnabled() ? "on" : "off", std::getenv("APS5_DRAW_PIPELINE") != nullptr ? "on" : "off", AgcDriver::Graphics::ShaderResources::SpaceReuseDisabled() ? "off" : "on");
}

}

int main() {
#ifdef _WIN32
    _putenv_s("APS5_EAGER_WRITEBACK", "1");
#else
    setenv("APS5_EAGER_WRITEBACK", "1", 1);
#endif
    try {
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
        }
        GuestBlock block;
        try {
            for (std::size_t offset = 0; offset < 0x1000; offset += Texel.size()) std::memcpy(block.Texture() + offset, Texel.data(), Texel.size());
            std::copy(VertexCode.begin(), VertexCode.end(), block.Code(0));
            std::copy(BdaPixelCode.begin(), BdaPixelCode.end(), block.Code(1));
            auto* headers = new (block.Headers()) Header[2]{};
            Registered(headers[0], block.Code(0), sizeof(VertexCode), 2);
            Registered(headers[1], block.Code(1), sizeof(BdaPixelCode), 1);
            CurrentWords(block);
        } catch (const std::exception& error) {
            std::cerr << "heap space writes failed: " << error.what() << '\n';
            AgcDriverShutdown_nid_postfix();
            return 1;
        }
        AgcDriverShutdown_nid_postfix();
        std::cout << "heap space writes passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "heap space writes failed: " << error.what() << '\n';
        try {
            AgcDriverShutdown_nid_postfix();
        } catch (const std::exception& shutdownError) {
            std::cerr << shutdownError.what() << '\n';
        }
        return 1;
    }
}
