#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_TESTS_EXECUTION_DEVICETABLEFIXTURE_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_TESTS_EXECUTION_DEVICETABLEFIXTURE_HPP

#include "NewPathFixture.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/ShaderMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/DeviceImageTable.hpp"
#include "Optimization/ResourceProgram.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include "GuestTestMemory.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace DeviceTable {

using NewPath::Program;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Width = 64;
constexpr std::uint32_t Height = 4;
constexpr std::uint32_t Lanes = 64;
constexpr std::uint32_t LaneWords = 4;
constexpr std::uint32_t OutputWords = Height * Lanes * LaneWords;
constexpr std::uint32_t Format32UInt = 20;
constexpr std::uint32_t Format32Float = 22;
constexpr std::uint32_t Format8_8_8_8UNorm = 56;
constexpr std::uint32_t Type2D = 9;
constexpr std::uint32_t Untouched = 0xdeadbeefu;
constexpr std::uint32_t OutputSlot = 8;
constexpr std::uint32_t SamplerSlot = 12;
constexpr std::uint32_t ImageSlot = 16;
constexpr std::uint32_t SecondImageSlot = 20;
constexpr std::uint32_t SrtWords = 32;
constexpr std::array<std::uint32_t, 4> PointClamp{0x92u, (4u * 256u) << 12u, 0u, 0u};

namespace Asm {

using namespace NewPath::Isa;

constexpr std::uint32_t VCvtF32U32 = 0x06u;
constexpr std::uint32_t VAddF32 = 0x03u;
constexpr std::uint32_t VMulF32 = 0x08u;
constexpr std::uint32_t VAndB32 = 0x1bu;
constexpr std::uint32_t Half = 0xf0u;
constexpr std::uint32_t OneOverWidth = 0x3c800000u;
constexpr std::uint32_t OneOverHeight = 0x3e800000u;
constexpr std::uint32_t ImageLoad = 0x00u;
constexpr std::uint32_t ImageStore = 0x08u;
constexpr std::uint32_t ImageGetResinfo = 0x0eu;
constexpr std::uint32_t ImageSampleLz = 0x27u;
constexpr std::uint32_t BufferStoreDwordX4 = 0x1eu;

constexpr std::array<std::uint32_t, 2> Mimg(std::uint32_t opcode, std::uint32_t dmask, bool r128, std::uint32_t vdata, std::uint32_t vaddr, std::uint32_t resource, std::uint32_t sampler = 0, bool unorm = false) {
    return {0xf0000000u | (opcode << 18u) | (r128 ? 0x8000u : 0u) | (unorm ? 0x1000u : 0u) | (dmask << 8u) | (1u << 3u), ((sampler / 4u) << 21u) | ((resource / 4u) << 16u) | (vdata << 8u) | vaddr};
}

inline void LoadSrt(Program& program, std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> loads) {
    for (const auto& [sgpr, slot] : loads) program << Smem(SLoadDwordX4, sgpr, 0, slot * 4u);
    program << WaitAll;
}

inline void TexelCenter(Program& program, std::uint32_t groupSgpr) {
    program << Vop2(VAndB32, 1, InlineInteger(63), 0)
            << Vop1(VCvtF32U32, 1, Vgpr(1))
            << Vop2(VAddF32, 1, Half, 1)
            << Vop2(VMulF32, 1, Literal, 1) << OneOverWidth
            << Vop1(VCvtF32U32, 2, groupSgpr)
            << Vop2(VAddF32, 2, Half, 2)
            << Vop2(VMulF32, 2, Literal, 2) << OneOverHeight;
}

inline void TexelIndex(Program& program, std::uint32_t groupSgpr) {
    program << Vop2(VAndB32, 1, InlineInteger(63), 0)
            << Vop1(VMovB32, 2, groupSgpr);
}

inline void StoreLane(Program& program, std::uint32_t groupSgpr, std::uint32_t output) {
    program << Vop1(VMovB32, 9, groupSgpr)
            << Vop2(VLshlRevB32, 8, InlineInteger(6), 9)
            << Vop2(VAddNcU32, 8, Vgpr(0), 8)
            << Vop2(VLshlRevB32, 8, InlineInteger(4), 8)
            << WaitAll
            << Mubuf(BufferStoreDwordX4, 4, 8, output)
            << EndProgram;
}

}

inline int failures = 0;

inline void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

inline int Finish(const char* name) {
    if (failures != 0) {
        std::fprintf(stderr, "%d %s checks failed\n", failures, name);
        return 1;
    }
    std::printf("%s tests passed\n", name);
    return 0;
}

inline std::string Hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08x", value);
    return text;
}

inline std::array<std::uint32_t, 4> Image(const void* texels, std::uint32_t format) {
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(texels));
    return {static_cast<std::uint32_t>(address >> 8u), static_cast<std::uint32_t>((address >> 40u) & 0xffu) | (format << 20u) | (((Width - 1u) & 3u) << 30u), ((Width - 1u) >> 2u) | ((Height - 1u) << 14u) | (1u << 31u), 0xfacu | (Type2D << 28u)};
}

inline std::array<std::uint32_t, 4> Buffer(const void* base, std::uint32_t bytes) {
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(base));
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, 0x31016facu};
}

inline TestGuest::Array<std::uint32_t, OutputWords> Output;
inline TestGuest::Array<std::uint32_t, SrtWords> Srt;

inline void Place(std::uint32_t slot, const std::array<std::uint32_t, 4>& words) {
    std::copy(words.begin(), words.end(), Srt.begin() + slot);
}

inline std::array<std::uint32_t, 2> SrtUserData() {
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(Srt.data()));
    Place(OutputSlot, Buffer(Output.data(), static_cast<std::uint32_t>(decltype(Output)::ByteSize)));
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>(address >> 32u)};
}

struct Compiled {
    std::unique_ptr<AgcDriver::ShaderMemory> memory;
    std::shared_ptr<const ShaderRecompiler::ResourceCapture> capture;
    std::vector<ShaderRecompiler::MemoryRegion> regions;
    std::shared_ptr<const ShaderRecompiler::RecompileResult> result;
    std::uint64_t address = 0;
};

inline Compiled Compile(AgcDriver::VulkanDevice& device, const Program& program, std::span<const std::uint32_t> userData) {
    Compiled compiled;
    compiled.address = program.Address();
    const auto code = program.Code();
    const ShaderRecompiler::ShaderComputeStageInfo compute{{Lanes, 1, 1}, 0u, {true, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, compiled.address, code, 0, {}},
        {64, 0, userData, compute, std::nullopt, std::nullopt, {}},
        device.Target(),
        {0, 0, 0, 128}
    };
    compiled.memory = std::make_unique<AgcDriver::ShaderMemory>(std::span<const ShaderRecompiler::MemoryRegion>{});
    compiled.capture = compiled.memory->Capture(request);
    compiled.regions = compiled.memory->Regions();
    request.context.memory = compiled.regions;
    compiled.result = ShaderRecompiler::Recompile(request, *compiled.capture);
    return compiled;
}

inline std::string CapturedReads(const Compiled& compiled) {
    std::string text;
    for (const auto& region : compiled.regions) {
        char item[64];
        std::snprintf(item, sizeof(item), "%s0x%llx+%zu", text.empty() ? "" : ", ", static_cast<unsigned long long>(region.guestAddress), region.bytes.size());
        text += item;
    }
    return text.empty() ? "none" : text;
}

inline void ExpectRouted(const Compiled& compiled, const std::string& what) {
    const auto& plan = *compiled.capture->plan;
    const auto evaluated = [&](std::uint32_t source) { return std::ranges::find(plan.materializationSources, source) != plan.materializationSources.end(); };
    std::size_t deviceImages = 0;
    std::size_t deviceSamplers = 0;
    std::size_t evaluations = 0;
    for (const auto& image : plan.info.images) {
        deviceImages += image.deviceTable ? 1u : 0u;
        evaluations += evaluated(image.source) ? 1u : 0u;
    }
    for (const auto& sampler : plan.info.samplers) {
        deviceSamplers += sampler.deviceTable ? 1u : 0u;
        evaluations += evaluated(sampler.source) ? 1u : 0u;
    }
    Expect(AgcDriver::Graphics::UsesDeviceImageTable(*compiled.result), what + ": the program does not read its images through the device image table (UsesDeviceImageTable is false)");
    Expect(!plan.info.images.empty() && deviceImages == plan.info.images.size(), what + ": " + std::to_string(deviceImages) + " of the plan's " + std::to_string(plan.info.images.size()) + " images read through the device table");
    Expect(deviceSamplers == plan.info.samplers.size(), what + ": " + std::to_string(deviceSamplers) + " of the plan's " + std::to_string(plan.info.samplers.size()) + " samplers read through the device table");
    Expect(evaluations == 0u, what + ": the capture evaluates " + std::to_string(evaluations) + " image or sampler descriptors");
}

inline void ExpectNothingCaptured(const Compiled& compiled, const std::string& what) {
    const auto& plan = *compiled.capture->plan;
    Expect(compiled.regions.empty() && plan.srtReads.empty(), what + ": the capture read guest memory (" + CapturedReads(compiled) + ") and planned " + std::to_string(plan.srtReads.size()) + " SRT reads");
}

struct Outcome {
    std::vector<std::uint32_t> words;
    std::string log;
    AgcDriver::Graphics::DeviceImageTable::Counts before;
    AgcDriver::Graphics::DeviceImageTable::Counts after;

    std::uint64_t Misses() const { return after.misses - before.misses; }
    std::uint64_t Inserted() const { return after.inserted - before.inserted; }
    std::uint64_t Refreshed() const { return after.refreshed - before.refreshed; }
};

class Batch {
public:
    explicit Batch(AgcDriver::VulkanDevice& device) : device(device) {
        std::fill(Output.begin(), Output.end(), Untouched);
        before = device.DeviceImageCounts();
        log = std::make_unique<NewPath::StderrCapture>();
    }

    void Dispatch(const Compiled& compiled, std::uint32_t groups) {
        device.Dispatch(*compiled.result, groups, 1, 1, compiled.address);
    }

    Outcome Finish() {
        device.WaitIdle();
        Outcome outcome;
        outcome.log = log->Finish();
        outcome.before = before;
        outcome.after = device.DeviceImageCounts();
        AgcDriver::GuestMemory::FlushGpuWrites(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(Output.data())), decltype(Output)::ByteSize);
        outcome.words.assign(Output.begin(), Output.end());
        return outcome;
    }

private:
    AgcDriver::VulkanDevice& device;
    AgcDriver::Graphics::DeviceImageTable::Counts before;
    std::unique_ptr<NewPath::StderrCapture> log;
};

inline Outcome Run(AgcDriver::VulkanDevice& device, const Compiled& compiled, std::uint32_t groups) {
    Batch batch(device);
    batch.Dispatch(compiled, groups);
    return batch.Finish();
}

struct Expected {
    std::uint32_t rows = Height;
    std::uint32_t component = 0;
    std::uint32_t components = 1;
    std::function<bool(std::uint32_t row, std::uint32_t x, std::uint32_t component, std::uint32_t word)> matches;
};

enum class Read { Expected, Zeros, Other };

inline Read Classify(const Outcome& outcome, const Expected& expected, std::string& mismatch) {
    bool all = true;
    bool zeros = true;
    for (std::uint32_t row = 0; row < expected.rows; ++row) {
        for (std::uint32_t x = 0; x < Lanes; ++x) {
            for (std::uint32_t component = expected.component; component < expected.component + expected.components; ++component) {
                const auto word = outcome.words[(row * Lanes + x) * LaneWords + component];
                zeros = zeros && word == 0u;
                if (!expected.matches(row, x, component, word)) {
                    if (all) mismatch = "row " + std::to_string(row) + " lane " + std::to_string(x) + " component " + std::to_string(component) + " is " + Hex(word);
                    all = false;
                }
            }
        }
    }
    return all ? Read::Expected : zeros ? Read::Zeros : Read::Other;
}

inline bool Faulted(const Outcome& outcome) {
    return outcome.log.find("image table:") != std::string::npos || outcome.log.find("BDA access failed") != std::string::npos || outcome.log.find("is rejected") != std::string::npos;
}

inline void ExpectClean(const Outcome& outcome, const std::string& what) {
    Expect(!Faulted(outcome), what + ": the dispatch reported a fault or a rejection:\n" + outcome.log);
}

inline Read ExpectFirstUse(const Outcome& first, const Expected& expected, std::uint32_t keys, const std::string& what) {
    ExpectClean(first, what);
    std::string mismatch;
    const auto read = Classify(first, expected, mismatch);
    const auto lanes = static_cast<std::uint64_t>(Lanes) * expected.rows * keys;
    if (read == Read::Expected) {
        Expect(first.Misses() == 0u, what + ": the first dispatch read the texels yet counted " + std::to_string(first.Misses()) + " misses");
    } else if (read == Read::Zeros) {
        Expect(first.Misses() >= keys && first.Misses() <= lanes, what + ": the first dispatch read zeros and counted " + std::to_string(first.Misses()) + " misses, expected " + std::to_string(keys) + " to " + std::to_string(lanes) + " for " + std::to_string(keys) + " new key(s)");
    } else {
        Expect(false, what + ": the first dispatch read neither the texels nor zeros (" + mismatch + ")");
    }
    return read;
}

inline void ExpectHit(const Outcome& outcome, const Expected& expected, const std::string& what) {
    ExpectClean(outcome, what);
    std::string mismatch;
    const auto read = Classify(outcome, expected, mismatch);
    Expect(read == Read::Expected, what + ": " + mismatch);
    Expect(outcome.Misses() == 0u, what + ": counted " + std::to_string(outcome.Misses()) + " new misses");
}

inline void ExpectResolved(const Outcome& first, Read read, const Outcome& second, std::uint32_t keys, const std::string& what) {
    if (read != Read::Zeros) return;
    const auto inserted = second.after.inserted - first.before.inserted;
    const auto distinct = second.after.distinct - first.before.distinct;
    Expect(inserted == keys && distinct == keys, what + ": the misses resolved " + std::to_string(distinct) + " distinct keys and inserted " + std::to_string(inserted) + ", expected " + std::to_string(keys));
}

inline bool SameFloat(std::uint32_t word, float expected) {
    return word == std::bit_cast<std::uint32_t>(expected);
}

inline bool SameUnorm(std::uint32_t word, std::uint32_t byte) {
    const auto value = std::bit_cast<float>(word);
    return std::isfinite(value) && std::fabs(value * 255.0f - static_cast<float>(byte)) < 0.01f;
}

inline int Main(const char* name, void (*run)(AgcDriver::VulkanDevice&)) {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        run(*device);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: exception: %s\n", error.what());
        ++failures;
    }
    return Finish(name);
}

}

#endif
