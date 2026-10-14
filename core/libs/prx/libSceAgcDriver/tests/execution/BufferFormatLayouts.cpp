#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
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
constexpr std::uint32_t Formats = 77;
constexpr std::uint32_t Records = 0x300u;
constexpr std::size_t BlockBytes = 65536;
constexpr std::size_t DataBytes = 0x400;
constexpr std::uint32_t ProgramWords = 256;
constexpr std::uint32_t LaneDwords = 8;
alignas(256) std::array<std::uint32_t, Lanes * 8> Params{};
alignas(256) std::array<std::uint32_t, Formats * Lanes * LaneDwords> RuntimeOutput{};
alignas(256) std::array<std::uint32_t, 7 * Lanes * LaneDwords> TypedOutput{};
alignas(256) std::array<std::uint32_t, Formats * 4> Descriptors{};
alignas(256) std::array<std::uint8_t, DataBytes> Bound{};

struct alignas(256) Program {
    std::array<std::uint32_t, ProgramWords> words{};
};

struct Layout {
    std::uint32_t first;
    std::uint32_t last;
};

constexpr std::array<Layout, 14> Layouts{{{1, 6}, {7, 13}, {14, 19}, {20, 22}, {23, 29}, {30, 36}, {37, 43}, {44, 49}, {50, 55}, {56, 61}, {62, 64}, {65, 71}, {72, 74}, {75, 77}}};

class GuestBlock {
public:
    GuestBlock() {
#ifdef _WIN32
        block = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, BlockBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
        block = static_cast<std::uint8_t*>(std::aligned_alloc(BlockBytes, BlockBytes));
#endif
        Require(block != nullptr, "buffer format layouts: cannot allocate the guest block");
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

std::uint32_t Hash(std::uint32_t value) {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

std::uint32_t Components(std::uint32_t format) {
    if (format <= 13u || (format >= 20u && format <= 22u)) return 1u;
    if (format <= 19u || (format >= 23u && format <= 29u) || (format >= 62u && format <= 64u)) return 2u;
    if (format <= 43u || (format >= 72u && format <= 74u)) return 3u;
    return 4u;
}

std::uint32_t RuntimeWord3(std::uint32_t format) {
    std::uint32_t selectors = 0;
    for (std::uint32_t output = 0; output < 4u; ++output) selectors |= (output < Components(format) ? 4u + output : 0u) << (output * 3u);
    return (Raw & ~0x7ffffu) | (format << 12u) | selectors;
}

std::uint32_t Activity(std::uint32_t lane) {
    return std::array<std::uint32_t, 4>{1u, 1u, 0u, 1u}[lane % 4u];
}

std::uint32_t Offset(std::uint32_t lane) {
    constexpr std::array<std::uint32_t, 8> tail{Records - 16u, Records - 12u, Records - 8u, Records - 4u, Records, Records + 4u, 0xfffffff0u, 0x80000000u};
    return lane < 24u ? 4u + lane * 24u : tail[lane - 24u];
}

std::uint32_t Special(std::uint32_t index) {
    constexpr std::array<std::uint32_t, 12> values{0x7fc00000u, 0x7f800001u, 0xff800000u, 0x00000001u, 0x80000000u, 0x3f800000u, 0xbf800000u, 0x7bff7c01u, 0x03ff8001u, 0xfc00fe00u, 0x477fe000u, 0x00800000u};
    return values[index % values.size()];
}

std::uint32_t Mubuf(std::uint32_t opcode, std::uint32_t offset = 0u) {
    return 0xe0001000u | (opcode << 18u) | offset;
}

std::uint32_t Mtbuf(std::uint32_t opcode, std::uint32_t format) {
    return 0xe8001000u | (format << 19u) | ((opcode & 7u) << 16u);
}

std::uint32_t Operands(std::uint32_t address, std::uint32_t data, std::uint32_t resource, std::uint32_t soffset = 0x80u, bool highOpcode = false) {
    return address | (data << 8u) | (resource << 16u) | (highOpcode ? 0x200000u : 0u) | (soffset << 24u);
}

class Assembler {
public:
    explicit Assembler(Program& program) : words(program.words) { words.fill(0xbf800000u); }

    void operator()(std::uint32_t word) {
        Require(at < words.size(), "buffer format layouts: the program does not fit");
        words[at++] = word;
    }

    void Prologue() {
        (*this)(0x34020085u);
        (*this)(0x34040085u);
        (*this)(0xe0381000u);
        (*this)(0x80020801u);
    }

    void Markers(std::uint32_t first, std::uint32_t count) {
        for (std::uint32_t reg = first; reg < first + count; ++reg) {
            (*this)(0x7e0002ffu | (reg << 17u));
            (*this)(Marker);
        }
    }

    void MaskInactive() {
        (*this)(0xbea8037eu);
        (*this)(0x363c1281u);
        (*this)(0x7d8a3c80u);
        (*this)(0x877e6a28u);
    }

    void Restore() {
        (*this)(0xbf8c3f70u);
        (*this)(0xbefe0328u);
    }

private:
    std::array<std::uint32_t, ProgramWords>& words;
    std::size_t at = 0;
};

void AssembleRuntime(Program& program) {
    Assembler emit(program);
    emit.Prologue();
    emit(0x8f188410u);
    emit(0x8f198a10u);
    emit(0x871b8310u);
    emit(0x809a1b84u);
    emit(0xf4280506u);
    emit(0x30000000u);
    emit(0xbf8c3f70u);
    emit(0xbf8cc07fu);
    emit.Markers(4u, 4u);
    emit.Markers(12u, 2u);
    emit.MaskInactive();
    emit(Mubuf(0x03u));
    emit(Operands(10u, 4u, 5u, 26u));
    emit(Mubuf(0x83u));
    emit(Operands(10u, 12u, 5u, 26u));
    emit.Restore();
    emit(Mubuf(0x1eu));
    emit(Operands(2u, 4u, 1u, 25u));
    emit(Mubuf(0x1du, 16u));
    emit(Operands(2u, 12u, 1u, 25u));
    emit(0xbf810000u);
}

void AssembleTyped(Program& program, const Layout& layout) {
    Assembler emit(program);
    const auto members = layout.last - layout.first + 1u;
    emit.Prologue();
    for (std::uint32_t member = 0; member < members; ++member) emit(0xb0000000u | ((30u + member) << 16u) | (member * Lanes * LaneDwords * 4u));
    emit(0xbf8c3f70u);
    for (std::uint32_t member = 0; member < members; ++member) emit.Markers(16u + member * 8u, 6u);
    emit.MaskInactive();
    for (std::uint32_t member = 0; member < members; ++member) {
        emit(Mtbuf(3u, layout.first + member));
        emit(Operands(8u, 16u + member * 8u, 0u));
        emit(Mtbuf(11u, layout.first + member));
        emit(Operands(8u, 20u + member * 8u, 0u, 0x80u, true));
    }
    emit.Restore();
    for (std::uint32_t member = 0; member < members; ++member) {
        emit(Mubuf(0x1eu));
        emit(Operands(2u, 16u + member * 8u, 1u, 30u + member));
        emit(Mubuf(0x1du, 16u));
        emit(Operands(2u, 20u + member * 8u, 1u, 30u + member));
    }
    emit(0xbf810000u);
}

ShaderRecompiler::RecompileResult Dispatch(AgcDriver::VulkanDevice& device, Program& program, const std::vector<std::uint32_t>& userData, std::uint32_t groups) {
    const std::span<const std::uint32_t> code(program.words);
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{Lanes, 1, 1}, 0u, {true, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
        {Lanes, 0, userData, compute, std::nullopt, std::nullopt, memory},
        device.Target(),
        {0, 0, 0, 128}
    };
    request.useCache = false;
    auto result = ShaderRecompiler::Recompile(request);
    device.Dispatch(result, groups, 1, 1, {}, reinterpret_cast<std::uintptr_t>(code.data()));
    device.WaitIdle();
    return result;
}

std::vector<std::uint32_t> UserData(const std::array<std::uint32_t, 4>& resource, const std::array<std::uint32_t, 4>& output) {
    std::vector<std::uint32_t> userData;
    for (const auto& descriptor : {resource, output, BufferDescriptor(Params.data(), sizeof(Params), Raw), BufferDescriptor(Descriptors.data(), sizeof(Descriptors), Raw)}) {
        userData.insert(userData.end(), descriptor.begin(), descriptor.end());
    }
    return userData;
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        GuestBlock guest;
        auto* data = guest.Data();
        for (std::uint32_t offset = 0; offset < DataBytes; ++offset) data[offset] = static_cast<std::uint8_t>(Hash(offset * 131u) >> 7u);
        for (std::uint32_t index = 0; index < 12u; ++index) {
            const auto value = Special(index);
            std::memcpy(data + Offset(index * 2u + 1u), &value, 4u);
        }
        for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
            Params[lane * 8u] = Offset(lane);
            Params[lane * 8u + 1u] = Activity(lane);
            Params[lane * 8u + 2u] = Offset(lane) - 4u;
        }
        for (std::uint32_t format = 1; format <= Formats; ++format) {
            const auto view = (format - 1u) % 4u;
            const auto descriptor = BufferDescriptor(data + view, Records - view, RuntimeWord3(format));
            std::copy(descriptor.begin(), descriptor.end(), Descriptors.begin() + (format - 1u) * 4u);
        }
        std::memcpy(Bound.data(), data, DataBytes);
        RuntimeOutput.fill(0xcdcdcdcdu);
        static Program runtime;
        AssembleRuntime(runtime);
        static_cast<void>(Dispatch(*device, runtime, UserData({}, BufferDescriptor(RuntimeOutput.data(), sizeof(RuntimeOutput), Raw)), Formats));
        static Program typed[Layouts.size()];
        for (std::uint32_t index = 0; index < Layouts.size(); ++index) {
            const auto& layout = Layouts[index];
            TypedOutput.fill(0xcdcdcdcdu);
            AssembleTyped(typed[index], layout);
            static_cast<void>(Dispatch(*device, typed[index], UserData(BufferDescriptor(Bound.data(), Records, Raw), BufferDescriptor(TypedOutput.data(), sizeof(TypedOutput), Raw)), 1u));
            for (std::uint32_t format = layout.first; format <= layout.last; ++format) {
                for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
                    for (std::uint32_t dword = 0; dword < 6u; ++dword) {
                        const auto actual = RuntimeOutput[((format - 1u) * Lanes + lane) * LaneDwords + dword];
                        const auto expected = TypedOutput[((format - layout.first) * Lanes + lane) * LaneDwords + dword];
                        const std::string where = "buffer format layouts: format " + std::to_string(format) + (dword < 4u ? " xyzw" : " d16 xyzw") + " lane " + std::to_string(lane) + " dword " + std::to_string(dword < 4u ? dword : dword - 4u);
                        Require(actual == expected, where + " is " + Hex(actual) + " through the run-time format, " + Hex(expected) + " through the typed format");
                        if (Activity(lane) == 0u) Require(actual == Marker, where + " of an exec-off lane changed to " + Hex(actual));
                    }
                }
            }
        }
        std::puts("buffer format layouts tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
