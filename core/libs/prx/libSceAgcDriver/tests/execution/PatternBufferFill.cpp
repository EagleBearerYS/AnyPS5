#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Memory/BufferFill.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "VulkanTestDevice.hpp"
#include <chrono>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using namespace AgcDriver::DriverDetail;
alignas(256) constexpr auto Code = PatternFillKernel;
alignas(256) constexpr auto MaskFirstCode = PatternFillKernelMaskFirst;
alignas(256) std::array<std::uint32_t, 4> Pattern{0, 0x12345678u, 0xdeadbeefu, 0x7fc12345u};
alignas(256) std::array<std::uint32_t, 4> Control{};
alignas(256) std::array<std::uint32_t, 260> Output{};

std::array<std::uint32_t, 12> UserData(void* output, std::uint32_t records, std::uint32_t elementWord3 = 0x14004u) {
    std::array<std::uint32_t, 12> data{};
    const auto descriptor = [&](std::size_t first, const void* pointer, std::uint32_t stride, std::uint32_t count, std::uint32_t format) {
        const auto address = reinterpret_cast<std::uintptr_t>(pointer);
        data[first] = static_cast<std::uint32_t>(address);
        data[first + 1] = static_cast<std::uint32_t>(address >> 32u) | (stride << 16u);
        data[first + 2] = count;
        data[first + 3] = format;
    };
    descriptor(0, Pattern.data(), 4, Pattern.size(), elementWord3);
    descriptor(4, output, 4, records, elementWord3);
    descriptor(8, Control.data(), 16, 1, 0x4dfacu);
    return data;
}

void RangeTests() {
    const ShaderRecompiler::ShaderComputeStageInfo compute{{64, 1, 1}, 0, {true, false, false}, false, 1};
    const auto data = UserData(Output.data(), 256);
    Require(MatchesPatternFillKernel(Code, data, compute), "pattern fill kernel was not recognized");
    for (std::size_t word = 0; word < Code.size(); ++word) {
        auto altered = Code;
        altered[word] ^= 1;
        Require(!MatchesPatternFillKernel(altered, data, compute), "a different shader matched the fill kernel");
    }
    Require(!MatchesPatternFillKernel(std::span(Code).first(17), data, compute), "truncated fill shader matched");
    for (const auto word : {1u, 3u, 5u, 7u, 9u, 11u}) {
        auto altered = data;
        altered[word] ^= word % 4 == 1 ? 0x80000000u : 0x1000u;
        Require(!MatchesPatternFillKernel(Code, altered, compute), "unsupported fill descriptor matched");
    }
    auto disabled = compute;
    disabled.groupIdEnable[0] = false;
    Require(!MatchesPatternFillKernel(Code, data, disabled), "fill with no group id matched");
    disabled = compute;
    disabled.numThreads[0] = 32;
    Require(!MatchesPatternFillKernel(Code, data, disabled), "different fill group size matched");
    std::array<std::uint32_t, 5> packet{0xc0031500u, 2, 1, 1, 0x41};
    auto range = DecodePatternFillRange(data, packet, PatternFillControl::CountThenMask);
    Require(range && range->PatternBytes(256, 0) == 512, "fill exceeded launched threads");
    Require(range->PatternBytes(80, 0) == 320, "fill exceeded its control count");
    for (const auto mask : {1u, 2u, 3u}) Require(range->PatternBytes(80, mask) == 320, "a fill whose pattern repeats every 16 bytes was refused");
    Require(!range->PatternBytes(80, 4), "a pattern longer than 16 bytes matched");
    Require(!range->PatternBytes(80, 7), "a pattern longer than 16 bytes matched");
    Require(!range->PatternBytes(79, 0), "partial 16-byte fill matched");
    Require(range->PatternBytes(0, 0) == 0, "empty control count was not empty");
    packet[1] = 68;
    packet[4] |= 0x20;
    range = DecodePatternFillRange(data, packet, PatternFillControl::CountThenMask);
    Require(range && range->PatternBytes(256, 0) == 272, "partial workgroup was rounded up");
    range->records = 64;
    Require(range->PatternBytes(256, 0) == 256, "fill exceeded output descriptor");
    range->source = range->destination + 16;
    Require(!range->PatternBytes(256, 0), "aliased pattern was accepted");
    range->source = range->destination - 8;
    Require(range->PatternBytes(256, 1) == 256 && !range->PatternBytes(256, 3), "the pattern words a mask reads were not checked against the destination");
    range->source = 0x1000;
    range->control = range->destination - 4;
    Require(!range->PatternBytes(256, 0), "aliased control was accepted");
    const std::array<std::uint32_t, 4> words{0x11u, 0x22u, 0x33u, 0x44u};
    Require(PatternFillRange::Pattern(words, 0) == std::array<std::uint32_t, 4>{0x11u, 0x11u, 0x11u, 0x11u}, "mask 0 did not repeat the first pattern word");
    Require(PatternFillRange::Pattern(words, 1) == std::array<std::uint32_t, 4>{0x11u, 0x22u, 0x11u, 0x22u}, "mask 1 did not repeat two pattern words");
    Require(PatternFillRange::Pattern(words, 2) == std::array<std::uint32_t, 4>{0x11u, 0x11u, 0x33u, 0x33u}, "mask 2 did not select pattern words 0 and 2");
    Require(PatternFillRange::Pattern(words, 3) == std::array<std::uint32_t, 4>{0x11u, 0x22u, 0x33u, 0x44u}, "mask 3 did not take four pattern words");
    Require(PatternFillRange::Pattern(std::span(words).first(2), 3) == std::array<std::uint32_t, 4>{0x11u, 0x22u, 0u, 0u}, "pattern words past the source's records were not zero");
    range->sourceRecords = 2;
    Require(range->SourceWords(3) == 2 && range->SourceWords(0) == 1, "the pattern words read were not bounded by the source's records");
    packet[1] = 0;
    Require(DecodePatternFillRange(data, packet, PatternFillControl::CountThenMask)->invocations == 0, "empty dispatch gained invocations");
    packet[1] = 0x4000001u;
    packet[4] = 0x41;
    Require(!DecodePatternFillRange(data, packet, PatternFillControl::CountThenMask), "wrapped thread indices were accepted");
    packet[1] = 1;
    packet[2] = 2;
    Require(!DecodePatternFillRange(data, packet, PatternFillControl::CountThenMask), "multidimensional fill was accepted");
}

void MaskFirstRangeTests() {
    const ShaderRecompiler::ShaderComputeStageInfo compute{{64, 1, 1}, 0, {true, false, false}, false, 1};
    const auto data = UserData(Output.data(), 256);
    Require(MatchPatternFillKernel(Code, data, compute) == PatternFillControl::CountThenMask, "the pattern fill kernel that reads its count first was not recognized as such");
    Require(MatchPatternFillKernel(MaskFirstCode, data, compute) == PatternFillControl::MaskThenCount, "the pattern fill kernel that reads its mask first was not recognized");
    for (std::size_t word = 0; word < MaskFirstCode.size(); ++word) {
        auto altered = MaskFirstCode;
        altered[word] ^= 1;
        Require(!MatchesPatternFillKernel(altered, data, compute), "a different shader matched the mask-first fill kernel");
    }
    const std::array<std::uint32_t, 5> packet{0xc0031500u, 4, 1, 1, 0x41};
    const auto range = DecodePatternFillRange(data, packet, PatternFillControl::MaskThenCount);
    Require(range && range->ControlledBytes(std::array<std::uint32_t, 2>{0, 80}) == 320, "the mask-first control block did not give its count");
    Require(range->ControlledBytes(std::array<std::uint32_t, 2>{3, 80}) == 320, "a mask-first fill of a 16-byte pattern was refused");
    Require(!range->ControlledBytes(std::array<std::uint32_t, 2>{4, 80}), "a mask-first fill of a pattern longer than 16 bytes matched");
    Require(!range->ControlledBytes(std::array<std::uint32_t, 2>{80, 0}), "the mask-first control block was read count first");
}

std::vector<std::uint32_t> Commands(std::span<const std::uint32_t> data, std::uint32_t threads, std::uint32_t wave, bool partial, bool indirect, const std::uint32_t* kernel = Code.data()) {
    std::vector<std::uint32_t> words;
    const auto registers = [&](std::uint32_t first, std::span<const std::uint32_t> values) {
        words.push_back(0xc0007600u | (static_cast<std::uint32_t>(values.size()) << 16u));
        words.push_back(first);
        words.insert(words.end(), values.begin(), values.end());
    };
    const std::array<std::uint32_t, 3> shape{64, 1, 1};
    registers(0x207, shape);
    const auto address = reinterpret_cast<std::uintptr_t>(kernel);
    const std::array<std::uint32_t, 2> program{static_cast<std::uint32_t>(address >> 8u), static_cast<std::uint32_t>(address >> 40u)};
    registers(0x20c, program);
    const std::array<std::uint32_t, 1> resource{(12u << 1u) | (1u << 7u)};
    registers(0x213, resource);
    registers(0x240, data);
    const auto initiator = 0x41u | (wave == 32 ? 0x8000u : 0u) | (partial ? 0x20u : 0u);
    if (indirect) {
        static std::array<std::uint32_t, 3> arguments;
        arguments = {threads / 64, 1, 1};
        const auto pointer = reinterpret_cast<std::uintptr_t>(arguments.data());
        words.insert(words.end(), {0xc0021600u, static_cast<std::uint32_t>(pointer), static_cast<std::uint32_t>(pointer >> 32u), initiator});
    } else {
        words.insert(words.end(), {0xc0031500u, partial ? threads : threads / 64, 1, 1, initiator});
    }
    return words;
}

void Submit(const std::vector<std::uint32_t>& words, void* output = Output.data(), std::size_t bytes = sizeof(Output)) {
    Packet packet{const_cast<std::uint32_t*>(words.data()), static_cast<std::uint32_t>(words.size()), 0, {}};
    AgcDriver::Submit(&packet, 0);
    AgcDriverWaitIdle_nid_postfix();
    AgcDriver::GuestMemory::FlushGpuWrites(reinterpret_cast<std::uintptr_t>(output), bytes);
}

void SelectTests() {
    const ShaderRecompiler::ShaderComputeStageInfo compute{{64, 1, 1}, 0, {true, false, false}, false, 1};
    for (const auto word3 : {0x14204u, 0x141c4u, 0x14fc4u}) Require(MatchesPatternFillKernel(Code, UserData(Output.data(), 256, word3), compute), "a fill descriptor that differs only in its Y, Z or W select was not recognized");
    for (const auto word3 : {0x14005u, 0x14000u, 0x15204u}) Require(!MatchesPatternFillKernel(Code, UserData(Output.data(), 256, word3), compute), "a fill descriptor with another X select or format matched");
    for (const auto wave : {32u, 64u}) {
        Output.fill(0xcafebabeu);
        Pattern[0] = 0x2468ace0u;
        Control = {200, 0, 0, 0};
        Submit(Commands(UserData(Output.data(), 256, 0x14204u), 256, wave, false, false));
        for (std::size_t i = 0; i < Output.size(); ++i) {
            const auto expected = i < 200 ? 0x2468ace0u : 0xcafebabeu;
            Require(Output[i] == expected, "fill through a descriptor with W select 1 mismatched at " + std::to_string(i) + " wave " + std::to_string(wave));
        }
    }
}

void ShortSourceTests() {
    for (const auto wave : {32u, 64u}) {
        Output.fill(0xcafebabeu);
        Pattern = {0x01020304u, 0x05060708u, 0x090a0b0cu, 0x0d0e0f10u};
        Control = {160, 3, 0, 0};
        auto data = UserData(Output.data(), 256);
        data[2] = 2;
        Submit(Commands(data, 256, wave, false, false));
        for (std::size_t i = 0; i < Output.size(); ++i) {
            const auto expected = i < 160 ? ((i & 3u) < 2 ? Pattern[i & 3u] : 0u) : 0xcafebabeu;
            Require(Output[i] == expected, "fill from a pattern source of two records mismatched at " + std::to_string(i) + " wave " + std::to_string(wave));
        }
    }
    Pattern = {0, 0x12345678u, 0xdeadbeefu, 0x7fc12345u};
}

void ExecuteTests() {
    struct Case { std::uint32_t pattern, count, records, threads, mask; bool partial, indirect; };
    constexpr std::array<Case, 13> cases{{
        {0, 256, 256, 256, 0, false, false},
        {0xffffffffu, 256, 256, 128, 0, false, false},
        {0x7fc12345u, 80, 256, 256, 0, false, false},
        {0x80000000u, 256, 64, 256, 0, false, false},
        {0xdeadbeefu, 256, 256, 68, 0, true, false},
        {0x3f800000u, 0, 256, 64, 0, false, false},
        {0x80808080u, 256, 256, 0, 0, false, false},
        {0xffffffffu, 1, 256, 64, 0, false, false},
        {0, 256, 7, 64, 0, false, false},
        {0x01010101u, 128, 256, 128, 0, false, true},
        {0, 128, 256, 128, 3, false, false},
        {0, 200, 256, 256, 1, false, false},
        {0, 96, 256, 128, 2, false, false}
    }};
    for (const bool maskFirst : {false, true}) {
        for (const auto wave : {32u, 64u}) {
            for (const auto& item : cases) {
                Output.fill(0xcafebabeu);
                Pattern[0] = item.pattern;
                Control = maskFirst ? std::array<std::uint32_t, 4>{item.mask, item.count, 0, 0} : std::array<std::uint32_t, 4>{item.count, item.mask, 0, 0};
                const auto words = Commands(UserData(Output.data(), item.records), item.threads, wave, item.partial, item.indirect, maskFirst ? MaskFirstCode.data() : Code.data());
                Submit(words);
                const auto count = std::min({item.count, item.records, item.threads});
                for (std::size_t i = 0; i < Output.size(); ++i) {
                    const auto expected = i < count ? Pattern[i & item.mask] : 0xcafebabeu;
                    Require(Output[i] == expected, "pattern fill mismatch at " + std::to_string(i) + " wave " + std::to_string(wave) + " count " + std::to_string(item.count) + (maskFirst ? " (mask first)" : "") + " got " + std::to_string(Output[i]) + " expected " + std::to_string(expected));
                }
            }
        }
    }

    Output.fill(0xcafebabeu);
    Pattern[0] = 0x40404040u;
    const auto firstPattern = Pattern;
    Control = {256, 3, 0, 0};
    auto words = Commands(UserData(Output.data(), 256), 256, 32, false, false);
    const auto label = [&](std::uint32_t* target, std::uint32_t value) {
        const auto address = reinterpret_cast<std::uintptr_t>(target);
        words.insert(words.end(), {0xc0064900u, 0x514u, (1u << 29u) | (2u << 24u), static_cast<std::uint32_t>(address), static_cast<std::uint32_t>(address >> 32u), value, 0, 0});
    };
    label(&Control[0], 128);
    label(&Control[1], 0);
    label(&Pattern[0], 0xababababu);
    const auto next = Commands(UserData(Output.data(), 256), 256, 32, false, false);
    words.insert(words.end(), next.begin(), next.end());
    Submit(words);
    for (std::size_t i = 0; i < Output.size(); ++i) {
        const auto expected = i < 128 ? 0xababababu : i < 256 ? firstPattern[i & 3u] : 0xcafebabeu;
        Require(Output[i] == expected, "fill did not observe preceding GPU results and labels at " + std::to_string(i));
    }
}

void PartialGroupTests() {
    for (const auto wave : {32u, 64u}) {
        Output.fill(0xcafebabeu);
        Pattern[0] = 0x5a5a5a5au;
        Control = {256, 0, 0, 0};
        auto words = Commands(UserData(Output.data(), 256), 128, wave, false, false);
        words[2] = 0x00100040u;
        words[3] = 0x00010001u;
        words[4] = 0x00010001u;
        words.back() |= 0x2u;
        Submit(words);
        for (std::size_t i = 0; i < Output.size(); ++i) {
            const auto expected = i < 80 ? 0x5a5a5a5au : 0xcafebabeu;
            Require(Output[i] == expected, "partial last thread group fill mismatch at " + std::to_string(i) + " wave " + std::to_string(wave));
        }
    }
}

void Benchmark() {
    constexpr std::size_t bytes = 64u << 20u;
    std::vector<std::uint32_t> storage(bytes / 4 + 4);
    auto* output = reinterpret_cast<std::uint32_t*>((reinterpret_cast<std::uintptr_t>(storage.data()) + 15u) & ~std::uintptr_t{15});
    Pattern[0] = 0xffffffffu;
    Control = {bytes / 4, 0, 0, 0};
    const auto words = Commands(UserData(output, bytes / 4), bytes / 4, 32, false, false);
    Submit(words, output, bytes);
    std::array<double, 9> times;
    for (auto& ms : times) {
        const auto start = std::chrono::steady_clock::now();
        Submit(words, output, bytes);
        ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        Require(std::all_of(output, output + bytes / 4, [](auto value) { return value == 0xffffffffu; }), "benchmark fill was incomplete");
    }
    std::sort(times.begin(), times.end());
    std::printf("64 MiB fill median %.3f ms, min %.3f, max %.3f\n", times[4], times.front(), times.back());
}

}

int main(int argc, char** argv) {
    try {
        RangeTests();
        MaskFirstRangeTests();
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--benchmark") Benchmark();
        else {
            SelectTests();
            ShortSourceTests();
            ExecuteTests();
            PartialGroupTests();
        }
        AgcDriverShutdown_nid_postfix();
        std::puts("pattern buffer fill tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        AgcDriverShutdown_nid_postfix();
        return 1;
    }
}
