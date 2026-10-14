#include "NewPathFixture.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <exception>
#include <tuple>

#ifdef _WIN32

namespace {

using namespace NewPath;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Lanes = 32;
constexpr std::uint32_t LaneParams = 64;
constexpr std::uint32_t ReturnsAt = 0x800u;
constexpr std::uint32_t DescriptorsAt = 0xf00u;
constexpr std::uint32_t ParamsBytes = 0x1000u;
constexpr std::uint32_t TargetBytes = 2048;
constexpr std::uint32_t FormatRecords = 0x300u;
constexpr std::uint32_t FormatRegion = 0x400u;
constexpr std::uint32_t FormatLaneParams = 32;

constexpr std::uint32_t SaveExec = 0xbea8037eu;
constexpr std::uint32_t RestoreExec = 0xbefe0328u;
constexpr std::uint32_t ExecFromVcc = 0x877e6a28u;
constexpr std::uint32_t VcmpNeZero = 0x7d8a3c80u;
constexpr std::uint32_t VAndB32 = 0x1bu;

int failures = 0;

void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

std::uint32_t Hash(std::uint32_t value) {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

ShaderRecompiler::RecompileResult Compile(AgcDriver::VulkanDevice& device, const Program& program, std::span<const std::uint32_t> userData) {
    const auto code = program.Code();
    const std::array<ShaderRecompiler::MemoryRegion, 1> regions{{{static_cast<std::uintptr_t>(program.Address()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{Lanes, 1, 1}, 0, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, program.Address(), code, 0, {}},
        {32, 0, userData, compute, std::nullopt, std::nullopt, regions},
        device.Target(),
        {0, 0, 0, 128}
    };
    request.useCache = false;
    return ShaderRecompiler::Recompile(request);
}

void Run(AgcDriver::VulkanDevice& device, const Program& program, const ShaderRecompiler::RecompileResult& result) {
    device.Dispatch(result, 1, 1, 1, static_cast<std::uintptr_t>(program.Address()));
    device.WaitIdle();
}

void Dispatch(AgcDriver::VulkanDevice& device, const Program& program, std::span<const std::uint32_t> userData) {
    Run(device, program, Compile(device, program, userData));
}

void LoadDescriptor(Program& program, std::uint32_t sgpr, std::uint32_t at) {
    using namespace Isa;
    program << Vop1(VMovB32, 15, InlineInteger(0)) << Mubuf(0x0eu, 16, 15, 0, false, at) << WaitAll;
    for (std::uint32_t word = 0; word < 4; ++word) program << Vop1(VReadFirstLaneB32, sgpr + word, Vgpr(16 + word));
}

void PutDescriptor(std::uint8_t* params, std::uint32_t at, const std::array<std::uint32_t, 4>& descriptor) {
    std::memcpy(params + at, descriptor.data(), sizeof(descriptor));
}

std::array<std::uint32_t, 4> RawResource(std::uint64_t address, std::uint32_t bytes) {
    return BufferResource(address, bytes);
}

struct AtomicOp {
    const char* name;
    std::uint32_t opcode;
    bool wide;
    std::uint64_t (*apply)(std::uint64_t old, std::uint64_t source, std::uint64_t compare);
};

std::uint64_t Swap(std::uint64_t, std::uint64_t source, std::uint64_t) { return source; }
std::uint64_t CompareSwap(std::uint64_t old, std::uint64_t source, std::uint64_t compare) { return old == compare ? source : old; }
std::uint64_t Add(std::uint64_t old, std::uint64_t source, std::uint64_t) { return old + source; }
std::uint64_t UnsignedMax(std::uint64_t old, std::uint64_t source, std::uint64_t) { return std::max(old, source); }
std::uint64_t Xor(std::uint64_t old, std::uint64_t source, std::uint64_t) { return old ^ source; }
std::uint64_t Increment(std::uint64_t old, std::uint64_t source, std::uint64_t) {
    const auto value = static_cast<std::uint32_t>(old);
    return value >= static_cast<std::uint32_t>(source) ? 0u : value + 1u;
}
std::uint64_t Decrement(std::uint64_t old, std::uint64_t source, std::uint64_t) {
    const auto value = static_cast<std::uint32_t>(old);
    return value == 0u || value > static_cast<std::uint32_t>(source) ? static_cast<std::uint32_t>(source) : value - 1u;
}

constexpr std::array<std::pair<AtomicOp, AtomicOp>, 5> AtomicPairs{{
    {{"cmpswap", 0x31u, false, &CompareSwap}, {"cmpswap", 0x31u, false, &CompareSwap}},
    {{"swap", 0x30u, false, &Swap}, {"xor", 0x3bu, false, &Xor}},
    {{"inc", 0x3cu, false, &Increment}, {"dec", 0x3du, false, &Decrement}},
    {{"add_x2", 0x52u, true, &Add}, {"umax_x2", 0x58u, true, &UnsignedMax}},
    {{"cmpswap_x2", 0x51u, true, &CompareSwap}, {"swap_x2", 0x50u, true, &Swap}},
}};

std::uint32_t AtomicActivity(std::uint32_t lane) {
    if (lane >= 20u) return 3u;
    return std::array<std::uint32_t, 4>{3u, 1u, 2u, 0u}[lane % 4u];
}

std::uint32_t AtomicRecords(bool wide) {
    return wide ? 1020u : 1024u;
}

std::uint32_t AtomicOffset(bool wide, std::uint32_t access, std::uint32_t lane) {
    const std::uint32_t slot = wide ? 8u : 4u;
    if (lane < 20u) return access * 512u + lane * slot;
    if (access == 1u && lane == 20u) return wide ? 992u : 1016u;
    const std::array<std::uint32_t, 12> tail{wide ? 1008u : 1020u, wide ? 1016u : 1024u, 1032u, wide ? 1000u : 1012u, 0xfff00000u, 0x7ffffff8u, 0x80000000u, 0xffff0000u, 0x10000u, 1040u, 0xffffff00u, 0x40000000u};
    return tail[lane - 20u];
}

bool AtomicValid(bool wide, std::uint32_t offset) {
    const std::uint32_t bytes = wide ? 8u : 4u;
    return offset <= AtomicRecords(wide) - bytes;
}

void BuildAtomicProgram(Program& program, const AtomicOp& first, const AtomicOp& second) {
    using namespace Isa;
    program << Vop2(VLshlRevB32, 1, InlineInteger(6), 0)
            << Mubuf(0x0eu, 2, 1, 0)
            << Mubuf(0x0eu, 6, 1, 0, false, 16)
            << Mubuf(0x0eu, 10, 1, 0, false, 32)
            << WaitAll;
    LoadDescriptor(program, 16, DescriptorsAt);
    const std::array<std::tuple<const AtomicOp*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t>, 2> accesses{{{&first, 1u, 20u, 6u, 2u}, {&second, 2u, 24u, 10u, 4u}}};
    for (const auto& [op, bit, data, operands, offset] : accesses) {
        for (std::uint32_t dword = 0; dword < 4; ++dword) program << Vop1(VMovB32, data + dword, Vgpr(operands + dword));
        program << SaveExec << Vop2(VAndB32, 30, InlineInteger(bit), 3) << VcmpNeZero << ExecFromVcc;
        program << std::array<std::uint32_t, 2>{0xe0005000u | (op->opcode << 18u), (ZeroOffset << 24u) | (4u << 16u) | (data << 8u) | offset} << RestoreExec;
    }
    program << WaitAll << Vop2(VLshlRevB32, 31, InlineInteger(4), 0)
            << Mubuf(0x1du, 20, 31, 0, false, ReturnsAt)
            << Mubuf(0x1du, 24, 31, 0, false, ReturnsAt + 8u)
            << EndProgram;
}

std::uint64_t ReadValue(const std::uint8_t* memory, std::uint32_t offset, bool wide) {
    std::uint64_t value = 0;
    std::memcpy(&value, memory + offset, wide ? 8u : 4u);
    return value;
}

void WriteValue(std::uint8_t* memory, std::uint32_t offset, bool wide, std::uint64_t value) {
    std::memcpy(memory + offset, &value, wide ? 8u : 4u);
}

void RunAtomics(AgcDriver::VulkanDevice& device, Program& program, const AtomicOp& first, const AtomicOp& second, std::uint32_t seed) {
    PrivatePages params(1);
    PrivatePages target(1);
    auto* p = params.Data();
    auto* t = target.Data();
    for (std::uint32_t byte = 0; byte < TargetBytes; ++byte) t[byte] = static_cast<std::uint8_t>(Hash(byte + seed) >> 5u);
    std::vector<std::uint8_t> expected(t, t + TargetBytes);
    std::array<std::array<std::uint32_t, 4>, Lanes> returns{};
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
        std::array<std::uint32_t, 16> words{};
        words[0] = AtomicOffset(first.wide, 0, lane);
        words[1] = AtomicActivity(lane);
        words[2] = AtomicOffset(second.wide, 1, lane);
        for (std::uint32_t index = 4; index < 12; ++index) words[index] = Hash(lane * 16u + index + seed * 31u);
        if (lane < 20u && lane % 3u == 0u) {
            for (const auto [op, offset, operands] : {std::tuple{&first, words[0], 4u}, std::tuple{&second, words[2], 8u}}) {
                if (op->apply != &CompareSwap || !AtomicValid(op->wide, offset)) continue;
                const auto current = ReadValue(t, offset, op->wide);
                if (op->wide) {
                    words[operands + 2u] = static_cast<std::uint32_t>(current);
                    words[operands + 3u] = static_cast<std::uint32_t>(current >> 32u);
                } else {
                    words[operands + 1u] = static_cast<std::uint32_t>(current);
                }
            }
        }
        std::memcpy(p + lane * LaneParams, words.data(), sizeof(words));
        returns[lane] = {words[4], words[5], words[8], words[9]};
    }
    for (std::uint32_t access = 0; access < 2; ++access) {
        const auto& op = access == 0 ? first : second;
        for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
            const auto* words = reinterpret_cast<const std::uint32_t*>(p + lane * LaneParams);
            if ((AtomicActivity(lane) & (1u << access)) == 0u) continue;
            const auto offset = words[access == 0 ? 0 : 2];
            const auto operands = access == 0 ? 4u : 8u;
            const std::uint64_t source = op.wide ? (std::uint64_t{words[operands + 1u]} << 32u) | words[operands] : words[operands];
            const std::uint64_t compare = op.wide ? (std::uint64_t{words[operands + 3u]} << 32u) | words[operands + 2u] : words[operands + 1u];
            auto& returned = returns[lane];
            const auto slot = access * 2u;
            if (!AtomicValid(op.wide, offset)) {
                returned[slot] = 0u;
                if (op.wide) returned[slot + 1u] = 0u;
                continue;
            }
            const auto old = ReadValue(expected.data(), offset, op.wide);
            WriteValue(expected.data(), offset, op.wide, op.apply(old, source, compare) & (op.wide ? ~std::uint64_t{0} : 0xffffffffu));
            returned[slot] = static_cast<std::uint32_t>(old);
            if (op.wide) returned[slot + 1u] = static_cast<std::uint32_t>(old >> 32u);
        }
    }
    PutDescriptor(p, DescriptorsAt, RawResource(target.Address(), AtomicRecords(first.wide)));
    const auto user = BufferResource(params.Address(), ParamsBytes);
    Dispatch(device, program, user);
    const std::string name = std::string("helper atomics ") + first.name + " and " + second.name + " through a run-time V#";
    for (std::uint32_t byte = 0; byte < TargetBytes; ++byte) {
        if (t[byte] == expected[byte]) continue;
        Expect(false, name + ": byte " + std::to_string(byte) + " is " + Hex(t[byte]) + ", expected " + Hex(expected[byte]));
        break;
    }
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
        std::array<std::uint32_t, 4> actual{};
        std::memcpy(actual.data(), p + ReturnsAt + lane * 16u, sizeof(actual));
        for (std::uint32_t slot = 0; slot < 4; ++slot) {
            if (actual[slot] == returns[lane][slot]) continue;
            Expect(false, name + ": lane " + std::to_string(lane) + " return dword " + std::to_string(slot) + " is " + Hex(actual[slot]) + ", expected " + Hex(returns[lane][slot]));
            slot = 4;
            lane = Lanes;
        }
    }
}

std::uint32_t Word3(std::uint32_t format) {
    return (RawBuffer & ~0x7f000u) | (format << 12u);
}

std::uint32_t FormatActivity(std::uint32_t lane) {
    return std::array<std::uint32_t, 4>{1u, 1u, 0u, 1u}[lane % 4u];
}

std::uint32_t FormatOffset(std::uint32_t lane) {
    constexpr std::array<std::uint32_t, 8> tail{FormatRecords - 48u, FormatRecords - 32u, FormatRecords - 14u, FormatRecords - 2u, FormatRecords, FormatRecords + 4u, 0xfffffff0u, 0x80000000u};
    return lane < 24u ? 4u + lane * 24u + (lane % 3u) : tail[lane - 24u];
}

std::uint32_t Special(std::uint32_t index) {
    constexpr std::array<std::uint32_t, 12> values{0x7fc00000u, 0x7f800001u, 0xff800000u, 0x00000001u, 0x80000000u, 0x3f800000u, 0xbf800000u, 0x477fe000u, 0x00800000u, 0xc7000000u, 0x4b800001u, 0x3effffffu};
    return values[index % values.size()];
}

std::uint8_t Initial(std::uint32_t offset) {
    return static_cast<std::uint8_t>(Hash(offset * 131u + 7u) >> 7u);
}

constexpr std::array<std::uint32_t, 20> StoreFormats{5u, 11u, 18u, 20u, 27u, 34u, 41u, 48u, 54u, 60u, 62u, 69u, 72u, 75u, 1u, 13u, 22u, 36u, 56u, 71u};
constexpr std::uint32_t FormatSelectorAt = 0xff0u;
constexpr std::uint32_t CompareSelector = 0xbf06ff32u;
constexpr std::uint32_t SkipTypedStore = 0xbf840002u;
constexpr std::uint32_t WaitScalar = 0xbf8cc07fu;

void BuildFormatProgram(Program& program) {
    using namespace Isa;
    program << Vop2(VLshlRevB32, 1, InlineInteger(5), 0)
            << Mubuf(0x0eu, 8, 1, 0)
            << Mubuf(0x0eu, 4, 1, 0, false, 16)
            << WaitAll;
    LoadDescriptor(program, 32, DescriptorsAt);
    LoadDescriptor(program, 44, DescriptorsAt + 32u);
    program << std::array<std::uint32_t, 2>{0xf4200c80u, 0xfa000000u | FormatSelectorAt} << WaitScalar;
    program << SaveExec << Vop2(VAndB32, 30, InlineInteger(1), 9) << VcmpNeZero << ExecFromVcc
            << std::array<std::uint32_t, 2>{0xe01c1000u, 0x80000408u | ((32u / 4u) << 16u)};
    for (const auto format : StoreFormats) {
        program << CompareSelector << format << SkipTypedStore
                << std::array<std::uint32_t, 2>{0xe8071000u | (format << 19u), 0x80000408u | ((44u / 4u) << 16u)};
    }
    program << WaitAll << RestoreExec << EndProgram;
}

struct FormatPages {
    PrivatePages params{1};
    PrivatePages regions{1};
};

void RunFormat(AgcDriver::VulkanDevice& device, const Program& program, const ShaderRecompiler::RecompileResult& result, FormatPages& pages, std::uint32_t format) {
    auto& params = pages.params;
    auto& regions = pages.regions;
    auto* p = params.Data();
    auto* untyped = regions.Data();
    auto* typed = regions.Data() + FormatRegion;
    for (std::uint32_t offset = 0; offset < FormatRegion; ++offset) untyped[offset] = typed[offset] = Initial(offset);
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
        std::array<std::uint32_t, 8> words{};
        words[0] = FormatOffset(lane);
        words[1] = FormatActivity(lane);
        for (std::uint32_t component = 0; component < 4u; ++component) {
            const auto mixed = Hash(lane * 4u + component + format * 977u);
            words[4u + component] = (mixed & 7u) == 0u ? Special(mixed >> 3u) : (mixed & 7u) == 1u ? (mixed >> 24u) : (mixed & 7u) == 2u ? (mixed >> 9u) | 0x3f000000u : mixed;
        }
        std::memcpy(p + lane * FormatLaneParams, words.data(), sizeof(words));
    }
    auto resource = [](std::uint64_t address, std::uint32_t word3) {
        auto descriptor = BufferResource(address, FormatRecords);
        descriptor[3] = word3;
        return descriptor;
    };
    PutDescriptor(p, DescriptorsAt, resource(regions.Address(), Word3(format)));
    PutDescriptor(p, DescriptorsAt + 32u, resource(regions.Address(FormatRegion), RawBuffer));
    std::memcpy(p + FormatSelectorAt, &format, 4u);
    Run(device, program, result);
    std::uint32_t written = 0;
    for (std::uint32_t offset = 0; offset < FormatRegion; ++offset) {
        if (untyped[offset] != typed[offset]) {
            Expect(false, "helper formatted stores through a run-time V#: format " + std::to_string(format) + " byte " + std::to_string(offset) + " is " + Hex(untyped[offset]) + " through the run-time format, " + Hex(typed[offset]) + " through the typed format");
            return;
        }
        if (untyped[offset] != Initial(offset)) ++written;
    }
    Expect(written != 0u, "helper formatted stores through a run-time V#: format " + std::to_string(format) + " stored nothing");
}

}

#endif

int main() {
#ifdef _WIN32
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        static Program atomicPrograms[AtomicPairs.size()];
        for (std::size_t index = 0; index < AtomicPairs.size(); ++index) {
            BuildAtomicProgram(atomicPrograms[index], AtomicPairs[index].first, AtomicPairs[index].second);
            RunAtomics(*device, atomicPrograms[index], AtomicPairs[index].first, AtomicPairs[index].second, static_cast<std::uint32_t>(index) * 7919u);
        }
        static Program formatProgram;
        BuildFormatProgram(formatProgram);
        FormatPages pages;
        const auto user = BufferResource(pages.params.Address(), ParamsBytes);
        const auto compiled = Compile(*device, formatProgram, user);
        for (const auto format : StoreFormats) RunFormat(*device, formatProgram, compiled, pages, format);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: exception: %s\n", error.what());
        return 1;
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d new path helper checks failed\n", failures);
        return 1;
    }
    std::printf("new path helper tests passed\n");
    return 0;
#else
    std::printf("skipped, the guest arena is Windows-only here\n");
    return VulkanTestSkipped;
#endif
}
