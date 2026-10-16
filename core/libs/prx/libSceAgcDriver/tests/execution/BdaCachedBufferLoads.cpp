#include "GuestTestMemory.hpp"
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
#include <algorithm>
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

constexpr std::uint32_t Threads = 64;
constexpr std::size_t BlockBytes = 65536;
constexpr std::uint32_t Loads = 4;
constexpr std::uint32_t LaneStride = 12;
constexpr std::uint32_t OutputStride = Loads * 12;
constexpr std::uint32_t Fill = 0xcdcdcdcdu;
constexpr std::uint32_t Raw = 0x31016facu;
constexpr std::array<std::uint32_t, Loads> LoadOffsets{0, 0, 4, 8};
constexpr std::array<std::uint32_t, Loads> LoadSource{0, 1, 0, 0};

alignas(256) std::array<std::array<std::uint32_t, 25>, 2> Programs{};

std::span<const std::uint32_t> Code(bool coherent) {
    auto& program = Programs[coherent ? 1u : 0u];
    const std::uint32_t glc = coherent ? 0x4000u : 0u;
    const std::array<std::uint32_t, 25> code{
        0xf4080100, 0xfa000000, 0xf4080200, 0xfa000010, 0xbf8cc07f, 0x1602008c,
        0xe03c1000 | glc, 0x80010201, 0xe03c1000 | glc, 0x80020501, 0xe03c1004 | glc, 0x80010801, 0xe03c1008 | glc, 0x80010b01,
        0xbf8c3f70, 0x161c00b0,
        0xe07c1000, 0x8003020e, 0xe07c100c, 0x8003050e, 0xe07c1018, 0x8003080e, 0xe07c1024, 0x80030b0e,
        0xbf810000,
    };
    std::copy(code.begin(), code.end(), program.begin());
    return program;
}

class GuestBlock {
public:
    GuestBlock() {
#ifdef _WIN32
        block = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, BlockBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
        block = static_cast<std::uint8_t*>(std::aligned_alloc(BlockBytes, BlockBytes));
#endif
        Require(block != nullptr, "BDA cached buffer loads: cannot allocate a guest block");
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

struct Source {
    std::uint8_t* base;
    std::uint32_t records;
};

struct Case {
    const char* name;
    std::uint32_t firstBlock;
    std::uint32_t firstOffset;
    std::uint32_t firstRecords;
    std::uint32_t secondBlock;
    std::uint32_t secondOffset;
    std::uint32_t secondRecords;
};

std::string Hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08x", value);
    return text;
}

std::array<std::uint32_t, 4> Descriptor(const void* data, std::uint32_t records) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), records, Raw};
}

ShaderRecompiler::RecompileResult Recompile(std::span<const std::uint32_t> code, std::uint32_t waveSize, std::span<const std::uint32_t> userData, const ShaderRecompiler::SpirvTarget& target) {
    const std::array<ShaderRecompiler::MemoryRegion, 1> regions{{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{Threads, 1, 1}, 0, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
        {waveSize, 0, userData, compute, std::nullopt, std::nullopt, regions},
        target,
        {0, 0, 0, 128}
    };
    request.useCache = false;
    return ShaderRecompiler::Recompile(request);
}

void Run(AgcDriver::VulkanDevice& device, std::array<GuestBlock, 2>& blocks, const Case& test, bool coherent, std::uint32_t waveSize, const ShaderRecompiler::SpirvTarget& target, const std::string& mode) {
    for (std::uint32_t block = 0; block < 2u; ++block) {
        for (std::size_t byte = 0; byte < BlockBytes; ++byte) blocks[block].Data()[byte] = static_cast<std::uint8_t>(byte * 7u + (byte >> 8u) * 13u + block * 101u + 1u);
    }
    const std::array<Source, 2> sources{{{blocks[test.firstBlock].Data() + test.firstOffset, test.firstRecords}, {blocks[test.secondBlock].Data() + test.secondOffset, test.secondRecords}}};
    TestGuest::Array<std::uint32_t, 8> table;
    const auto first = Descriptor(sources[0].base, sources[0].records);
    const auto second = Descriptor(sources[1].base, sources[1].records);
    std::copy(first.begin(), first.end(), table.begin());
    std::copy(second.begin(), second.end(), table.begin() + 4);
    TestGuest::Array<std::uint32_t, Threads * OutputStride / 4u> output;
    output.fill(Fill);
    const auto tableAddress = reinterpret_cast<std::uintptr_t>(table.data());
    const auto out = Descriptor(output.data(), static_cast<std::uint32_t>(decltype(output)::ByteSize));
    std::array<std::uint32_t, 16> userData{};
    userData[0] = static_cast<std::uint32_t>(tableAddress);
    userData[1] = static_cast<std::uint32_t>(tableAddress >> 32u);
    std::copy(out.begin(), out.end(), userData.begin() + 12);
    const auto code = Code(coherent);
    const auto result = Recompile(code, waveSize, userData, target);
    device.Dispatch(result, 1, 1, 1, reinterpret_cast<std::uintptr_t>(code.data()));
    device.WaitIdle();
    const std::string label = std::string("BDA cached buffer loads: ") + test.name + ", " + mode + (coherent ? " glc" : "");
    for (std::uint32_t lane = 0; lane < Threads; ++lane) {
        for (std::uint32_t load = 0; load < Loads; ++load) {
            const auto& source = sources[LoadSource[load]];
            for (std::uint32_t component = 0; component < 3u; ++component) {
                const std::uint32_t offset = lane * LaneStride + LoadOffsets[load] + component * 4u;
                std::uint32_t expected = 0;
                if (offset + 4u <= source.records) std::memcpy(&expected, source.base + offset, sizeof(expected));
                const auto value = output[(lane * OutputStride) / 4u + load * 3u + component];
                Require(value == expected, label + ": lane " + std::to_string(lane) + " load " + std::to_string(load) + " component " + std::to_string(component) + " is " + Hex(value) + ", expected " + Hex(expected));
            }
        }
    }
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        std::array<GuestBlock, 2> blocks;
        const auto native = device->Target();
        const auto split = device->ComputeTarget(32);
        const std::array<Case, 5> cases{{
            {"two blocks", 0, 0x100, 4096, 1, 0x200, 4096},
            {"one block, two buffers", 0, 0x100, 4096, 0, 0x1000, 4096},
            {"records end inside the loads", 0, 0, 397, 1, 0, 230},
            {"unaligned first base", 0, 2, 4096, 1, 0, 4096},
            {"buffer ends at the block end", 0, BlockBytes - 760, 760, 1, BlockBytes - 768, 768},
        }};
        for (const auto& test : cases) {
            for (const bool coherent : {false, true}) {
                Run(*device, blocks, test, coherent, 32, native, "wave32");
                Run(*device, blocks, test, coherent, 64, native, "wave64");
                if (split.subgroupSize != native.subgroupSize) Run(*device, blocks, test, coherent, 64, split, "wave64 split");
            }
        }
        std::puts("BDA cached buffer load tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
