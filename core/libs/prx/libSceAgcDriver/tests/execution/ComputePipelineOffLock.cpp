#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "Recompiler.hpp"
#include "Optimization/ResourceProgram.hpp"
#include "VulkanTestDevice.hpp"
#include "GuestTestMemory.hpp"
#include <array>
#include <barrier>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Threads = 32;
constexpr std::uint32_t Untouched = 0xcafebabeu;

alignas(256) constexpr std::array<std::uint32_t, 7> Code{
    0x34020082u, 0xe0301000u, 0x80000201u, 0xbf8c3f70u, 0xe0701000u, 0x80010201u, 0xbf810000u,
};
alignas(256) constexpr std::array<std::uint32_t, 8> Other{
    0x34020082u, 0xe0301000u, 0x80000201u, 0xbf8c3f70u, 0x4a040481u, 0xe0701000u, 0x80010201u, 0xbf810000u,
};
TestGuest::Array<std::uint32_t, Threads> Input;
TestGuest::Array<std::uint32_t, Threads> Shifted;
TestGuest::Array<std::uint32_t, Threads> Output;

std::array<std::uint32_t, 4> Descriptor(const void* data, std::uint32_t bytes) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, 0x31016facu};
}

std::array<std::uint32_t, 8> UserData(const void* input) {
    const auto source = Descriptor(input, decltype(Input)::ByteSize);
    const auto target = Descriptor(Output.data(), decltype(Output)::ByteSize);
    return {source[0], source[1], source[2], source[3], target[0], target[1], target[2], target[3]};
}

TestGuest::Vector<std::uint32_t> Commands(const std::uint32_t* program, const std::array<std::uint32_t, 8>& data) {
    TestGuest::Vector<std::uint32_t> words;
    const auto registers = [&](std::uint32_t first, std::span<const std::uint32_t> values) {
        words.push_back(0xc0007600u | (static_cast<std::uint32_t>(values.size()) << 16u));
        words.push_back(first);
        words.insert(words.end(), values.begin(), values.end());
    };
    const std::array<std::uint32_t, 3> shape{Threads, 1, 1};
    registers(0x207, shape);
    const auto address = reinterpret_cast<std::uintptr_t>(program);
    const std::array<std::uint32_t, 2> code{static_cast<std::uint32_t>(address >> 8u), static_cast<std::uint32_t>(address >> 40u)};
    registers(0x20c, code);
    const std::array<std::uint32_t, 1> resource{static_cast<std::uint32_t>(data.size()) << 1u};
    registers(0x213, resource);
    registers(0x240, data);
    words.insert(words.end(), {0xc0031500u, 1, 1, 1, 0x8041u});
    return words;
}

void Dispatch(const std::uint32_t* program, const void* input) {
    Output.fill(Untouched);
    const auto words = Commands(program, UserData(input));
    Packet packet{const_cast<std::uint32_t*>(words.data()), static_cast<std::uint32_t>(words.size()), 0, {}};
    AgcDriver::Submit(&packet, 0);
    AgcDriverWaitIdle_nid_postfix();
    AgcDriver::GuestMemory::FlushGpuWrites(reinterpret_cast<std::uintptr_t>(Output.data()), decltype(Output)::ByteSize);
}

void Expect(const std::array<std::uint32_t, Threads>& expected, std::uint32_t added, const std::string& what) {
    for (std::uint32_t lane = 0; lane < Threads; ++lane) Require(Output[lane] == expected[lane] + added, what + ": lane " + std::to_string(lane) + " is " + std::to_string(Output[lane]) + ", expected " + std::to_string(expected[lane] + added));
}

void Driver() {
    const auto before = AgcDriver::VulkanDevice::ComputePipelineCreations();
    Dispatch(Code.data(), Input.data());
    Expect(Input, 0u, "the first dispatch of a program");
    const auto first = AgcDriver::VulkanDevice::ComputePipelineCreations();
    Require(first.inFrames == before.inFrames + 1u, "the first dispatch of a program created " + std::to_string(first.inFrames - before.inFrames) + " compute pipelines, expected one");
    Require(first.underLock == before.underLock, "the dispatch created its compute pipeline while holding the GPU mutex");
    Dispatch(Code.data(), Shifted.data());
    Expect(Shifted, 0u, "a dispatch of the same program over another buffer");
    const auto second = AgcDriver::VulkanDevice::ComputePipelineCreations();
    Require(second.inFrames == first.inFrames && second.underLock == first.underLock, "a dispatch of a program whose pipeline exists created another one");
}

ShaderRecompiler::RecompileRequest Request(std::span<const std::uint32_t> code, std::span<const std::uint32_t> userData, const ShaderRecompiler::SpirvTarget& target, std::span<const ShaderRecompiler::MemoryRegion> memory) {
    const ShaderRecompiler::ShaderComputeStageInfo compute{{Threads, 1, 1}, 0u, {false, false, false}, false, 1};
    return {{ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}}, {Threads, 0, userData, compute, std::nullopt, std::nullopt, memory}, target, {0, 0, 0, 128}};
}

void Concurrent(AgcDriver::VulkanDevice& device) {
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(Other.data()), std::as_bytes(std::span(Other))}}};
    const auto userData = UserData(Input.data());
    const auto request = Request(Other, userData, device.ComputeTarget(Threads), memory);
    const auto handle = ShaderRecompiler::PrepareShader(request);
    ShaderRecompiler::SrtRuntime runtime{};
    runtime.userData = userData;
    const auto capture = ShaderRecompiler::CaptureResources(request, runtime, *handle);
    const auto result = ShaderRecompiler::MaterializeShader(request, *capture, *handle);
    const auto before = AgcDriver::VulkanDevice::ComputePipelineCreations();
    constexpr std::size_t Callers = 8;
    std::barrier start(static_cast<std::ptrdiff_t>(Callers));
    std::vector<std::thread> callers;
    for (std::size_t index = 0; index < Callers; ++index) {
        callers.emplace_back([&] {
            start.arrive_and_wait();
            device.PrepareComputePipeline(*result);
        });
    }
    for (auto& caller : callers) caller.join();
    const auto after = AgcDriver::VulkanDevice::ComputePipelineCreations();
    Require(after.inFrames == before.inFrames + 1u, "eight threads that needed one new compute pipeline at once created " + std::to_string(after.inFrames - before.inFrames));
    Output.fill(Untouched);
    device.Dispatch(*result, 1, 1, 1, reinterpret_cast<std::uintptr_t>(Other.data()));
    device.WaitIdle();
    Expect(Input, 1u, "a dispatch whose pipeline was created before it");
    Require(AgcDriver::VulkanDevice::ComputePipelineCreations().inFrames == after.inFrames, "a dispatch whose pipeline was created before it created another one");
}

}

int main() {
    try {
        for (std::uint32_t lane = 0; lane < Threads; ++lane) {
            Input[lane] = lane * 2654435761u + 17u;
            Shifted[lane] = lane * 40503u + 3u;
        }
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
            Concurrent(*device);
        }
        Driver();
        AgcDriverShutdown_nid_postfix();
        std::puts("compute pipeline off-lock tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        try {
            AgcDriverShutdown_nid_postfix();
        } catch (const std::exception&) {
        }
        return 1;
    }
}
