#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <set>
#include <stdexcept>
#include <span>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t OutputBytes = 512;
constexpr std::uint8_t Fill = 0xdeu;
constexpr std::uint32_t SharedSame = 0;
constexpr std::uint32_t SharedDifferent = 4;
constexpr std::uint32_t Adjacent = 8;
constexpr std::uint32_t Halves = 80;
constexpr std::uint32_t PartialShared = 208;
constexpr std::uint32_t PartialDistinct = 212;
constexpr std::uint32_t VccWord = 288;

alignas(256) std::array<std::uint8_t, OutputBytes> Output{};

std::uint32_t Vop1Move(std::uint32_t vdst, std::uint32_t src) {
    return 0x7e000200u | (vdst << 17u) | src;
}

std::uint32_t Vop2(std::uint32_t op, std::uint32_t vdst, std::uint32_t src0, std::uint32_t vsrc1) {
    return (op << 25u) | (vdst << 17u) | (vsrc1 << 9u) | src0;
}

std::uint32_t StoreWord0(std::uint32_t op) {
    return 0xe0005000u | (op << 18u);
}

std::uint32_t StoreWord1(std::uint32_t vdata, std::uint32_t vaddr) {
    return 0x80010000u | (vdata << 8u) | vaddr;
}

constexpr std::uint32_t Literal = 0xffu;
constexpr std::uint32_t V0 = 0x100u;
constexpr std::uint32_t AddU32 = 0x25u;
constexpr std::uint32_t AndB32 = 0x1bu;
constexpr std::uint32_t OrB32 = 0x1cu;
constexpr std::uint32_t LshlB32 = 0x1au;
constexpr std::uint32_t LshrB32 = 0x16u;
constexpr std::uint32_t StoreByte = 0x18u;
constexpr std::uint32_t StoreShort = 0x1au;
constexpr std::uint32_t StoreDword = 0x1cu;

std::vector<std::uint32_t> Program(std::uint32_t waveSize, bool vccScalar) {
    const bool wave64 = waveSize == 64u;
    std::vector<std::uint32_t> code{
        Vop2(AndB32, 0, 0xbfu, 0),
        Vop1Move(1, 0x80u + SharedSame), Vop1Move(2, Literal), 0x5au,
        StoreWord0(StoreByte), StoreWord1(2, 1),
        Vop1Move(1, 0x80u + SharedDifferent), Vop2(AddU32, 2, 0x81u, 0),
        StoreWord0(StoreByte), StoreWord1(2, 1),
        Vop2(AddU32, 1, 0x80u + Adjacent, 0), Vop2(OrB32, 2, Literal, 0), 0x80u,
        StoreWord0(StoreByte), StoreWord1(2, 1),
        Vop2(AndB32, 3, 0x9fu, 0), Vop2(LshlB32, 3, 0x82u, 3), Vop2(LshrB32, 4, 0x85u, 0), Vop2(LshlB32, 4, 0x81u, 4),
        Vop2(AddU32, 1, V0 + 3u, 4), Vop2(AddU32, 1, Literal, 1), Halves, Vop2(AddU32, 2, Literal, 0), 0x1000u,
        StoreWord0(StoreShort), StoreWord1(2, 1),
        Vop2(AndB32, 3, 0x81u, 0), 0x7d8a0680u, wave64 ? 0xbe8a246au : 0xbe8a3c6au,
        0x7d880090u, Vop2(AddU32, 5, Literal, 0), PartialDistinct, Vop1Move(6, Literal), PartialShared, Vop2(0x01u, 1, V0 + 5u, 6),
        Vop2(AddU32, 2, 0xb0u, 0),
        StoreWord0(StoreByte), StoreWord1(2, 1),
        wave64 ? 0xbefe040au : 0xbefe030au,
    };
    if (vccScalar) code.insert(code.end(), {0x7d880085u, 0xbe88046au, 0x7e0e0208u, Vop1Move(1, Literal), VccWord, StoreWord0(StoreDword), StoreWord1(7, 1)});
    code.push_back(0xbf810000u);
    return code;
}

std::array<std::uint32_t, 4> BufferDescriptor(const void* data, std::uint32_t bytes) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, 0x31016facu};
}

std::uint32_t Run(AgcDriver::VulkanDevice& device, std::span<const std::uint32_t> code, std::uint32_t waveSize, std::uint32_t threads) {
    Output.fill(Fill);
    std::vector<std::uint32_t> userData(8, 0u);
    const auto output = BufferDescriptor(Output.data(), OutputBytes);
    std::copy(output.begin(), output.end(), userData.begin() + 4);
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{threads, 1, 1}, 0u, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
        {waveSize, 0, userData, compute, std::nullopt, std::nullopt, memory},
        device.ComputeTarget(waveSize),
        {0, 0, 0, 128}
    };
    request.useCache = false;
    const auto result = ShaderRecompiler::Recompile(request);
    device.Dispatch(result, 1, 1, 1, reinterpret_cast<std::uintptr_t>(code.data()));
    device.WaitIdle();
    const auto& words = result.spirv.Words();
    for (std::size_t index = 5; index < words.size() && (words[index] >> 16u) != 0u; index += words[index] >> 16u) {
        if ((words[index] & 0xffffu) == spv::OpExecutionMode && words[index + 2] == spv::ExecutionModeLocalSize) return words[index + 3] * waveSize / threads;
    }
    throw std::runtime_error("coherent subword stores: the compute program has no LocalSize");
}

void Expect(bool condition, const std::string& name, std::uint32_t byte, const std::string& expected) {
    Require(condition, "coherent subword stores " + name + ": byte " + std::to_string(byte) + " is " + std::to_string(Output[byte]) + ", expected " + expected);
}

void Check(const std::string& name, std::uint32_t waveSize, bool vccScalar) {
    std::vector<bool> written(OutputBytes, false);
    const auto expectByte = [&](std::uint32_t byte, std::uint32_t value) {
        Expect(Output[byte] == value, name, byte, std::to_string(value));
        written[byte] = true;
    };
    expectByte(SharedSame, 0x5au);
    std::set<std::uint32_t> lanes;
    for (std::uint32_t lane = 0; lane < waveSize; ++lane) lanes.insert(lane + 1u);
    Expect(lanes.contains(Output[SharedDifferent]), name, SharedDifferent, "the value of one of the storing lanes");
    written[SharedDifferent] = true;
    for (std::uint32_t lane = 0; lane < waveSize; ++lane) expectByte(Adjacent + lane, lane | 0x80u);
    for (std::uint32_t lane = 0; lane < waveSize; ++lane) {
        const auto byte = Halves + 4u * (lane & 31u) + 2u * (lane >> 5u);
        expectByte(byte, (0x1000u + lane) & 0xffu);
        expectByte(byte + 1u, (0x1000u + lane) >> 8u);
    }
    std::set<std::uint32_t> partial;
    for (std::uint32_t lane = 1; lane < waveSize; lane += 2) {
        if (lane < 16u) partial.insert(0x30u + lane);
        else expectByte(PartialDistinct + lane, 0x30u + lane);
    }
    Expect(partial.contains(Output[PartialShared]), name, PartialShared, "the value of one of the odd lanes below 16");
    written[PartialShared] = true;
    if (vccScalar) {
        for (std::uint32_t byte = VccWord; byte < VccWord + 4u; ++byte) expectByte(byte, byte == VccWord ? 0x1fu : 0u);
    }
    for (std::uint32_t byte = 0; byte < OutputBytes; ++byte) {
        if (!written[byte]) Expect(Output[byte] == Fill, name, byte, std::to_string(Fill) + " (no lane stores it)");
    }
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        const auto wave32 = Program(32u, false);
        const auto oneLane = Program(64u, false);
        const auto vccRead = Program(64u, true);
        struct Case {
            const char* name;
            std::span<const std::uint32_t> code;
            std::uint32_t waveSize;
            std::uint32_t threads;
            bool vccScalar;
            std::uint32_t invocations;
        };
        const std::array<Case, 4> cases{{{"wave32", wave32, 32u, 32u, false, 32u}, {"wave64", oneLane, 64u, 64u, false, 64u}, {"wave64 with a scalar vcc read", vccRead, 64u, 64u, true, 64u}, {"two wave64 waves with a scalar vcc read", vccRead, 64u, 128u, true, 32u}}};
        const auto subgroup = device->ComputeTarget(64u).subgroupSize;
        for (const auto& item : cases) {
            const auto invocations = Run(*device, item.code, item.waveSize, item.threads);
            Check(item.name, item.waveSize, item.vccScalar);
            if (subgroup == 32u) Require(invocations == item.invocations, std::string("coherent subword stores ") + item.name + ": " + std::to_string(invocations) + " invocations per wave, expected " + std::to_string(item.invocations));
        }
        std::puts("coherent subword stores tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
