#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Shaders/ShaderRegistry.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "SceShaders.hpp"
#include "VulkanTestDevice.hpp"
#include "GuestTestMemory.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;

constexpr std::uint32_t Threads = 32;
constexpr std::uint32_t Untouched = 0xcafebabeu;

alignas(256) constexpr std::array<std::uint32_t, 8> Code{
    0x34020082u, 0xe0301000u, 0x80000201u, 0xbf8c3f70u, 0x4a040483u, 0xe0701000u, 0x80010201u, 0xbf810000u,
};
TestGuest::Array<std::uint32_t, Threads> Input;
TestGuest::Array<std::uint32_t, Threads> Output;

std::array<std::uint32_t, 4> Descriptor(const void* data, std::uint32_t bytes) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, 0x31016facu};
}

std::vector<std::uint32_t> Commands() {
    const auto source = Descriptor(Input.data(), decltype(Input)::ByteSize);
    const auto target = Descriptor(Output.data(), decltype(Output)::ByteSize);
    const std::array<std::uint32_t, 8> data{source[0], source[1], source[2], source[3], target[0], target[1], target[2], target[3]};
    std::vector<std::uint32_t> words;
    const auto registers = [&](std::uint32_t first, std::span<const std::uint32_t> values) {
        words.push_back(0xc0007600u | (static_cast<std::uint32_t>(values.size()) << 16u));
        words.push_back(first);
        words.insert(words.end(), values.begin(), values.end());
    };
    const std::array<std::uint32_t, 3> shape{Threads, 1, 1};
    registers(0x207, shape);
    const auto address = reinterpret_cast<std::uintptr_t>(Code.data());
    const std::array<std::uint32_t, 2> program{static_cast<std::uint32_t>(address >> 8u), static_cast<std::uint32_t>(address >> 40u)};
    registers(0x20c, program);
    const std::array<std::uint32_t, 1> resource{static_cast<std::uint32_t>(data.size()) << 1u};
    registers(0x213, resource);
    registers(0x240, data);
    words.insert(words.end(), {0xc0031500u, 1, 1, 1, 0x8041u});
    return words;
}

void Register() {
    struct Header {
        Shader shader{};
        std::array<ShaderRegister, 7> registers{};
        ShaderSpecialRegs specials{};
    };
    alignas(8) static Header header{};
    const auto address = reinterpret_cast<std::uintptr_t>(Code.data());
    header.shader.file_header = 0x34333231u;
    header.shader.version = 0x18;
    header.shader.header_size = sizeof(Header);
    header.shader.shader_size = sizeof(Code);
    header.shader.code = Code.data();
    header.shader.sh_registers = header.registers.data();
    header.shader.num_sh_registers = header.registers.size();
    header.shader.specials = &header.specials;
    header.specials.dispatch_modifier = 0x8000u;
    header.registers = {{{0x20c, static_cast<std::uint32_t>(address >> 8u)}, {0x20d, static_cast<std::uint32_t>(address >> 40u)}, {0x207, Threads}, {0x208, 1}, {0x209, 1}, {0x212, 0}, {0x213, 8u << 1u}}};
    AgcDriverRegisterShader_nid_postfix(&header.shader);
}

void Dispatch(const std::string& what) {
    Output.fill(Untouched);
    const auto words = Commands();
    Packet packet{const_cast<std::uint32_t*>(words.data()), static_cast<std::uint32_t>(words.size()), 0, {}};
    AgcDriver::Submit(&packet, 0);
    AgcDriverWaitIdle_nid_postfix();
    AgcDriver::GuestMemory::FlushGpuWrites(reinterpret_cast<std::uintptr_t>(Output.data()), decltype(Output)::ByteSize);
    for (std::uint32_t lane = 0; lane < Threads; ++lane) Require(Output[lane] == Input[lane] + 3u, what + ": lane " + std::to_string(lane) + " is " + std::to_string(Output[lane]) + ", expected " + std::to_string(Input[lane] + 3u));
}

void SetVariable(const char* name, const std::string& value) {
#ifdef _WIN32
    Require(_putenv_s(name, value.c_str()) == 0, std::string("cannot set ") + name);
#else
    Require((value.empty() ? unsetenv(name) : setenv(name, value.c_str(), 1)) == 0, std::string("cannot set ") + name);
#endif
}

int RunRecording(const char* executable) {
#ifdef _WIN32
    const std::string command = "\"\"" + std::string(executable) + "\" --record\"";
#else
    const std::string command = "'" + std::string(executable) + "' --record";
#endif
    return std::system(command.c_str());
}

void Record() {
    Register();
    Dispatch("the recording run");
    const auto creations = AgcDriver::VulkanDevice::ComputePipelineCreations();
    Require(creations.inFrames == 1u && creations.ahead == 0u, "the recording run created " + std::to_string(creations.inFrames) + " compute pipelines inside frames and " + std::to_string(creations.ahead) + " ahead, expected one inside a frame");
    AgcDriverShutdown_nid_postfix();
}

void Replay() {
    const auto before = AgcDriver::VulkanDevice::ComputePipelineCreations();
    Register();
    AgcDriver::DriverDetail::CloseRegistrationPreparation();
    const auto registered = AgcDriver::VulkanDevice::ComputePipelineCreations();
    Require(registered.ahead == before.ahead + 1u && registered.inFrames == before.inFrames, "the registration of a program whose pipeline an earlier run recorded created " + std::to_string(registered.ahead - before.ahead) + " compute pipelines ahead, expected one");
    Dispatch("the run that replays the manifest");
    const auto dispatched = AgcDriver::VulkanDevice::ComputePipelineCreations();
    Require(dispatched.inFrames == registered.inFrames && dispatched.ahead == registered.ahead, "the dispatch of a program whose pipeline was created at its registration created another one");
    AgcDriverShutdown_nid_postfix();
}

}

int main(int argc, char** argv) {
    try {
        for (std::uint32_t lane = 0; lane < Threads; ++lane) Input[lane] = lane * 2654435761u + 11u;
        if (argc == 2 && std::string(argv[1]) == "--record") {
            Record();
            return 0;
        }
        Require(argc == 1, "usage: [--record]");
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
        }
        const auto directory = std::filesystem::temp_directory_path() / ("aps5-pipeline-manifest-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        SetVariable("ANYPS5_NO_SHADER_CACHE", "");
        SetVariable("ANYPS5_SHADER_CACHE_DIR", directory.string());
        Require(RunRecording(argv[0]) == 0, "the recording run failed");
        bool recorded = false;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) recorded |= entry.path().extension() == ".manifest";
        Require(recorded, "the recording run left no pipeline manifest");
        Replay();
        std::error_code error;
        std::filesystem::remove_all(directory, error);
        std::puts("compute pipeline manifest tests passed");
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
