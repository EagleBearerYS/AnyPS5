#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_TESTS_EXECUTION_INDIRECTDRAWCACHEFIXTURE_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_TESTS_EXECUTION_INDIRECTDRAWCACHEFIXTURE_HPP

#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Submit/include/Dcb.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace IndirectDrawCacheFixture {

using AgcDriver::Graphics::Require;

inline constexpr std::byte Kept{0x40};
inline constexpr std::uint32_t BaseVertexLocation = 0x8c;
inline constexpr std::uint32_t StartInstanceLocation = 0x8d;
inline constexpr std::uint32_t Clipped = 0x40000000u;

alignas(256) inline constexpr std::array<std::uint32_t, 21> PushedVertexCode{
    0x7e000d05u, 0x080200f4u, 0x10020300u, 0x100202f4u, 0x060202f3u, 0x060400f3u, 0x10040500u, 0x060404f3u,
    0x7e060208u, 0x06060609u, 0x7e0802f2u, 0x7e0a02ffu, 0x44800000u, 0xf80000d1u, 0x00000005u, 0xf80008cfu, 0x04030201u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u,
};
alignas(256) inline constexpr std::array<std::uint32_t, 23> WalkedVertexCode{
    0x7e000d05u, 0x080200f4u, 0x10020300u, 0x100202f4u, 0x060202f3u, 0x060400f3u, 0x10040500u, 0x060404f3u,
    0xf4000304u, 0xfa000000u, 0xbf8cc07fu, 0x7e06020cu, 0x7e0802f2u, 0x7e0a02ffu, 0x44800000u, 0xf80000d1u, 0x00000005u, 0xf80008cfu, 0x04030201u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u,
};
alignas(256) inline constexpr std::array<std::uint32_t, 8> PixelCode{0x7e000200u, 0x7e020280u, 0xf800180fu, 0x00010100u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u};
alignas(256) inline std::array<std::byte, 1024> ColorMemory{};
alignas(256) inline std::array<std::uint32_t, 8> Records{};
alignas(256) inline std::array<float, 64> Depths{};

struct Header {
    Shader shader{};
    ShaderUserData userData{};
};

alignas(8) inline Header PushedVertexHeader{};
alignas(8) inline Header WalkedVertexHeader{};
alignas(8) inline Header PixelHeader{};

inline void Registered(Header& header, const std::uint32_t* code, std::size_t bytes, std::uint8_t type) {
    header.shader.file_header = 0x34333231u;
    header.shader.version = 0x18u;
    header.shader.header_size = sizeof(Header);
    header.shader.shader_size = static_cast<std::uint32_t>(bytes);
    header.shader.code = code;
    header.shader.type = type;
    header.shader.user_data = &header.userData;
    AgcDriverRegisterShader_nid_postfix(&header.shader);
}

inline void RegisterShaders() {
    Registered(PushedVertexHeader, PushedVertexCode.data(), sizeof(PushedVertexCode), 2);
    Registered(WalkedVertexHeader, WalkedVertexCode.data(), sizeof(WalkedVertexCode), 2);
    Registered(PixelHeader, PixelCode.data(), sizeof(PixelCode), 1);
}

inline void Append(std::vector<std::uint32_t>& commands, std::uint32_t opcode, std::initializer_list<std::uint32_t> payload) {
    commands.push_back(0xc0000000u | (static_cast<std::uint32_t>(payload.size() - 1) << 16u) | (opcode << 8u));
    commands.insert(commands.end(), payload);
}

struct IndirectPacket {
    const std::uint32_t* vertex = nullptr;
    std::array<std::uint32_t, 2> userWords{};
    std::uint32_t startInstanceLocation = 0;
    std::uint32_t records = 1;
};

inline const IndirectPacket PushedOne{PushedVertexCode.data(), {Clipped, Clipped}, StartInstanceLocation, 1};
inline const IndirectPacket PushedTwo{PushedVertexCode.data(), {Clipped, Clipped}, StartInstanceLocation, 2};

inline std::vector<std::uint32_t> Commands(const IndirectPacket& draw) {
    const auto vertex = reinterpret_cast<std::uintptr_t>(draw.vertex);
    const auto pixel = reinterpret_cast<std::uintptr_t>(PixelCode.data());
    const auto records = reinterpret_cast<std::uintptr_t>(Records.data());
    const auto color = reinterpret_cast<std::uintptr_t>(ColorMemory.data());
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> context{
        {0x2d5, 0x2000},
        {0x1b6, 0}, {0x207, 0x01200000}, {0x200, 0}, {0x203, 0x800},
        {0x2dc, 0xaa00}, {0x2f8, 0}, {0x292, 2}, {0x293, 0},
        {0x80, 0}, {0x8d, 0}, {0x83, 0xffff}, {0x8c, 0xa},
        {0x2f9, 0x2d}, {0x313, 0x6000}, {0x30e, 0xffffffff}, {0x30f, 0xffffffff},
        {0x206, 0x43f}, {0x204, 0x80000}, {0x205, 0x240},
        {0x8e, 0xf}, {0x8f, 0xf}, {0x202, 0xcc0010},
        {0x1c4, 0}, {0x1c5, 9}, {0x1c3, 4}, {0x31c, 0x28028},
        {0x31b, 0}, {0x31d, 0}, {0x3b0, (63u << 14u) | 3u},
        {0x3b8, 0x9000000}, {0x1e0, 0},
        {0xc, 0}, {0xd, 0x40040},
        {0x81, 0x80000000}, {0x82, 0x40040},
        {0x90, 0x80000000}, {0x91, 0x40040},
        {0x94, 0x80000000}, {0x95, 0x40040},
        {0x10f, std::bit_cast<std::uint32_t>(32.0f)}, {0x110, std::bit_cast<std::uint32_t>(32.0f)},
        {0x111, std::bit_cast<std::uint32_t>(-2.0f)}, {0x112, std::bit_cast<std::uint32_t>(2.0f)},
        {0x113, std::bit_cast<std::uint32_t>(1.0f)}, {0x114, 0},
        {0xb4, 0}, {0xb5, std::bit_cast<std::uint32_t>(1.0f)},
        {0x2fe, 0}, {0x302, 0}, {0x306, 0}, {0x30a, 0},
        {0x318, static_cast<std::uint32_t>(color >> 8u)}, {0x390, static_cast<std::uint32_t>(color >> 40u)},
    };
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> shader{
        {0xc8, static_cast<std::uint32_t>(vertex >> 8u)}, {0xc9, static_cast<std::uint32_t>(vertex >> 40u)}, {0x8b, 2u << 1u},
        {BaseVertexLocation, draw.userWords[0]}, {StartInstanceLocation, draw.userWords[1]},
        {0x008, static_cast<std::uint32_t>(pixel >> 8u)}, {0x009, static_cast<std::uint32_t>(pixel >> 40u)}, {0x00b, 1u << 1u}, {0x00c, std::bit_cast<std::uint32_t>(1.0f)},
    };
    std::vector<std::uint32_t> commands;
    Append(commands, 0x79, {0x242, 4});
    Append(commands, 0x79, {0x24a, 0});
    for (const auto& [offset, value] : context) Append(commands, 0x69, {offset, value});
    for (const auto& [offset, value] : shader) Append(commands, 0x76, {offset, value});
    Append(commands, 0x11, {1, static_cast<std::uint32_t>(records), static_cast<std::uint32_t>(records >> 32u)});
    if (draw.records == 1) Append(commands, 0x24, {0, BaseVertexLocation, draw.startInstanceLocation, 2});
    else Append(commands, 0x2c, {0, BaseVertexLocation, draw.startInstanceLocation, 0, draw.records, 0, 0, 16, 2});
    return commands;
}

struct Drawn {
    std::array<std::byte, 1024> color{};
    std::size_t covered = 0;
    AgcDriver::DriverDetail::DrawEntryCounters before;
    AgcDriver::DriverDetail::DrawEntryCounters after;
    std::uint64_t Hits() const { return after.hits - before.hits; }
};

inline Drawn Submit(const IndirectPacket& draw, std::initializer_list<std::array<std::uint32_t, 4>> records) {
    Records.fill(0);
    std::size_t at = 0;
    for (const auto& record : records) {
        std::copy(record.begin(), record.end(), Records.begin() + at);
        at += record.size();
    }
    ColorMemory.fill(Kept);
    Drawn drawn;
    drawn.before = AgcDriver::DriverDetail::Driver::Get().DrawCacheCounters();
    auto commands = Commands(draw);
    Packet packet{commands.data(), static_cast<std::uint32_t>(commands.size()), 0, {}};
    Require(sceAgcDriverSubmitDcb(&packet) == 0, "the draw submission failed");
    AgcDriverWaitIdle_nid_postfix();
    drawn.after = AgcDriver::DriverDetail::Driver::Get().DrawCacheCounters();
    AgcDriver::GuestMemory::FlushGpuWrites(reinterpret_cast<std::uintptr_t>(ColorMemory.data()), ColorMemory.size());
    drawn.color = ColorMemory;
    drawn.covered = static_cast<std::size_t>(std::count_if(ColorMemory.begin(), ColorMemory.end(), [](std::byte value) { return value != Kept; }));
    return drawn;
}

inline void RequireSamePixels(const Drawn& drawn, const Drawn& expected, const std::string& what) {
    for (std::size_t index = 0; index < ColorMemory.size(); ++index) {
        Require(drawn.color[index] == expected.color[index], "byte " + std::to_string(index) + " of " + what + " is " + std::to_string(static_cast<unsigned>(drawn.color[index])) + ", the reference draw wrote " + std::to_string(static_cast<unsigned>(expected.color[index])));
    }
}

inline Drawn Reference() {
    const auto drawn = Submit(PushedOne, {{3, 1, 0, 0}});
    Require(drawn.covered != 0 && drawn.covered != ColorMemory.size(), "the CPU-path indirect draw whose record sets depth 0 over clipped register values covered " + std::to_string(drawn.covered) + " of " + std::to_string(ColorMemory.size()) + " bytes, expected an edge through the target");
    return drawn;
}

inline std::string Hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%x", value);
    return text;
}

}

#endif
