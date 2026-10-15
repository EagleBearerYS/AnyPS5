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
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Lanes = 32;
constexpr std::uint32_t Raw = 0x31016facu;
constexpr std::size_t BlockBytes = 65536;
constexpr std::uint32_t Records = 1024;
constexpr std::uint32_t Marker = 0xdeadbeefu;

TestGuest::Array<std::uint32_t, 8> Descriptors;
TestGuest::Array<std::uint32_t, 8> Output;

class GuestBlock {
public:
    GuestBlock() {
#ifdef _WIN32
        block = static_cast<std::uint32_t*>(VirtualAlloc(nullptr, BlockBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
        block = static_cast<std::uint32_t*>(std::aligned_alloc(BlockBytes, BlockBytes));
#endif
        Require(block != nullptr, "scalar load segments: cannot allocate a guest block");
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

    std::uint32_t* Data() { return block; }

private:
    std::uint32_t* block = nullptr;
};

std::array<std::uint32_t, 4> BufferDescriptor(const void* data, std::uint32_t bytes) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, Raw};
}

std::uint32_t ScalarLoad(std::uint32_t destination, std::uint32_t base) {
    return 0xf4200000u | (destination << 6u) | (base / 2u);
}

std::string Hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08x", value);
    return text;
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        GuestBlock first;
        GuestBlock second;
        for (std::uint32_t index = 0; index < Records / 4u; ++index) {
            first.Data()[index] = 0x1000u + index;
            second.Data()[index] = 0x2000u + index;
        }
        const auto firstDescriptor = BufferDescriptor(first.Data(), Records);
        const auto secondDescriptor = BufferDescriptor(second.Data(), Records);
        for (std::uint32_t word = 0; word < 4u; ++word) {
            Descriptors[word] = firstDescriptor[word];
            Descriptors[4u + word] = secondDescriptor[word];
        }
        Output.fill(Marker);
        std::vector<std::uint32_t> words{0xe0380000u, 0x80000200u, 0xe0380010u, 0x80000600u, 0xbf8c3f70u};
        for (std::uint32_t word = 0; word < 8u; ++word) words.push_back(0x7e000400u | ((16u + word) << 17u) | (258u + word));
        const std::array<std::pair<std::uint32_t, std::uint32_t>, 6> loads{{{16u, 0u}, {20u, 0u}, {16u, 4u}, {20u, Records - 4u}, {16u, Records}, {20u, 8u}}};
        for (std::uint32_t index = 0; index < loads.size(); ++index) {
            words.push_back(ScalarLoad(24u + index, loads[index].first));
            words.push_back(0xfa000000u | loads[index].second);
        }
        words.push_back(0xbf8cc07fu);
        for (std::uint32_t index = 0; index < loads.size(); ++index) words.push_back(0x7e000200u | ((10u + index) << 17u) | (24u + index));
        words.insert(words.end(), {0xe0780000u, 0x80010a00u, 0xe0740010u, 0x80010e00u, 0xbf810000u});
        TestGuest::Vector<std::uint32_t> code(words.begin(), words.end());
        const std::span<const std::uint32_t> program(code.data(), code.size());
        const auto address = reinterpret_cast<std::uintptr_t>(program.data());
        TestGuest::Vector<std::uint32_t> userData;
        for (const auto& descriptor : {BufferDescriptor(Descriptors.data(), static_cast<std::uint32_t>(decltype(Descriptors)::ByteSize)), BufferDescriptor(Output.data(), static_cast<std::uint32_t>(decltype(Output)::ByteSize))}) {
            userData.insert(userData.end(), descriptor.begin(), descriptor.end());
        }
        const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{address, std::as_bytes(program)}}};
        const ShaderRecompiler::ShaderComputeStageInfo compute{{Lanes, 1, 1}, 0u, {false, false, false}, false, 1};
        ShaderRecompiler::RecompileRequest request{
            {ShaderStage::Compute, address, program, 0, {}},
            {Lanes, 0, userData, compute, std::nullopt, std::nullopt, memory},
            device->Target(),
            {0, 0, 0, 128}
        };
        request.useCache = false;
        const auto result = ShaderRecompiler::Recompile(request);
        const auto bytes = std::as_bytes(std::span(result.spirv));
        const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        Require(text.find("bda_segment") != std::string_view::npos, "scalar load segments: the scalar loads through run-time V#s do not translate through the per-invocation segment cache");
        device->Dispatch(result, 1, 1, 1, address);
        device->WaitIdle();
        const std::array<std::uint32_t, 6> expected{0x1000u, 0x2000u, 0x1001u, 0x2000u + Records / 4u - 1u, 0u, 0x2002u};
        for (std::uint32_t index = 0; index < expected.size(); ++index) {
            Require(Output[index] == expected[index], "scalar load segments: load " + std::to_string(index) + " through the " + (loads[index].first == 16u ? "first" : "second") + " run-time V# at offset " + Hex(loads[index].second) + " read " + Hex(Output[index]) + ", expected " + Hex(expected[index]));
        }
        std::puts("scalar load segments tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
