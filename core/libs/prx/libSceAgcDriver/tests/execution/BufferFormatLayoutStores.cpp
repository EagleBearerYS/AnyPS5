#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <tuple>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Lanes = 32;
constexpr std::uint32_t Raw = 0x31016facu;
constexpr std::uint32_t Formats = 77;
constexpr std::uint32_t Records = 0x300u;
constexpr std::uint32_t RegionBytes = 0x400u;
constexpr std::uint32_t ProgramWords = 64;
alignas(256) std::array<std::uint32_t, Lanes * 8> Params{};
alignas(256) std::array<std::uint8_t, RegionBytes> First{};
alignas(256) std::array<std::uint8_t, RegionBytes> Second{};
alignas(256) std::array<std::uint8_t, RegionBytes> Typed{};

struct alignas(256) Program {
    std::array<std::uint32_t, ProgramWords> words{};
};

std::array<std::uint32_t, 4> BufferDescriptor(const void* data, std::uint32_t bytes, std::uint32_t word3) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, word3};
}

std::string Hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%02x", value);
    return text;
}

std::uint32_t Hash(std::uint32_t value) {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

std::uint32_t Word3(std::uint32_t format) {
    return (Raw & ~0x7f000u) | (format << 12u);
}

std::uint32_t Activity(std::uint32_t lane) {
    return std::array<std::uint32_t, 4>{1u, 1u, 0u, 1u}[lane % 4u];
}

std::uint32_t Offset(std::uint32_t lane) {
    constexpr std::array<std::uint32_t, 8> tail{Records - 48u, Records - 32u, Records - 14u, Records - 2u, Records, Records + 4u, 0xfffffff0u, 0x80000000u};
    return lane < 24u ? 4u + lane * 24u + (lane % 3u) : tail[lane - 24u];
}

std::uint32_t Special(std::uint32_t index) {
    constexpr std::array<std::uint32_t, 12> values{0x7fc00000u, 0x7f800001u, 0xff800000u, 0x00000001u, 0x80000000u, 0x3f800000u, 0xbf800000u, 0x477fe000u, 0x00800000u, 0xc7000000u, 0x4b800001u, 0x3effffffu};
    return values[index % values.size()];
}

std::uint8_t Initial(std::uint32_t offset) {
    return static_cast<std::uint8_t>(Hash(offset * 131u + 7u) >> 7u);
}

void Assemble(Program& program, std::uint32_t first, std::uint32_t second) {
    auto& w = program.words;
    w.fill(0xbf800000u);
    std::size_t at = 0;
    const auto emit = [&](std::uint32_t word) { w[at++] = word; };
    emit(0x34020085u);
    emit(0xe0381000u);
    emit(0x80020801u);
    emit(0xe0381010u);
    emit(0x80020401u);
    emit(0xbf8c3f70u);
    emit(0xbea8037eu);
    emit(0x363c1281u);
    emit(0x7d8a3c80u);
    emit(0x87296a28u);
    for (const auto [format, resource, lanes] : {std::array<std::uint32_t, 3>{first, 0u, 0x55555555u}, std::array<std::uint32_t, 3>{second, 1u, 0xaaaaaaaau}}) {
        emit(0x877eff29u);
        emit(lanes);
        emit(0xe01c1000u);
        emit(0x80000408u | (resource << 16u));
        emit(0xe8071000u | (format << 19u));
        emit(0x80030408u);
    }
    emit(0xbf8c3f70u);
    emit(0xbefe0328u);
    emit(0xbf810000u);
    Require(at <= w.size(), "buffer format layout stores: the program does not fit");
}

void Run(AgcDriver::VulkanDevice& device, Program& program, std::uint32_t first, std::uint32_t second) {
    for (std::uint32_t offset = 0; offset < RegionBytes; ++offset) First[offset] = Second[offset] = Typed[offset] = Initial(offset);
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
        Params[lane * 8u] = Offset(lane);
        Params[lane * 8u + 1u] = Activity(lane);
        for (std::uint32_t component = 0; component < 4u; ++component) {
            const auto mixed = Hash(lane * 4u + component + first * 977u);
            Params[lane * 8u + 4u + component] = (mixed & 7u) == 0u ? Special(mixed >> 3u) : (mixed & 7u) == 1u ? (mixed >> 24u) : (mixed & 7u) == 2u ? (mixed >> 9u) | 0x3f000000u : mixed;
        }
    }
    Assemble(program, first, second);
    std::vector<std::uint32_t> userData;
    for (const auto& descriptor : {BufferDescriptor(First.data(), Records, Word3(first)), BufferDescriptor(Second.data(), Records, Word3(second)), BufferDescriptor(Params.data(), sizeof(Params), Raw), BufferDescriptor(Typed.data(), Records, Raw)}) {
        userData.insert(userData.end(), descriptor.begin(), descriptor.end());
    }
    const std::span<const std::uint32_t> code(program.words);
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{Lanes, 1, 1}, 0u, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
        {Lanes, 0, userData, compute, std::nullopt, std::nullopt, memory},
        device.Target(),
        {0, 0, 0, 128}
    };
    request.useCache = false;
    const auto result = ShaderRecompiler::Recompile(request);
    device.Dispatch(result, 1, 1, 1, {}, reinterpret_cast<std::uintptr_t>(code.data()));
    device.WaitIdle();
    std::array<std::int32_t, RegionBytes> owner{};
    owner.fill(-1);
    const std::array<std::array<std::uint32_t, 2>, 4> tail{{{Records - 48u, Records - 32u}, {Records - 32u, Records - 16u}, {Records - 16u, Records - 4u}, {Records - 4u, Records}}};
    for (std::uint32_t lane = 0; lane < 28u; ++lane) {
        const auto begin = lane < 24u ? lane * 24u : tail[lane - 24u][0];
        const auto end = lane < 24u ? begin + 24u : tail[lane - 24u][1];
        for (auto offset = begin; offset < end; ++offset) owner[offset] = static_cast<std::int32_t>(lane);
    }
    std::uint32_t written = 0;
    for (std::uint32_t offset = 0; offset < RegionBytes; ++offset) {
        const auto lane = owner[offset];
        for (const auto [region, format, parity] : {std::tuple{&First, first, 0}, std::tuple{&Second, second, 1}}) {
            const auto actual = (*region)[offset];
            const std::string where = "buffer format layout stores: format " + std::to_string(format) + " byte " + std::to_string(offset);
            if (lane >= 0 && lane % 2 == parity) {
                Require(actual == Typed[offset], where + " (lane " + std::to_string(lane) + ") is " + Hex(actual) + " through the run-time format, " + Hex(Typed[offset]) + " through the typed format");
                if (actual != Initial(offset)) ++written;
            } else {
                Require(actual == Initial(offset), where + " outside the lanes that store this format changed to " + Hex(actual));
            }
        }
        if (lane < 0) Require(Typed[offset] == Initial(offset), "buffer format layout stores: typed byte " + std::to_string(offset) + " outside every lane changed");
    }
    Require(written != 0u, "buffer format layout stores: formats " + std::to_string(first) + " and " + std::to_string(second) + " stored nothing");
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        static Program programs[(Formats + 1u) / 2u];
        for (std::uint32_t format = 1; format <= Formats; format += 2u) Run(*device, programs[format / 2u], format, format < Formats ? format + 1u : 1u);
        std::puts("buffer format layout stores tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
