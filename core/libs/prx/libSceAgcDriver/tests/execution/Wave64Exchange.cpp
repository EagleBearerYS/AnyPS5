#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t MaxThreads = 128;
constexpr std::uint32_t Results = 18;
constexpr std::uint32_t Stride = 32;
constexpr std::uint32_t Untouched = 0xdeadbeefu;
alignas(256) std::array<std::uint32_t, MaxThreads * Stride> Output{};

constexpr std::uint32_t Vcc = 106;
constexpr std::uint32_t Exec = 126;
constexpr std::uint32_t Literal = 0xff;
constexpr std::uint32_t Vgpr = 0x100;

std::uint32_t Int(std::uint32_t value) {
    return 0x80u + value;
}

class Assembler {
public:
    void Sop1(std::uint32_t op, std::uint32_t sdst, std::uint32_t ssrc0) { code.push_back(0xbe800000u | (sdst << 16u) | (op << 8u) | ssrc0); }
    void Sop2(std::uint32_t op, std::uint32_t sdst, std::uint32_t ssrc0, std::uint32_t ssrc1) { code.push_back(0x80000000u | (op << 23u) | (sdst << 16u) | (ssrc1 << 8u) | ssrc0); }
    void Movk(std::uint32_t sdst, std::uint32_t value) { code.push_back(0xb0000000u | (sdst << 16u) | value); }
    void Vop1(std::uint32_t op, std::uint32_t vdst, std::uint32_t src0) { code.push_back(0x7e000000u | (vdst << 17u) | (op << 9u) | src0); }
    void Vop2(std::uint32_t op, std::uint32_t vdst, std::uint32_t src0, std::uint32_t vsrc1) { code.push_back((op << 25u) | (vdst << 17u) | (vsrc1 << 9u) | src0); }
    void Vopc(std::uint32_t op, std::uint32_t src0, std::uint32_t vsrc1) { code.push_back(0x7c000000u | (op << 17u) | (vsrc1 << 9u) | src0); }
    void ReadLane(std::uint32_t sdst, std::uint32_t vsrc, std::uint32_t lane) { code.insert(code.end(), {0xd7600000u | sdst, Vgpr + vsrc | (lane << 9u)}); }
    void Ds(std::uint32_t op, std::uint32_t vdst, std::uint32_t address, std::uint32_t data) { code.insert(code.end(), {0xd8000000u | (op << 18u), (vdst << 24u) | (data << 8u) | address}); }
    void Store(std::uint32_t vdata, std::uint32_t slot) { code.insert(code.end(), {0xe0701000u | (slot * 4u), 0x80010001u | (vdata << 8u)}); }
    void Sopp(std::uint32_t op, std::uint32_t simm) { code.push_back(0xbf800000u | (op << 16u) | (simm & 0xffffu)); }
    void Word(std::uint32_t word) { code.push_back(word); }
    std::size_t Here() const { return code.size(); }
    void BranchTo(std::uint32_t op, std::size_t target) { Sopp(op, static_cast<std::uint32_t>(static_cast<std::int32_t>(target) - static_cast<std::int32_t>(code.size()) - 1)); }

    std::vector<std::uint32_t> code;
};

constexpr std::uint32_t SMovB32 = 0x03, SMovB64 = 0x04, SBcnt1B64 = 0x10, SAndSaveexecB64 = 0x24;
constexpr std::uint32_t SAddU32 = 0x00, SXorB64 = 0x13;
constexpr std::uint32_t VMov = 0x01, VReadFirstLane = 0x02;
constexpr std::uint32_t VAdd = 0x25, VSub = 0x26, VAnd = 0x1b, VXor = 0x1d, VShl = 0x1a, VShr = 0x16, VMulU24 = 0x0b;
constexpr std::uint32_t CmpLt = 0xc1, CmpEq = 0xc2, CmpGt = 0xc4;
constexpr std::uint32_t Execz = 0x08, Execnz = 0x09, Barrier = 0x0a, Waitcnt = 0x0c, Endpgm = 0x01;

void Waterfall(Assembler& a, std::uint32_t index, std::uint32_t count, std::uint32_t seen, std::uint32_t order) {
    a.Movk(count, 0);
    a.Sop1(SMovB64, 26, Exec);
    const auto loop = a.Here();
    a.Vop1(VReadFirstLane, 28, Vgpr + index);
    a.Vopc(CmpEq, 28, index);
    a.Sop1(SAndSaveexecB64, 30, Vcc);
    a.Sop2(SAddU32, count, count, Int(1));
    a.Vop1(VMov, seen, 28);
    a.Vop1(VMov, order, count);
    a.Sop2(SXorB64, Exec, Exec, 30);
    a.BranchTo(Execnz, loop);
    a.Sop1(SMovB64, Exec, 26);
}

void SkipRegion(Assembler& a, std::uint32_t compare, std::uint32_t bound, std::uint32_t sdst, std::uint32_t value) {
    a.Vopc(compare, Int(bound), 0);
    a.Sop1(SAndSaveexecB64, 18, Vcc);
    a.Sopp(Execz, 1);
    a.Movk(sdst, value);
    a.Sop1(SMovB64, Exec, 18);
}

std::vector<std::uint32_t> Program() {
    Assembler a;
    a.Vop2(VShl, 1, Int(7), 0);
    a.Vop2(VAnd, 0, Int(63), 0);
    a.Vop2(VAnd, 3, Int(9), 0);
    a.Vopc(CmpEq, Int(9), 3);
    a.Sop1(SBcnt1B64, 10, Vcc);
    a.Sop1(SMovB64, 12, Vcc);
    a.Vop1(VMov, 4, 10);
    a.Vop1(VMov, 5, 12);
    a.Vop1(VMov, 6, 13);
    a.Vop2(VMulU24, 7, Int(7), 0);
    a.Vop2(VAdd, 7, Int(3), 7);
    a.ReadLane(14, 7, Int(37));
    a.ReadLane(15, 7, Int(5));
    a.ReadLane(16, 7, Int(63));
    a.Vop1(VMov, 8, 14);
    a.Vop1(VMov, 9, 15);
    a.Vop1(VMov, 10, 16);
    a.Vopc(CmpLt, Int(40), 0);
    a.Sop1(SAndSaveexecB64, 18, Vcc);
    a.Vop1(VReadFirstLane, 20, Vgpr + 7);
    a.Sop1(SMovB64, Exec, 18);
    a.Vop1(VMov, 11, 20);
    a.Vopc(CmpGt, Int(2), 0);
    a.Sop1(SAndSaveexecB64, 18, Vcc);
    a.Vop1(VReadFirstLane, 21, Vgpr + 7);
    a.Sop1(SMovB64, Exec, 18);
    a.Vop1(VMov, 12, 21);
    a.Movk(22, 0);
    a.Movk(23, 0);
    a.Movk(24, 0);
    SkipRegion(a, CmpLt, 47, 22, 0x11);
    SkipRegion(a, CmpGt, 8, 23, 0x22);
    a.Vopc(CmpLt, Literal, 0);
    a.Word(100);
    a.Sop1(SAndSaveexecB64, 18, Vcc);
    a.Sopp(Execz, 1);
    a.Movk(24, 0x33);
    a.Sop1(SMovB64, Exec, 18);
    a.Vop1(VMov, 13, 22);
    a.Vop1(VMov, 14, 23);
    a.Vop1(VMov, 15, 24);
    a.Vop2(VShr, 16, Int(4), 0);
    Waterfall(a, 16, 25, 17, 18);
    a.Vop1(VMov, 19, 25);
    a.Vop2(VShr, 20, Int(5), 0);
    Waterfall(a, 20, 32, 21, 22);
    a.Vop1(VMov, 26, 32);
    a.Vop2(VMulU24, 22, Int(13), 0);
    a.Vop2(VShl, 23, Int(2), 0);
    a.Ds(0x0d, 0, 23, 22);
    a.Sopp(Waitcnt, 0xc07f);
    a.Vop2(VXor, 24, Int(63), 0);
    a.Vop2(VShl, 24, Int(2), 24);
    a.Ds(0x36, 25, 24, 0);
    a.Sopp(Waitcnt, 0xc07f);
    a.Vop2(VAdd, 27, Int(1), 22);
    a.Sopp(Barrier, 0);
    a.Ds(0x0d, 0, 23, 27);
    a.Sopp(Waitcnt, 0xc07f);
    a.Sopp(Barrier, 0);
    a.Vop2(VSub, 28, Int(31), 0);
    a.Vop2(VAnd, 28, Int(63), 28);
    a.Vop2(VShl, 28, Int(2), 28);
    a.Ds(0x36, 29, 28, 0);
    a.Sopp(Waitcnt, 0xc07f);
    for (const auto [vgpr, slot] : std::array<std::pair<std::uint32_t, std::uint32_t>, Results>{{{4, 0}, {5, 1}, {6, 2}, {8, 3}, {9, 4}, {10, 5}, {11, 6}, {12, 7}, {13, 8}, {14, 9}, {15, 10}, {17, 11}, {18, 12}, {19, 13}, {25, 14}, {29, 15}, {26, 16}, {21, 17}}}) a.Store(vgpr, slot);
    a.Sopp(Endpgm, 0);
    return a.code;
}

std::array<std::uint32_t, Results> Expected(std::uint32_t tid) {
    const std::uint32_t lane = tid & 63u;
    const auto value = [](std::uint32_t source) { return source * 7u + 3u; };
    return {16u, 0xaa00aa00u, 0xaa00aa00u, value(37), value(5), value(63), value(41), value(0), 0x11u, 0x22u, 0u, lane >> 4u, (lane >> 4u) + 1u, 4u, (lane ^ 63u) * 13u, ((31u - lane) & 63u) * 13u + 1u, 2u, lane >> 5u};
}

constexpr std::array<const char*, Results> Names{"bcnt1 of a ballot", "ballot low word", "ballot high word", "v_readlane 37", "v_readlane 5", "v_readlane 63", "v_readfirstlane above 40", "v_readfirstlane below 2", "execz region of the upper half", "execz region of the lower half", "execz region of no lane", "waterfall index", "waterfall iteration", "waterfall iterations", "LDS mirror", "LDS across a barrier", "half-uniform waterfall iterations", "half-uniform waterfall index"};

std::uint32_t Run(AgcDriver::VulkanDevice& device, std::span<const std::uint32_t> code, std::uint32_t threads) {
    Output.fill(Untouched);
    std::vector<std::uint32_t> userData(8, 0u);
    const auto address = reinterpret_cast<std::uintptr_t>(Output.data());
    userData[4] = static_cast<std::uint32_t>(address);
    userData[5] = static_cast<std::uint32_t>((address >> 32u) & 0xffffu);
    userData[6] = static_cast<std::uint32_t>(Output.size() * 4u);
    userData[7] = 0x31016facu;
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{threads, 1, 1}, 64u, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
        {64u, 0, userData, compute, std::nullopt, std::nullopt, memory},
        device.ComputeTarget(64u),
        {0, 0, 0, 128}
    };
    request.useCache = false;
    const auto result = ShaderRecompiler::Recompile(request);
    device.Dispatch(result, 1, 1, 1, reinterpret_cast<std::uintptr_t>(code.data()));
    device.WaitIdle();
    const auto& words = result.spirv.Words();
    for (std::size_t index = 5; index < words.size() && (words[index] >> 16u) != 0u; index += words[index] >> 16u) {
        if ((words[index] & 0xffffu) == spv::OpExecutionMode && words[index + 2] == spv::ExecutionModeLocalSize) return 64u * words[index + 3] / threads;
    }
    throw std::runtime_error("wave64 exchange: the compute program has no LocalSize");
}

void Check(std::uint32_t threads) {
    for (std::uint32_t tid = 0; tid < threads; ++tid) {
        const auto expected = Expected(tid);
        for (std::uint32_t index = 0; index < Results; ++index) {
            const auto actual = Output[tid * Stride + index];
            Require(actual == expected[index], "wave64 exchange, " + std::to_string(threads) + " threads: lane " + std::to_string(tid) + " " + Names[index] + " is " + std::to_string(actual) + ", expected " + std::to_string(expected[index]));
        }
    }
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        const auto code = Program();
        const auto subgroup = device->ComputeTarget(64u).subgroupSize;
        for (const std::uint32_t threads : {64u, 128u}) {
            const auto lanes = Run(*device, code, threads);
            Check(threads);
            if (subgroup == 32u) Require(lanes == (threads == 64u ? 64u : 32u), "wave64 exchange: " + std::to_string(threads) + " threads ran at " + std::to_string(lanes) + " invocations per wave");
        }
        std::puts("wave64 exchange tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
