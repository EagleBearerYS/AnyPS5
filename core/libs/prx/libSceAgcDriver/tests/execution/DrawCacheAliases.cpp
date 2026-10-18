#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Submit/include/Dcb.hpp"
#include "VulkanTestDevice.hpp"
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;

constexpr std::size_t Contents = 8;
constexpr std::size_t Addresses = 5;

alignas(256) constexpr std::array<std::uint32_t, 8> VertexCode{0x7e000208u, 0x7e020280u, 0x7e040280u, 0x7e0602f2u, 0xf80008cfu, 0x03020100u, 0xbf810000u, 0xbf810000u};
alignas(256) constexpr std::array<std::uint32_t, 8> PixelCode{0xf4000100u, 0xfa000000u, 0x7e000204u, 0x7e020280u, 0xf800180fu, 0x00010100u, 0xbf810000u, 0xbf810000u};
alignas(16) std::array<std::array<std::array<float, 4>, Addresses>, Contents> Data{};
alignas(256) std::array<std::byte, 1024> ColorMemory{};

struct Header {
    Shader shader{};
    ShaderUserData userData{};
};

alignas(8) Header VertexHeader{};
alignas(8) Header PixelHeader{};

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

std::vector<std::uint32_t> DrawFrom(const void* source) {
    const auto color = reinterpret_cast<std::uintptr_t>(ColorMemory.data());
    const auto vertex = reinterpret_cast<std::uintptr_t>(VertexCode.data());
    const auto pixel = reinterpret_cast<std::uintptr_t>(PixelCode.data());
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> context{
        {0x2d5, 0x2000},
        {0x1b6, 0}, {0x207, 0}, {0x200, 0}, {0x203, 0x800},
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
        {0x318, static_cast<std::uint32_t>(color >> 8u)}, {0x390, static_cast<std::uint32_t>(color >> 40u)},
        {0x10f, std::bit_cast<std::uint32_t>(32.0f)}, {0x110, std::bit_cast<std::uint32_t>(32.0f)},
        {0x111, std::bit_cast<std::uint32_t>(-2.0f)}, {0x112, std::bit_cast<std::uint32_t>(2.0f)},
        {0x113, std::bit_cast<std::uint32_t>(1.0f)}, {0x114, 0},
        {0xb4, 0}, {0xb5, std::bit_cast<std::uint32_t>(1.0f)},
    };
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> shader{
        {0xc8, static_cast<std::uint32_t>(vertex >> 8u)}, {0xc9, static_cast<std::uint32_t>(vertex >> 40u)}, {0x8b, 1u << 1u}, {0x8c, 0},
        {0x008, static_cast<std::uint32_t>(pixel >> 8u)}, {0x009, static_cast<std::uint32_t>(pixel >> 40u)}, {0x00b, 2u << 1u},
    };
    std::vector<std::uint32_t> commands;
    Append(commands, 0x79, {0x242, 4});
    for (const auto& [offset, value] : context) Append(commands, 0x69, {offset, value});
    for (const auto& [offset, value] : shader) Append(commands, 0x76, {offset, value});
    const auto address = reinterpret_cast<std::uintptr_t>(source);
    Append(commands, 0x76, {0x00c, static_cast<std::uint32_t>(address)});
    Append(commands, 0x76, {0x00d, static_cast<std::uint32_t>(address >> 32u)});
    Append(commands, 0x2d, {3, 2});
    return commands;
}

AgcDriver::DriverDetail::DrawEntryCounters Draw(const void* source) {
    auto commands = DrawFrom(source);
    Packet packet{commands.data(), static_cast<std::uint32_t>(commands.size()), 0, {}};
    Require(sceAgcDriverSubmitDcb(&packet) == 0, "the draw submission failed");
    AgcDriverWaitIdle_nid_postfix();
    return AgcDriver::DriverDetail::Driver::Get().DrawCacheCounters();
}

std::string Describe(const AgcDriver::DriverDetail::Driver::DrawCacheFootprint& footprint) {
    return std::to_string(footprint.aliases) + " aliases holding " + std::to_string(footprint.aliased) + " entries, " + std::to_string(footprint.entries) + " entries in the cache, " + std::to_string(footprint.evictions) + " evicted, " + std::to_string(footprint.relocationHits) + " relocation hits";
}

std::string EvictedEntriesLeaveTheirAliases(bool libraries) {
    Registered(VertexHeader, VertexCode.data(), sizeof(VertexCode), 2);
    Registered(PixelHeader, PixelCode.data(), sizeof(PixelCode), 1);
    const auto mode = std::string(libraries ? "with" : "without") + " graphics pipeline libraries";
    for (std::size_t content = 0; content < Contents; ++content) {
        for (auto& data : Data[content]) data = {0.0625f * static_cast<float>(content + 1), 0.0f, 0.0f, 0.0f};
    }
    for (std::size_t content = 0; content < Contents; ++content) {
        for (std::size_t address = 0; address + 1 < Addresses; ++address) Draw(Data[content][address].data());
    }
    auto& driver = AgcDriver::DriverDetail::Driver::Get();
    const auto footprint = driver.DrawCacheSize();
    Require(footprint.evictions != 0, mode + ": the draws evicted no draw-cache entry (" + Describe(footprint) + ")");
    Require(footprint.aliases != 0, mode + ": relocation learned no alias (" + Describe(footprint) + ")");
    Require(footprint.aliasedOutside == 0, mode + ": " + std::to_string(footprint.aliasedOutside) + " draw-cache entries that left the cache are still held by relocation aliases (" + Describe(footprint) + ")");
    const auto before = driver.DrawCacheCounters();
    const auto after = Draw(Data[Contents - 1][Addresses - 1].data());
    Require(after.hits == before.hits + 1, mode + ": the newest data at a new address did not reuse the cached draw through its alias (" + Describe(driver.DrawCacheSize()) + ")");
    return mode + ": " + Describe(footprint) + ", none outside the cache; the newest data at a new address still hit";
}

}

int main() {
#ifdef _WIN32
    _putenv_s("APS5_DRAW_CACHE_ENTRIES", "4");
#else
    setenv("APS5_DRAW_CACHE_ENTRIES", "4", 1);
#endif
    try {
        bool libraries = false;
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
            libraries = device->GraphicsPipelineLibraries();
        }
        if (!libraries && std::getenv("APS5_NO_GPL") == nullptr) return VulkanTestSkipped;
        const auto summary = EvictedEntriesLeaveTheirAliases(libraries);
        AgcDriverShutdown_nid_postfix();
        std::cout << summary << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        AgcDriverShutdown_nid_postfix();
        return 1;
    }
}
