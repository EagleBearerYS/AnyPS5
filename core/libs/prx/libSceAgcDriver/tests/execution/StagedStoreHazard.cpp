#include "NewPathFixture.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <chrono>
#include <exception>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32

namespace {

using namespace NewPath;
using Clock = std::chrono::steady_clock;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Lanes = 64;
constexpr std::uint32_t TargetPages = 16;
constexpr std::uint32_t TargetWords = static_cast<std::uint32_t>(TargetPages * PageBytes / 4u);
constexpr std::uint32_t LaneStride = TargetWords / Lanes;
constexpr std::uint32_t CpuOffset = 1000;
constexpr std::uint32_t Unfinished = 0xdeadbeefu;
constexpr std::uint32_t CalibrationRounds = 1u << 20u;
constexpr std::uint64_t MaxRounds = std::uint64_t{1} << 31u;
constexpr auto Busy = std::chrono::milliseconds(600);
constexpr auto CalibrationTime = std::chrono::milliseconds(50);
constexpr auto CopyInSettle = std::chrono::milliseconds(50);

int failures = 0;

void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

const Program& SpinThenStoreProgram() {
    static const Program program = [] {
        using namespace Isa;
        constexpr std::uint32_t VLshrRevB32 = 0x16u;
        constexpr std::uint32_t VXorB32 = 0x1du;
        constexpr std::uint32_t MoveS4ToS20 = 0xbe940304u;
        constexpr std::uint32_t DecrementS20 = 0x80948114u;
        constexpr std::uint32_t CompareS20NotZero = 0xbf078014u;
        constexpr std::uint32_t BranchBackWhileNotZero = 0xbf85fff7u;
        Program built;
        built << Vop2(VAddNcU32, 1, InlineInteger(1), 0) << MoveS4ToS20
              << Vop2(VLshlRevB32, 2, InlineInteger(13), 1) << Vop2(VXorB32, 1, Vgpr(1), 2)
              << Vop2(VLshrRevB32, 2, InlineInteger(17), 1) << Vop2(VXorB32, 1, Vgpr(1), 2)
              << Vop2(VLshlRevB32, 2, InlineInteger(5), 1) << Vop2(VXorB32, 1, Vgpr(1), 2)
              << DecrementS20 << CompareS20NotZero << BranchBackWhileNotZero
              << Vop2(VLshlRevB32, 3, InlineInteger(2), 0)
              << Mubuf(BufferStoreDword, 1, 3, 8)
              << Vop2(VLshlRevB32, 4, InlineInteger(14), 0)
              << Vop2(VAddNcU32, 5, InlineInteger(1), 0)
              << Mubuf(BufferStoreDword, 5, 4, 0)
              << EndProgram;
        return built;
    }();
    return program;
}

std::uint32_t Previous(std::uint32_t word) {
    return word * 2654435761u + 0x5a5a5a5au;
}

std::uint32_t Cpu(std::uint32_t word) {
    return 0xc0000000u | word;
}

struct Memory {
    PrivatePages target{TargetPages};
    PrivatePages scratch{1};

    void Reset() {
        for (std::uint32_t word = 0; word < TargetWords; ++word) target.Words()[word] = Previous(word);
        for (std::uint32_t lane = 0; lane < Lanes; ++lane) scratch.Words()[lane] = Unfinished;
        AgcDriver::GuestMemory::BumpCollectEpoch();
    }
};

std::shared_ptr<const ShaderRecompiler::RecompileResult> Compile(AgcDriver::VulkanDevice& device, const Memory& memory, std::uint32_t rounds) {
    const auto target = BufferResource(memory.target.Address(), static_cast<std::uint32_t>(memory.target.Bytes()));
    const auto scratch = BufferResource(memory.scratch.Address(), Lanes * 4u);
    const std::vector<std::uint32_t> userData{target[0], target[1], target[2], target[3], rounds, 0u, 0u, 0u, scratch[0], scratch[1], scratch[2], scratch[3]};
    const auto& program = SpinThenStoreProgram();
    const auto code = program.Code();
    const std::array<ShaderRecompiler::MemoryRegion, 1> regions{{{static_cast<std::uintptr_t>(program.Address()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{Lanes, 1, 1}, 0u, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, program.Address(), code, 0, {}},
        {64, 0, userData, compute, std::nullopt, std::nullopt, regions},
        device.Target(),
        {0, 0, 0, 128}
    };
    request.useCache = false;
    return std::make_shared<const ShaderRecompiler::RecompileResult>(ShaderRecompiler::Recompile(request));
}

std::uint32_t Calibrate(AgcDriver::VulkanDevice& device, Memory& memory) {
    for (std::uint64_t measured = 0;; measured = measured == 0u ? CalibrationRounds : measured * 4u) {
        memory.Reset();
        const auto result = Compile(device, memory, measured == 0u ? 1u : static_cast<std::uint32_t>(measured));
        std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
        const auto start = Clock::now();
        device.Dispatch(*result, 1, 1, 1, SpinThenStoreProgram().Address());
        device.WaitIdle();
        const auto took = Clock::now() - start;
        if (measured == 0u || (took < CalibrationTime && measured < MaxRounds / 4u)) continue;
        const auto microseconds = std::max<long long>(std::chrono::duration_cast<std::chrono::microseconds>(took).count(), 1);
        const auto rounds = measured * static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(Busy).count()) / static_cast<std::uint64_t>(microseconds);
        return static_cast<std::uint32_t>(std::clamp<std::uint64_t>(rounds, CalibrationRounds, MaxRounds));
    }
}

void CpuWritesDuringStagedDispatch(AgcDriver::VulkanDevice& device) {
    Memory memory;
    const auto rounds = Calibrate(device, memory);
    memory.Reset();
    const auto result = Compile(device, memory, rounds);
    const std::string what = "a dispatch storing one dword per lane, after a GPU spin, through a 1 MiB V# of host-imported memory, while the CPU writes other dwords of the V# range";
    std::string failure;
    bool running = false;
    {
        StderrCapture capture;
        try {
            {
                std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
                device.Dispatch(*result, 1, 1, 1, SpinThenStoreProgram().Address());
                device.SubmitRecorded();
            }
            std::this_thread::sleep_for(CopyInSettle);
            for (std::uint32_t lane = 0; lane < Lanes; ++lane) memory.target.Words()[lane * LaneStride + CpuOffset] = Cpu(lane * LaneStride + CpuOffset);
            running = memory.scratch.Words()[0] == Unfinished;
            std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
            device.WaitIdle();
        } catch (const std::exception& error) {
            failure = error.what();
        }
        const auto log = capture.Finish();
        if (log.find("failed") != std::string::npos) failure += "\n" + log;
    }
    Expect(failure.empty(), what + ": the dispatch failed: " + failure);
    Expect(running, what + ": the dispatch had finished when the CPU wrote");
    AgcDriver::GuestMemory::FlushGpuWrites(memory.target.Address(), memory.target.Bytes());
    std::size_t stored = 0;
    std::size_t lost = 0;
    std::size_t changed = 0;
    std::string first;
    for (std::uint32_t word = 0; word < TargetWords; ++word) {
        const auto actual = memory.target.Words()[word];
        const bool lane = word % LaneStride == 0u;
        const bool cpu = word % LaneStride == CpuOffset;
        const auto expected = lane ? word / LaneStride + 1u : cpu ? Cpu(word) : Previous(word);
        if (actual == expected) continue;
        if (lane) ++stored;
        else if (cpu) ++lost;
        else ++changed;
        if (first.empty()) first = "dword " + std::to_string(word) + " holds " + Hex(actual) + ", expected " + Hex(expected) + (lane ? " from the store" : cpu ? " from the CPU" : " kept from before");
    }
    Expect(stored == 0u && lost == 0u && changed == 0u, what + ": " + std::to_string(stored) + " stored dwords are wrong, " + std::to_string(lost) + " of " + std::to_string(Lanes) + " dwords the CPU wrote during the dispatch were overwritten and " + std::to_string(changed) + " other dwords changed; first " + first);
}

}

#endif

int main() {
#ifdef _WIN32
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        CpuWritesDuringStagedDispatch(*device);
    } catch (const std::exception& error) {
        Expect(false, std::string("exception: ") + error.what());
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d staged store hazard checks failed\n", failures);
        return 1;
    }
    std::printf("staged store hazard tests passed\n");
    return 0;
#else
    std::printf("skipped, the guest arena is Windows-only here\n");
    return VulkanTestSkipped;
#endif
}
