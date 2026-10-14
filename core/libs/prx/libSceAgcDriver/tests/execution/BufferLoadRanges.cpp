#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include "GuestTestMemory.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Lanes = 32;
constexpr std::uint32_t Marker = 0xdeadbeefu;
constexpr std::uint32_t Raw = 0x31016facu;
constexpr std::uint32_t RawNoRecordCheck = 0x21016facu;
constexpr std::uint32_t InputBytes = 512;
constexpr std::uint32_t ProgramWords = 40;
TestGuest::Array<std::uint8_t, InputBytes> Input;
TestGuest::Array<std::uint32_t, Lanes * 2> Params;
TestGuest::Array<std::uint32_t, Lanes * 4> Output;

struct alignas(256) Program {
    std::array<std::uint32_t, ProgramWords> words{};
};

struct Case {
    const char* name;
    std::uint32_t opcode;
    std::uint32_t components;
    std::uint32_t bytes;
    bool coherent;
    std::uint32_t offset;
    std::uint32_t view;
    std::uint32_t records;
    std::uint32_t word3;
};

constexpr std::uint32_t Ubyte = 0x08u;
constexpr std::uint32_t Ushort = 0x0au;
constexpr std::uint32_t Dword = 0x0cu;
constexpr std::uint32_t DwordX2 = 0x0du;
constexpr std::uint32_t DwordX4 = 0x0eu;
constexpr std::uint32_t DwordX3 = 0x0fu;

constexpr std::array<Case, 20> Cases{{
    {"dwordx3", DwordX3, 3, 4, false, 0, 0, 40, Raw},
    {"dwordx3 view+1", DwordX3, 3, 4, false, 0, 1, 44, Raw},
    {"dwordx3 view+2 offset 4", DwordX3, 3, 4, false, 4, 2, 36, Raw},
    {"dwordx3 view+3", DwordX3, 3, 4, false, 0, 3, 48, Raw},
    {"dwordx3 view+4", DwordX3, 3, 4, false, 0, 4, 52, Raw},
    {"dwordx3 glc", DwordX3, 3, 4, true, 0, 0, 40, Raw},
    {"dwordx3 glc view+2", DwordX3, 3, 4, true, 0, 2, 44, Raw},
    {"dwordx4", DwordX4, 4, 4, false, 0, 0, 52, Raw},
    {"dwordx4 view+1 offset 8", DwordX4, 4, 4, false, 8, 1, 60, Raw},
    {"dwordx2 view+3", DwordX2, 2, 4, false, 0, 3, 28, Raw},
    {"dwordx2 glc view+2", DwordX2, 2, 4, true, 0, 2, 28, Raw},
    {"dword", Dword, 1, 4, false, 0, 0, 36, Raw},
    {"dword view+1 offset 4", Dword, 1, 4, false, 4, 1, 36, Raw},
    {"ushort view+2", Ushort, 1, 2, false, 0, 2, 40, Raw},
    {"ubyte view+3", Ubyte, 1, 1, false, 0, 3, 36, Raw},
    {"dwordx3 without record check", DwordX3, 3, 4, false, 0, 0, 36, RawNoRecordCheck},
    {"dwordx3 without record check view+1", DwordX3, 3, 4, false, 0, 1, 36, RawNoRecordCheck},
    {"dwordx4 without record check view+3", DwordX4, 4, 4, false, 0, 3, 40, RawNoRecordCheck},
    {"dwordx3 without records", DwordX3, 3, 4, false, 0, 0, 0, Raw},
    {"dwordx3 with 4 bytes", DwordX3, 3, 4, false, 0, 2, 4, Raw},
}};

std::array<std::uint32_t, 4> BufferDescriptor(const void* data, std::uint32_t bytes, std::uint32_t word3) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, word3};
}

std::string Hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08x", value);
    return text;
}

std::array<std::uint32_t, Lanes> LaneOffsets(const Case& c) {
    const auto n = c.records;
    const std::array<std::uint32_t, 4> tail = c.bytes == 1 ? std::array<std::uint32_t, 4>{n - 3, n - 1, 3, 7} : c.bytes == 2 ? std::array<std::uint32_t, 4>{n - 4, n - 2, 2, 6} : std::array<std::uint32_t, 4>{n - 24, n - 28, 32, 36};
    return {0, 4, 8, 12, 16, 20, 24, 28,
            n - 20, n - 16, n - 12, n - 8, n - 4, n, n + 4, n + 8,
            0xfffffffcu, 0xfffffff8u, 0xfffffff4u, 0xfffffff0u, 0x80000000u, 0x7ffffffcu, 0xffffff00u, 0x00010000u,
            tail[0], tail[1], tail[2], tail[3], 0, 4, n - 8, n - 4};
}

bool LaneActive(std::uint32_t lane) {
    return lane != 3 && lane != 9 && lane != 13 && lane < 28;
}

bool Valid(const Case& c, std::uint32_t offset) {
    if (c.records == 0) return false;
    if (c.word3 == Raw) return c.records >= c.bytes && offset <= c.records - c.bytes;
    return (offset >> 2) < c.records / 4;
}

std::uint32_t Loaded(const Case& c, const std::uint8_t* view, std::uint32_t offset) {
    std::uint32_t dword = 0;
    std::memcpy(&dword, view + (offset & ~3u), sizeof(dword));
    if (c.bytes == 4) return dword;
    return (dword >> ((offset & 3u) * 8u)) & (c.bytes == 1 ? 0xffu : 0xffffu);
}

void Assemble(Program& program, const Case& c) {
    auto& w = program.words;
    w.fill(0xbf800000u);
    std::size_t at = 0;
    const auto emit = [&](std::uint32_t word) { w[at++] = word; };
    emit(0x34020083u);
    emit(0x34040084u);
    emit(0xe0341000u);
    emit(0x80020801u);
    for (std::uint32_t reg = 4; reg < 8; ++reg) {
        emit(0x7e0002ffu | (reg << 17u));
        emit(Marker);
    }
    emit(0xbf8c3f70u);
    emit(0x7d8a1280u);
    emit(0xbea83c6au);
    emit(0xe0001000u | (c.opcode << 18u) | c.offset | (c.coherent ? 0x4000u : 0u));
    emit(0x80000408u);
    emit(0xbf8c3f70u);
    emit(0xbefe0328u);
    emit(0xe0781000u);
    emit(0x80010402u);
    emit(0xbf810000u);
}

void Run(AgcDriver::VulkanDevice& device, Program& program, const Case& c) {
    for (std::uint32_t index = 0; index < InputBytes; ++index) Input[index] = static_cast<std::uint8_t>(index * 37u + 11u + (index >> 3u));
    const auto offsets = LaneOffsets(c);
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
        Params[lane * 2u] = offsets[lane];
        Params[lane * 2u + 1u] = LaneActive(lane) ? 1u : 0u;
    }
    Output.fill(0xcdcdcdcdu);
    Assemble(program, c);
    const auto* view = Input.data() + c.view;
    TestGuest::Vector<std::uint32_t> userData;
    for (const auto& descriptor : {BufferDescriptor(view, c.records, c.word3), BufferDescriptor(Output.data(), decltype(Output)::ByteSize, Raw), BufferDescriptor(Params.data(), decltype(Params)::ByteSize, Raw)}) {
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
    device.Dispatch(result, 1, 1, 1, reinterpret_cast<std::uintptr_t>(code.data()));
    device.WaitIdle();
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
        for (std::uint32_t dword = 0; dword < 4; ++dword) {
            std::uint32_t expected = Marker;
            if (LaneActive(lane) && dword < c.components) {
                const auto offset = offsets[lane] + c.offset + dword * 4u;
                expected = Valid(c, offset) ? Loaded(c, view, offset) : 0u;
            }
            const auto actual = Output[lane * 4u + dword];
            Require(actual == expected, std::string("buffer load ranges ") + c.name + ": lane " + std::to_string(lane) + " dword " + std::to_string(dword) + " is " + Hex(actual) + ", expected " + Hex(expected));
        }
    }
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        static Program programs[Cases.size()];
        for (std::size_t index = 0; index < Cases.size(); ++index) Run(*device, programs[index], Cases[index]);
        std::puts("buffer load ranges tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
