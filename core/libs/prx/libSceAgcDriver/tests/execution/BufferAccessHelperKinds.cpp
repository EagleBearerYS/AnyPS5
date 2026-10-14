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
constexpr std::size_t BlockBytes = 65536;
constexpr std::size_t TargetBytes = 2048;
constexpr std::uint32_t ProgramWords = 96;
TestGuest::Array<std::uint32_t, Lanes * 16> Params;
TestGuest::Array<std::uint32_t, Lanes * 16> Output;
TestGuest::Array<std::uint32_t, 4> Descriptor;
TestGuest::Array<std::uint8_t, TargetBytes> Bound;

struct alignas(256) Program {
    std::array<std::uint32_t, ProgramWords> words{};
};

enum class Kind { Store, Scalar, Atomic };

struct Case {
    const char* name;
    Kind kind;
    std::uint32_t opcode;
    std::uint32_t components;
    std::uint32_t bytes;
    bool coherent;
    bool gpu;
    std::uint32_t second;
    std::uint32_t view;
    std::uint32_t records;
};

constexpr std::uint32_t StoreByte = 0x18u;
constexpr std::uint32_t StoreShort = 0x1au;
constexpr std::uint32_t StoreDword = 0x1cu;
constexpr std::uint32_t StoreDwordX2 = 0x1du;
constexpr std::uint32_t StoreDwordX4 = 0x1eu;
constexpr std::uint32_t StoreDwordX3 = 0x1fu;
constexpr std::uint32_t AtomicAdd = 0x32u;
constexpr std::uint32_t AtomicUMax = 0x38u;

constexpr std::array<Case, 22> Cases{{
    {"store dwordx4", Kind::Store, StoreDwordX4, 4, 4, false, false, 4, 0, 1024},
    {"store dwordx4 view+1", Kind::Store, StoreDwordX4, 4, 4, false, false, 4, 1, 1024},
    {"store dwordx3 view+2 glc", Kind::Store, StoreDwordX3, 3, 4, true, false, 0, 2, 1000},
    {"store dwordx2 view+3", Kind::Store, StoreDwordX2, 2, 4, false, false, 8, 3, 1020},
    {"store dword", Kind::Store, StoreDword, 1, 4, false, false, 4, 0, 1023},
    {"store short view+1", Kind::Store, StoreShort, 1, 2, false, false, 2, 1, 1022},
    {"store byte view+3 glc", Kind::Store, StoreByte, 1, 1, true, false, 1, 3, 1021},
    {"store short glc", Kind::Store, StoreShort, 1, 2, true, false, 6, 0, 1023},
    {"gpu store dwordx4", Kind::Store, StoreDwordX4, 4, 4, false, true, 4, 0, 1024},
    {"gpu store dwordx3 view+1 glc", Kind::Store, StoreDwordX3, 3, 4, true, true, 0, 1, 1000},
    {"gpu store dword view+2", Kind::Store, StoreDword, 1, 4, false, true, 4, 2, 1023},
    {"gpu store short view+3", Kind::Store, StoreShort, 1, 2, false, true, 2, 3, 1022},
    {"gpu store byte", Kind::Store, StoreByte, 1, 1, false, true, 1, 0, 1021},
    {"scalar loads", Kind::Scalar, 0, 1, 4, false, false, 0, 0, 1024},
    {"scalar loads view+4", Kind::Scalar, 0, 1, 4, false, false, 0, 4, 1022},
    {"gpu scalar loads", Kind::Scalar, 0, 1, 4, false, true, 0, 0, 1024},
    {"gpu scalar loads view+8", Kind::Scalar, 0, 1, 4, false, true, 0, 8, 1021},
    {"atomic add and umax", Kind::Atomic, AtomicAdd, 1, 4, false, false, 4, 0, 1024},
    {"atomic add and umax view+4", Kind::Atomic, AtomicAdd, 1, 4, false, false, 8, 4, 1020},
    {"gpu atomic add and umax", Kind::Atomic, AtomicAdd, 1, 4, false, true, 4, 0, 1024},
    {"gpu atomic add and umax view+8", Kind::Atomic, AtomicAdd, 1, 4, false, true, 0, 8, 1012},
    {"gpu atomic add and umax without records", Kind::Atomic, AtomicAdd, 1, 4, false, true, 4, 0, 0},
}};

constexpr std::array<std::uint32_t, 10> ScalarOffsets{0u, 4u, 8u, 0x3f8u, 0x3fau, 0x3fcu, 0x3feu, 0x400u, 0x404u, 0x1ffff0u};
constexpr std::array<std::uint32_t, 2> ScalarRuntime{0xfffffffcu, 12u};

class GuestBlock {
public:
    GuestBlock() {
#ifdef _WIN32
        block = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, BlockBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
        block = static_cast<std::uint8_t*>(std::aligned_alloc(BlockBytes, BlockBytes));
#endif
        Require(block != nullptr, "buffer access helper kinds: cannot allocate the guest block");
        GuestAllocations::Mutation().Add(block, BlockBytes, true, true, true);
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

std::uint32_t Hash(std::uint32_t value) {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

std::uint32_t Activity(std::uint32_t lane) {
    if (lane >= 20u) return 3u;
    return std::array<std::uint32_t, 4>{3u, 1u, 2u, 0u}[lane % 4u];
}

std::uint32_t FirstOffset(const Case& c, std::uint32_t lane) {
    const auto n = c.records & ~(c.bytes - 1u);
    const std::uint32_t slot = c.kind == Kind::Atomic ? 4u : 16u;
    if (lane < 20u) return lane * slot;
    const std::array<std::uint32_t, 12> tail{c.kind == Kind::Atomic ? n - 4u : n - 12u, n, n + 4u, 0xfff00000u, 0x7ffffffcu, 0x80000000u, 0xffff0000u, 0x10000u, n + 16u, 0xffffff00u, n + 64u, 0x40000000u};
    return tail[lane - 20u];
}

std::uint32_t SecondOffset(const Case& c, std::uint32_t lane) {
    const auto n = c.records & ~(c.bytes - 1u);
    const std::uint32_t slot = c.kind == Kind::Atomic ? 4u : 16u;
    if (lane < 20u) return 512u + lane * slot;
    if (lane == 20u) return (c.kind == Kind::Atomic || c.bytes == 4u ? n - 8u : n - 6u) - c.second;
    return FirstOffset(c, lane);
}

bool Valid(const Case& c, std::uint32_t offset) {
    return c.records >= c.bytes && offset <= c.records - c.bytes;
}

void Assemble(Program& program, const Case& c) {
    auto& w = program.words;
    w.fill(0xbf800000u);
    std::size_t at = 0;
    const auto emit = [&](std::uint32_t word) { w[at++] = word; };
    const std::uint32_t resource = c.gpu ? 4u : 0u;
    emit(0x34020086u);
    emit(0x34040086u);
    emit(0xe0381000u);
    emit(0x80020801u);
    emit(0xe0381010u);
    emit(0x80020401u);
    emit(0xe0381020u);
    emit(0x80020c01u);
    if (c.gpu) {
        emit(0xe0380000u);
        emit(0x80031000u);
    }
    emit(0xbf8c3f70u);
    if (c.gpu) {
        for (std::uint32_t word = 0; word < 4u; ++word) emit(0x7e000400u | ((16u + word) << 17u) | (256u + 16u + word));
    }
    if (c.kind == Kind::Scalar) {
        const std::uint32_t base = c.gpu ? 8u : 0u;
        for (std::uint32_t index = 0; index < ScalarOffsets.size(); ++index) {
            emit(0xf4200000u | ((20u + index) << 6u) | base);
            emit(0xfa000000u | ScalarOffsets[index]);
        }
        for (std::uint32_t index = 0; index < ScalarRuntime.size(); ++index) {
            emit(0xf4200004u | ((40u + index) << 6u));
            emit(0xfa000000u | (56u + index * 4u));
        }
        emit(0xbf8cc07fu);
        for (std::uint32_t index = 0; index < ScalarRuntime.size(); ++index) {
            emit(0xf4200000u | ((30u + index) << 6u) | base);
            emit(((40u + index) << 25u) | 8u);
        }
        emit(0xbf8cc07fu);
        for (std::uint32_t index = 0; index < 12u; ++index) emit(0x7e000200u | ((4u + index) << 17u) | (index < ScalarOffsets.size() ? 20u + index : 30u + index - static_cast<std::uint32_t>(ScalarOffsets.size())));
        emit(0xe0781000u);
        emit(0x80010402u);
        emit(0xe0781010u);
        emit(0x80010802u);
        emit(0xe0781020u);
        emit(0x80010c02u);
        emit(0xbf810000u);
        Require(at <= w.size(), "buffer access helper kinds: the program does not fit");
        return;
    }
    if (c.kind == Kind::Atomic) {
        emit(0x7e280304u);
        emit(0x7e2a030cu);
    }
    emit(0xbea8037eu);
    emit(0x363c1281u);
    emit(0x7d8a3c80u);
    emit(0x877e6a28u);
    if (c.kind == Kind::Store) {
        const std::uint32_t store = 0xe0001000u | (c.opcode << 18u) | (c.coherent ? 0x4000u : 0u);
        emit(store);
        emit(0x80000408u | (resource << 16u));
        emit(0xbefe0328u);
        emit(0x363c1282u);
        emit(0x7d8a3c80u);
        emit(0x877e6a28u);
        emit(store | c.second);
        emit(0x80000c0au | (resource << 16u));
        emit(0xbf8c3f70u);
        emit(0xbefe0328u);
        emit(0xbf810000u);
    } else {
        emit(0xe0005000u | (AtomicAdd << 18u));
        emit(0x80001408u | (resource << 16u));
        emit(0xbefe0328u);
        emit(0x363c1282u);
        emit(0x7d8a3c80u);
        emit(0x877e6a28u);
        emit(0xe0005000u | (AtomicUMax << 18u) | c.second);
        emit(0x8000150au | (resource << 16u));
        emit(0xbf8c3f70u);
        emit(0xbefe0328u);
        emit(0xe0741000u);
        emit(0x80011402u);
        emit(0xbf810000u);
    }
    Require(at <= w.size(), "buffer access helper kinds: the program does not fit");
}

std::uint32_t ReadDword(const std::vector<std::uint8_t>& memory, std::size_t offset) {
    std::uint32_t value = 0;
    std::memcpy(&value, memory.data() + offset, 4u);
    return value;
}

void Run(AgcDriver::VulkanDevice& device, GuestBlock& guest, Program& program, const Case& c) {
    std::uint8_t* target = c.gpu ? guest.Data() : Bound.data();
    for (std::uint32_t index = 0; index < TargetBytes; ++index) target[index] = static_cast<std::uint8_t>(Hash(index + c.records) >> 5u);
    std::vector<std::uint8_t> expected(target, target + TargetBytes);
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
        Params[lane * 16u] = FirstOffset(c, lane);
        Params[lane * 16u + 1u] = Activity(lane);
        Params[lane * 16u + 2u] = SecondOffset(c, lane);
        Params[lane * 16u + 3u] = 0u;
        for (std::uint32_t index = 4u; index < 12u; ++index) Params[lane * 16u + index] = (Hash(lane * 16u + index) & 7u) == 0u ? 0u : Hash(lane * 16u + index + 977u);
        Params[lane * 16u + 14u] = ScalarRuntime[0];
        Params[lane * 16u + 15u] = ScalarRuntime[1];
    }
    Output.fill(Marker);
    Assemble(program, c);
    const auto* view = target + c.view;
    const auto input = BufferDescriptor(view, c.records, Raw);
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
    const std::string name = std::string("buffer access helper kinds ") + c.name;
    if (c.kind == Kind::Scalar) {
        const auto load = [&](std::uint32_t soffset, std::uint32_t immediate) {
            const auto address = soffset + immediate;
            const bool inBounds = address >= soffset && static_cast<std::uint64_t>(address & ~3u) + 4u <= c.records;
            return inBounds ? ReadDword(expected, c.view + (address & ~3u)) : 0u;
        };
        for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
            for (std::uint32_t index = 0; index < 12u; ++index) {
                const auto value = index < ScalarOffsets.size() ? load(0u, ScalarOffsets[index]) : load(ScalarRuntime[index - ScalarOffsets.size()], 8u);
                const auto actual = Output[lane * 16u + index];
                Require(actual == value, name + ": lane " + std::to_string(lane) + " load " + std::to_string(index) + " is " + Hex(actual) + ", expected " + Hex(value));
            }
        }
        return;
    }
    std::array<std::array<std::uint32_t, 2>, Lanes> returned{};
    for (std::uint32_t access = 0; access < 2u; ++access) {
        for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
            const bool active = (Activity(lane) & (1u << access)) != 0u;
            const auto offset = access == 0u ? FirstOffset(c, lane) : SecondOffset(c, lane) + c.second;
            if (c.kind == Kind::Atomic) {
                const auto operand = Params[lane * 16u + (access == 0u ? 4u : 8u)];
                returned[lane][access] = operand;
                if (!active) continue;
                returned[lane][access] = 0u;
                if (!Valid(c, offset)) continue;
                const auto old = ReadDword(expected, c.view + offset);
                const auto updated = access == 0u ? old + operand : std::max(old, operand);
                returned[lane][access] = old;
                std::memcpy(expected.data() + c.view + offset, &updated, 4u);
                continue;
            }
            if (!active) continue;
            for (std::uint32_t component = 0; component < c.components; ++component) {
                const auto address = offset + component * 4u;
                if (!Valid(c, address)) continue;
                const auto value = Params[lane * 16u + (access == 0u ? 4u : 8u) + component];
                std::memcpy(expected.data() + c.view + address, &value, c.bytes);
            }
        }
    }
    for (std::uint32_t index = 0; index < TargetBytes; ++index) {
        Require(target[index] == expected[index], name + ": byte " + std::to_string(index) + " is " + Hex(target[index]) + ", expected " + Hex(expected[index]));
    }
    if (c.kind != Kind::Atomic) return;
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
        for (std::uint32_t access = 0; access < 2u; ++access) {
            const auto actual = Output[lane * 16u + access];
            Require(actual == returned[lane][access], name + ": lane " + std::to_string(lane) + " atomic " + std::to_string(access + 1u) + " returned " + Hex(actual) + ", expected " + Hex(returned[lane][access]));
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
        std::puts("buffer access helper kinds tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
