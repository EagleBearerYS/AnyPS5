#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawPipeline.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Memory/BufferFill.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/Shutdown.hpp"
#include "VulkanTestDevice.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using AgcDriver::DriverDetail::DrawPipeline;
using AgcDriver::DriverDetail::PatternFillKernel;

constexpr std::size_t BlockBytes = 65536;
constexpr std::size_t LargeBytes = 4 * BlockBytes;
constexpr std::uint32_t Lanes = 32;
constexpr std::uint32_t LaneBytes = Lanes * 4u;

alignas(256) constexpr auto FillCode = PatternFillKernel;
alignas(256) constexpr std::array<std::uint32_t, 8> IncrementCode{0x34020082u, 0xe0301000u, 0x80000201u, 0xbf8c3f70u, 0x4a060481u, 0xe0701000u, 0x80010301u, 0xbf810000u};

std::uint32_t* NewBlock(std::size_t bytes) {
#ifdef _WIN32
    auto* block = static_cast<std::uint32_t*>(VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
#else
    auto* block = static_cast<std::uint32_t*>(std::aligned_alloc(BlockBytes, bytes));
#endif
    if (block == nullptr) throw std::runtime_error("packet drain study: cannot allocate a guest block");
    std::memset(block, 0, bytes);
    GuestAllocations::Mutation().Add(block, bytes, true, true, false);
    return block;
}

std::uint32_t* Source = nullptr;
std::uint32_t* Other = nullptr;
std::uint32_t* Held = nullptr;
std::uint32_t* Marker = nullptr;
std::uint32_t* Flag = nullptr;
std::uint32_t* LargeSource = nullptr;
std::uint32_t* LargeDestination = nullptr;
std::uint32_t* Pattern = nullptr;
std::uint32_t* Control = nullptr;
std::uint32_t* FillOutput = nullptr;
std::uint32_t* Input = nullptr;
std::uint32_t* Output = nullptr;
std::uint32_t markerValue = 0;

std::uint64_t Address(const void* pointer) {
    return reinterpret_cast<std::uintptr_t>(pointer);
}

std::uint32_t Low(const void* pointer) {
    return static_cast<std::uint32_t>(Address(pointer));
}

std::uint32_t High(const void* pointer) {
    return static_cast<std::uint32_t>(Address(pointer) >> 32u);
}

template <typename Ready>
void WaitUntil(Ready ready, const std::string& what) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!ready()) {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("packet drain study: timed out waiting for " + what);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

std::array<std::uint64_t, 257> Requests() {
    std::array<std::uint64_t, 257> counts{};
    for (std::uint32_t opcode = 0; opcode < counts.size(); ++opcode) counts[opcode] = DrawPipeline::DrainRequestsByOpcode(opcode);
    return counts;
}

void Finish() {
    auto idle = std::async(std::launch::async, [] { AgcDriverWaitIdle_nid_postfix(); });
    if (idle.wait_for(std::chrono::seconds(20)) != std::future_status::ready) {
        std::fputs("packet drain study: the queue worker and the draw pipeline deadlocked\n", stderr);
        std::fflush(stderr);
        std::_Exit(1);
    }
    idle.get();
}

std::vector<std::uint32_t> DmaFill(std::uint32_t value, const void* destination, std::uint32_t bytes) {
    return {0xc0055000u, 2u << 29u, value, 0u, Low(destination), High(destination), bytes};
}

std::vector<std::uint32_t> DmaCopy(const void* source, const void* destination, std::uint32_t bytes) {
    return {0xc0055000u, 0u, Low(source), High(source), Low(destination), High(destination), bytes};
}

std::vector<std::uint32_t> DmaPrefetch(const void* source, std::uint32_t bytes) {
    return {0xc0055000u, 2u << 20u, Low(source), High(source), 0u, 0u, bytes};
}

std::vector<std::uint32_t> CopyDataImmediate(std::uint32_t value, const void* destination) {
    return {0xc0044000u, 0x105u, value, 0u, Low(destination), High(destination)};
}

std::vector<std::uint32_t> CopyDataMemory(const void* source, const void* destination) {
    return {0xc0044000u, 0x101u, Low(source), High(source), Low(destination), High(destination)};
}

std::vector<std::uint32_t> CopyDataClock(const void* destination) {
    return {0xc0044000u, 0x10109u, 0u, 0u, Low(destination), High(destination)};
}

std::vector<std::uint32_t> CopyDataRegister(const void* source) {
    return {0xc0044000u, 0x1u, Low(source), High(source), 0xa1f0u, 0u};
}

std::vector<std::uint32_t> Wait64(const void* awaited, std::uint32_t reference) {
    return {0xc0079300u, 0x13u, Low(awaited), High(awaited), reference, 0u, 0xffffffffu, 0u, 0x19u};
}

std::vector<std::uint32_t> Registers(std::uint32_t first, std::initializer_list<std::uint32_t> values) {
    std::vector<std::uint32_t> words{0xc0007600u | (static_cast<std::uint32_t>(values.size()) << 16u), first};
    words.insert(words.end(), values.begin(), values.end());
    return words;
}

void Append(std::vector<std::uint32_t>& words, const std::vector<std::uint32_t>& packet) {
    words.insert(words.end(), packet.begin(), packet.end());
}

std::vector<std::uint32_t> FillDispatch() {
    const auto descriptor = [](const void* pointer, std::uint32_t stride, std::uint32_t count, std::uint32_t format) {
        return std::array<std::uint32_t, 4>{Low(pointer), High(pointer) | (stride << 16u), count, format};
    };
    const auto pattern = descriptor(Pattern, 4, 4, 0x14004u);
    const auto output = descriptor(FillOutput, 4, 256, 0x14004u);
    const auto control = descriptor(Control, 16, 1, 0x4dfacu);
    std::vector<std::uint32_t> words;
    Append(words, Registers(0x207, {64, 1, 1}));
    Append(words, Registers(0x20c, {static_cast<std::uint32_t>(Address(FillCode.data()) >> 8u), static_cast<std::uint32_t>(Address(FillCode.data()) >> 40u)}));
    Append(words, Registers(0x213, {(12u << 1u) | (1u << 7u)}));
    Append(words, Registers(0x240, {pattern[0], pattern[1], pattern[2], pattern[3], output[0], output[1], output[2], output[3], control[0], control[1], control[2], control[3]}));
    Append(words, {0xc0031500u, 4, 1, 1, 0x41u | 0x8000u});
    return words;
}

std::vector<std::uint32_t> IncrementDispatch() {
    const auto descriptor = [](const void* pointer) {
        return std::array<std::uint32_t, 4>{Low(pointer), High(pointer) & 0xffffu, LaneBytes, 0x31016facu};
    };
    const auto input = descriptor(Input);
    const auto output = descriptor(Output);
    std::vector<std::uint32_t> words;
    Append(words, Registers(0x20c, {static_cast<std::uint32_t>(Address(IncrementCode.data()) >> 8u), static_cast<std::uint32_t>(Address(IncrementCode.data()) >> 40u)}));
    Append(words, Registers(0x207, {Lanes, 1u, 1u}));
    Append(words, Registers(0x213, {8u << 1u}));
    Append(words, Registers(0x240, {input[0], input[1], input[2], input[3], output[0], output[1], output[2], output[3]}));
    Append(words, {0xc0031500u, 1u, 1u, 1u, 0x8041u});
    return words;
}

std::string Probe(const char* name, std::vector<std::uint32_t> words, const std::function<void()>& afterRelease = {}) {
    WaitUntil([] { return !DrawPipeline::Queue0().Busy(); }, "the pipeline to empty before " + std::string(name));
    auto release = std::make_shared<std::atomic<bool>>(false);
    DrawPipeline::Queue0().Enqueue([release] {
        while (!release->load(std::memory_order_acquire)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }, {{Address(Held), Address(Held) + 32}});
    const auto value = ++markerValue;
    Append(words, {0xc0064900u, 0x514u, 1u << 29u, Low(Marker), High(Marker), value, 0u, 0u});
    const auto before = Requests();
    Packet packet{words.data(), static_cast<std::uint32_t>(words.size()), 0, {}};
    AgcDriver::Submit(&packet, 0);
    bool passed = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (Requests() != before) break;
        const auto pending = DrawPipeline::Queue0().PendingLabel(Address(Marker), 4);
        if (pending.has_value() && *pending == value) {
            passed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto atRelease = Requests();
    release->store(true, std::memory_order_release);
    if (afterRelease) afterRelease();
    Finish();
    std::string drained;
    for (std::size_t opcode = 0; opcode < atRelease.size(); ++opcode) {
        if (atRelease[opcode] == before[opcode]) continue;
        char item[48];
        std::snprintf(item, sizeof(item), "%s0x%02zx x%llu", drained.empty() ? "" : ", ", opcode, static_cast<unsigned long long>(atRelease[opcode] - before[opcode]));
        drained += item;
    }
    const auto outcome = passed ? std::string("passes") : drained.empty() ? std::string("blocked without a drain request") : "drains (" + drained + ")";
    std::printf("packet drain study: %-48s %s\n", name, outcome.c_str());
    std::fflush(stdout);
    return passed ? "passes" : drained.empty() ? "blocked" : "drains";
}

std::string Requested(const char* name, std::vector<std::uint32_t> words) {
    WaitUntil([] { return !DrawPipeline::Queue0().Busy(); }, "the pipeline to empty before " + std::string(name));
    const auto before = Requests();
    Packet packet{words.data(), static_cast<std::uint32_t>(words.size()), 0, {}};
    AgcDriver::Submit(&packet, 0);
    Finish();
    const auto after = Requests();
    std::string requested;
    for (std::size_t opcode = 0; opcode < after.size(); ++opcode) {
        if (after[opcode] == before[opcode]) continue;
        char item[48];
        std::snprintf(item, sizeof(item), "%s0x%02zx x%llu", requested.empty() ? "" : ", ", opcode, static_cast<unsigned long long>(after[opcode] - before[opcode]));
        requested += item;
    }
    std::printf("packet drain study: %-48s %s\n", name, requested.empty() ? "requests no drain" : ("requests a drain (" + requested + ")").c_str());
    std::fflush(stdout);
    return requested;
}

void Expect(const std::string& outcome, const char* expected, const char* name) {
    if (outcome != expected) throw std::runtime_error(std::string("packet drain study: ") + name + " " + outcome + ", expected it to " + expected);
}

const char* EnvSwitch(const char* name) {
    const char* text = std::getenv(name);
    return text != nullptr && text[0] != '\0' && text[0] != '0' ? "on" : "off";
}

}

int main() {
    try {
        {
            const auto probe = OpenVulkanTestDevice();
            if (!probe) return VulkanTestSkipped;
        }
        if (DrawPipeline::Depth() == 0) throw std::runtime_error("packet drain study: the draw pipeline is off");
        const bool dma = std::string(EnvSwitch("APS5_PIPELINE_DMA")) == "on";
        const bool dispatch = std::string(EnvSwitch("APS5_PIPELINE_DISPATCH")) == "on";
        std::printf("packet drain study: APS5_PIPELINE_DMA %s, APS5_PIPELINE_DISPATCH %s, depth %zu\n", EnvSwitch("APS5_PIPELINE_DMA"), EnvSwitch("APS5_PIPELINE_DISPATCH"), DrawPipeline::Depth());
        Shader incrementShader{};
        incrementShader.file_header = 0x34333231;
        incrementShader.version = 0x18;
        incrementShader.header_size = sizeof(Shader);
        incrementShader.shader_size = sizeof(IncrementCode);
        incrementShader.code = IncrementCode.data();
        AgcDriverRegisterShader_nid_postfix(&incrementShader);
        Source = NewBlock(BlockBytes);
        Other = NewBlock(BlockBytes);
        Held = NewBlock(BlockBytes);
        Marker = NewBlock(BlockBytes);
        Flag = NewBlock(BlockBytes);
        LargeSource = NewBlock(LargeBytes);
        LargeDestination = NewBlock(LargeBytes);
        Pattern = NewBlock(BlockBytes);
        Control = NewBlock(BlockBytes);
        FillOutput = NewBlock(BlockBytes);
        Input = NewBlock(BlockBytes);
        Output = NewBlock(BlockBytes);
        Pattern[0] = 0x5a5a5a5au;
        Control[0] = 256;
        for (std::uint32_t lane = 0; lane < Lanes; ++lane) Input[lane] = lane;

        Expect(Probe("DMA_DATA immediate fill, 64 B", DmaFill(0x11111111u, Other, 64)), dma ? "passes" : "drains", "an immediate DMA fill");
        Expect(Probe("DMA_DATA immediate fill, 16 B, onto the pending write", DmaFill(0x22222222u, Held, 16)), dma ? "passes" : "drains", "an immediate DMA fill onto the pending write");
        Expect(Probe("DMA_DATA immediate fill, 256 KiB", DmaFill(0x33333333u, LargeDestination, static_cast<std::uint32_t>(LargeBytes))), dma ? "passes" : "drains", "a large immediate DMA fill");
        Expect(Probe("DMA_DATA copy, 64 B", DmaCopy(Source, Other, 64)), dma ? "passes" : "drains", "a DMA copy");
        Expect(Probe("DMA_DATA copy, 16 B, from the pending write", DmaCopy(Held, Other, 16)), dma ? "passes" : "drains", "a DMA copy from the pending write");
        Expect(Probe("DMA_DATA copy, 256 KiB", DmaCopy(LargeSource, LargeDestination, static_cast<std::uint32_t>(LargeBytes))), dma ? "passes" : "drains", "a large DMA copy");
        Expect(Probe("DMA_DATA prefetch, 64 B", DmaPrefetch(Source, 64)), "passes", "a DMA prefetch");
        Expect(Probe("COPY_DATA immediate to memory", CopyDataImmediate(0x44444444u, Other)), "drains", "an immediate COPY_DATA");
        Expect(Probe("COPY_DATA memory to memory", CopyDataMemory(Source, Other)), "drains", "a memory COPY_DATA");
        Expect(Probe("COPY_DATA GPU clock to memory", CopyDataClock(Other)), "drains", "a clock COPY_DATA");
        Expect(Probe("COPY_DATA memory to a context register", CopyDataRegister(Source)), "drains", "a register COPY_DATA");
        Flag[0] = 1;
        Expect(Probe("WAIT_REG_MEM_64 satisfied at entry", Wait64(Flag, 1)), "passes", "a satisfied wait");
        Expect(Probe("WAIT_REG_MEM_64 on the pending write", Wait64(Held + 4, 0)), "drains", "a wait on the pending write");
        Flag[0] = 0;
        Expect(Probe("WAIT_REG_MEM_64 not yet satisfied", Wait64(Flag, 1), [] { std::atomic_ref<std::uint32_t>(Flag[0]).store(1, std::memory_order_release); }), "drains", "an unsatisfied wait");
        Expect(Probe("DISPATCH_DIRECT pattern fill, first dispatch", FillDispatch()), dispatch ? "passes" : "drains", "a pattern fill dispatch");
        auto fillAfterWait = Wait64(Flag, 1);
        Append(fillAfterWait, FillDispatch());
        Expect(Probe("DISPATCH_DIRECT pattern fill after a satisfied wait", fillAfterWait), dispatch ? "passes" : "drains", "a pattern fill dispatch after a wait");
        auto twoFills = FillDispatch();
        Append(twoFills, FillDispatch());
        Expect(Probe("DISPATCH_DIRECT two pattern fills", twoFills), dispatch ? "passes" : "drains", "two pattern fill dispatches");
        const auto firstIncrement = Probe("DISPATCH_DIRECT increment kernel, first sight", IncrementDispatch());
        Expect(firstIncrement, "drains", "the first increment kernel dispatch");
        const auto repeatIncrement = Probe("DISPATCH_DIRECT increment kernel, repeated", IncrementDispatch());
        Expect(Probe("DISPATCH_DIRECT increment kernel, repeated again", IncrementDispatch()), repeatIncrement.c_str(), "the third increment kernel dispatch");
        if (!dispatch) Expect(repeatIncrement, "drains", "a repeated increment kernel dispatch");
        const auto idleFill = Requested("idle pipeline: DISPATCH_DIRECT pattern fill", FillDispatch());
        if (!idleFill.empty()) throw std::runtime_error("packet drain study: a pattern fill dispatch requested a drain with the pipeline idle (" + idleFill + ")");
        const auto idleIncrement = Requested("idle pipeline: DISPATCH_DIRECT increment kernel", IncrementDispatch());
        if (dispatch != idleIncrement.empty()) throw std::runtime_error("packet drain study: an increment kernel dispatch with the pipeline idle requested " + (idleIncrement.empty() ? std::string("no drain") : idleIncrement));
        LibcRunShutdown_nid_postfix();
        std::puts("packet drain study done");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        try { LibcRunShutdown_nid_postfix(); } catch (...) {}
        return 1;
    }
}
