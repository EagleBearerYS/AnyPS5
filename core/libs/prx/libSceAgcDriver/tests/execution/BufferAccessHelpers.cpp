#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include "GuestTestMemory.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
constexpr std::size_t BlockBytes = 65536;
constexpr std::uint32_t PastBlock = 0x20000u;
constexpr std::uint32_t Unmapped = 0x10010u;
constexpr std::uint32_t ProgramWords = 64;
TestGuest::Array<std::uint8_t, InputBytes> Input;
TestGuest::Array<std::uint32_t, Lanes * 4> Params;
TestGuest::Array<std::uint32_t, Lanes * 8> Output;
TestGuest::Array<std::uint32_t, 4> Descriptor;

struct alignas(256) Program {
    std::array<std::uint32_t, ProgramWords> words{};
};

constexpr std::uint32_t Ubyte = 0x08u;
constexpr std::uint32_t Ushort = 0x0au;
constexpr std::uint32_t Dword = 0x0cu;
constexpr std::uint32_t DwordX2 = 0x0du;
constexpr std::uint32_t DwordX4 = 0x0eu;
constexpr std::uint32_t DwordX3 = 0x0fu;

struct Case {
    const char* name;
    std::uint32_t opcode;
    std::uint32_t components;
    std::uint32_t bytes;
    bool coherent;
    bool gpu;
    std::uint32_t first;
    std::uint32_t second;
    std::uint32_t view;
    std::uint32_t records;
    std::uint32_t word3;
};

constexpr std::array<Case, 24> Cases{{
    {"dwordx3", DwordX3, 3, 4, false, false, 0, 8, 0, 40, Raw},
    {"dwordx3 view+1", DwordX3, 3, 4, false, false, 4, 0, 1, 44, Raw},
    {"dwordx3 view+2", DwordX3, 3, 4, false, false, 0, 12, 2, 36, Raw},
    {"dwordx3 view+3 glc", DwordX3, 3, 4, true, false, 8, 4, 3, 48, Raw},
    {"dwordx4", DwordX4, 4, 4, false, false, 0, 16, 0, 52, Raw},
    {"dwordx4 view+1", DwordX4, 4, 4, false, false, 8, 0, 1, 60, Raw},
    {"dwordx2 view+3", DwordX2, 2, 4, false, false, 0, 4, 3, 28, Raw},
    {"dword view+1", Dword, 1, 4, false, false, 4, 0, 1, 36, Raw},
    {"ushort view+2", Ushort, 1, 2, false, false, 0, 2, 2, 40, Raw},
    {"ubyte view+3", Ubyte, 1, 1, false, false, 3, 0, 3, 36, Raw},
    {"dwordx3 without record check view+1", DwordX3, 3, 4, false, false, 0, 4, 1, 36, RawNoRecordCheck},
    {"dwordx3 without records", DwordX3, 3, 4, false, false, 0, 4, 0, 0, Raw},
    {"gpu dwordx3", DwordX3, 3, 4, false, true, 0, 8, 0, 40, Raw},
    {"gpu dwordx3 view+1", DwordX3, 3, 4, false, true, 4, 0, 1, 44, Raw},
    {"gpu dwordx3 view+2 glc", DwordX3, 3, 4, true, true, 0, 12, 2, 36, Raw},
    {"gpu dwordx4 view+3", DwordX4, 4, 4, false, true, 8, 0, 3, 60, Raw},
    {"gpu dwordx2", DwordX2, 2, 4, false, true, 0, 4, 0, 28, Raw},
    {"gpu dword view+1", Dword, 1, 4, false, true, 4, 0, 1, 36, Raw},
    {"gpu dword glc", Dword, 1, 4, true, true, 0, 4, 0, 36, Raw},
    {"gpu ushort view+2", Ushort, 1, 2, false, true, 0, 2, 2, 40, Raw},
    {"gpu ubyte view+3", Ubyte, 1, 1, false, true, 3, 0, 3, 36, Raw},
    {"gpu dwordx3 without records", DwordX3, 3, 4, false, true, 0, 4, 0, 0, Raw},
    {"gpu dwordx3 exec-off lanes past the guest block", DwordX3, 3, 4, false, true, 0, 4, 0, PastBlock, Raw},
    {"gpu dwordx4 glc exec-off lanes past the guest block", DwordX4, 4, 4, true, true, 4, 0, 1, PastBlock, Raw},
}};

class GuestBlock {
public:
    GuestBlock() {
#ifdef _WIN32
        block = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, BlockBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
        block = static_cast<std::uint8_t*>(std::aligned_alloc(BlockBytes, BlockBytes));
#endif
        Require(block != nullptr, "buffer access helpers: cannot allocate the guest block");
        GuestAllocations::Mutation().Add(block, BlockBytes, true, false, true);
    }

    ~GuestBlock() {
        GuestAllocations::Mutation().Remove(block);
#ifdef _WIN32
        VirtualFree(block, 0, MEM_RELEASE);
#else
        std::free(block);
#endif
    }

    GuestBlock(const GuestBlock&) = delete;
    GuestBlock& operator=(const GuestBlock&) = delete;

    std::uint8_t* Data() { return block; }

private:
    std::uint8_t* block = nullptr;
};

std::array<std::uint32_t, 4> BufferDescriptor(const void* data, std::uint32_t bytes, std::uint32_t word3) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, word3};
}

std::string Hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08x", value);
    return text;
}

std::uint32_t Activity(std::uint32_t lane) {
    return std::array<std::uint32_t, 4>{3u, 1u, 2u, 0u}[lane % 4u];
}

std::uint32_t RangeOffset(const Case& c, std::uint32_t lane) {
    const auto n = c.records;
    const std::array<std::uint32_t, 4> tail = c.bytes == 1 ? std::array<std::uint32_t, 4>{n - 3, n - 1, 3, 7} : c.bytes == 2 ? std::array<std::uint32_t, 4>{n - 4, n - 2, 2, 6} : std::array<std::uint32_t, 4>{n - 24, n - 28, 32, 36};
    const std::array<std::uint32_t, Lanes> offsets{0, 4, 8, 12, 16, 20, 24, 28,
            n - 20, n - 16, n - 12, n - 8, n - 4, n, n + 4, n + 8,
            0xfffffffcu, 0xfffffff8u, 0xfffffff4u, 0xfffffff0u, 0x80000000u, 0x7ffffffcu, 0xffffff00u, 0x00010000u,
            tail[0], tail[1], tail[2], tail[3], 0, 4, n - 8, n - 4};
    return offsets[lane];
}

std::uint32_t LaneOffset(const Case& c, std::uint32_t load, std::uint32_t lane) {
    if (c.records == PastBlock) return load == 0u ? lane * 4u : (Activity(lane) & 2u) != 0u ? lane * 8u : Unmapped;
    return RangeOffset(c, load == 0u ? lane : (lane * 7u + 3u) % Lanes);
}

bool Valid(const Case& c, std::uint32_t offset) {
    if (c.records == 0) return false;
    if (c.word3 == Raw) return c.records >= c.bytes && offset <= c.records - c.bytes;
    return (offset >> 2) < c.records / 4;
}

std::uint32_t Loaded(const Case& c, const std::uint8_t* view, std::uint32_t offset) {
    std::uint32_t value = 0;
    std::memcpy(&value, view + offset, c.bytes);
    return value;
}

void Assemble(Program& program, const Case& c) {
    auto& w = program.words;
    w.fill(0xbf800000u);
    std::size_t at = 0;
    const auto emit = [&](std::uint32_t word) { w[at++] = word; };
    const std::uint32_t resource = c.gpu ? 4u : 0u;
    const std::uint32_t load = 0xe0001000u | (c.opcode << 18u) | (c.coherent ? 0x4000u : 0u);
    emit(0x34020084u);
    emit(0x34040085u);
    emit(0xe0381000u);
    emit(0x80020801u);
    for (std::uint32_t reg : {4u, 5u, 6u, 7u, 12u, 13u, 14u, 15u}) {
        emit(0x7e0002ffu | (reg << 17u));
        emit(Marker);
    }
    emit(0xbf8c3f70u);
    if (c.gpu) {
        emit(0xe0380000u);
        emit(0x80031000u);
        emit(0xbf8c3f70u);
        for (std::uint32_t word = 0; word < 4u; ++word) emit(0x7e000400u | ((16u + word) << 17u) | (256u + 16u + word));
    }
    emit(0xbea8037eu);
    emit(0x36281281u);
    emit(0x7d8a2880u);
    emit(0x877e6a28u);
    emit(load | c.first);
    emit(0x80000408u | (resource << 16u));
    emit(0xbefe0328u);
    emit(0x36281282u);
    emit(0x7d8a2880u);
    emit(0x877e6a28u);
    emit(load | c.second);
    emit(0x80000c0au | (resource << 16u));
    emit(0xbf8c3f70u);
    emit(0xbefe0328u);
    emit(0xe0781000u);
    emit(0x80010402u);
    emit(0xe0781010u);
    emit(0x80010c02u);
    emit(0xbf810000u);
}

void Run(AgcDriver::VulkanDevice& device, GuestBlock& guest, Program& program, const Case& c) {
    std::uint8_t* data = c.gpu ? guest.Data() : Input.data();
    std::memset(data, 0, c.gpu ? BlockBytes : InputBytes);
    for (std::uint32_t index = 0; index < InputBytes; ++index) data[index] = static_cast<std::uint8_t>(index * 37u + 11u + (index >> 3u));
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
        Params[lane * 4u] = LaneOffset(c, 0u, lane);
        Params[lane * 4u + 1u] = Activity(lane);
        Params[lane * 4u + 2u] = LaneOffset(c, 1u, lane);
        Params[lane * 4u + 3u] = 0u;
    }
    Output.fill(0xcdcdcdcdu);
    Assemble(program, c);
    const auto* view = data + c.view;
    const auto input = BufferDescriptor(view, c.records, c.word3);
    Descriptor = input;
    std::vector<std::uint32_t> userData;
    for (const auto& descriptor : {c.gpu ? std::array<std::uint32_t, 4>{} : input, BufferDescriptor(Output.data(), decltype(Output)::ByteSize, Raw), BufferDescriptor(Params.data(), decltype(Params)::ByteSize, Raw), BufferDescriptor(Descriptor.data(), decltype(Descriptor)::ByteSize, Raw)}) {
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
        for (std::uint32_t load = 0; load < 2u; ++load) {
            for (std::uint32_t dword = 0; dword < 4; ++dword) {
                std::uint32_t expected = Marker;
                if ((Activity(lane) & (1u << load)) != 0u && dword < c.components) {
                    const auto offset = LaneOffset(c, load, lane) + (load == 0u ? c.first : c.second) + dword * 4u;
                    expected = Valid(c, offset) ? Loaded(c, view, offset) : 0u;
                }
                const auto actual = Output[lane * 8u + load * 4u + dword];
                Require(actual == expected, std::string("buffer access helpers ") + c.name + ": lane " + std::to_string(lane) + " load " + std::to_string(load + 1u) + " dword " + std::to_string(dword) + " is " + Hex(actual) + ", expected " + Hex(expected));
            }
        }
    }
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        GuestBlock guest;
        static Program programs[Cases.size()];
        for (std::size_t index = 0; index < Cases.size(); ++index) Run(*device, guest, programs[index], Cases[index]);
        std::puts("buffer access helpers tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
