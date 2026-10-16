#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Shaders/ShaderRegistry.hpp"
#include "prx/libSceAgcDriver/Execution/include/PipelineReplay.hpp"
#include "prx/libSceAgcDriver/Submit/include/Dcb.hpp"
#include "SceShaders.hpp"
#include "VulkanTestDevice.hpp"
#include "GuestTestMemory.hpp"
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using Registers = std::vector<std::pair<std::uint32_t, std::uint32_t>>;

constexpr std::size_t Programs = 2;
constexpr std::size_t Formats = 2;
constexpr std::size_t Pipelines = Programs * Programs * Formats;

alignas(256) constexpr std::array<std::uint32_t, 8> FirstVertexCode{0x7e000208u, 0x7e020280u, 0x7e040280u, 0x7e0602f2u, 0xf80008cfu, 0x03020100u, 0xbf810000u, 0xbf810000u};
alignas(256) constexpr std::array<std::uint32_t, 8> SecondVertexCode{0x7e000208u, 0x7e020280u, 0x7e0402f0u, 0x7e0602f2u, 0xf80008cfu, 0x03020100u, 0xbf810000u, 0xbf810000u};
alignas(256) constexpr std::array<std::uint32_t, 8> FirstPixelCode{0x7e000200u, 0x7e020280u, 0xf800180fu, 0x00010100u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u};
alignas(256) constexpr std::array<std::uint32_t, 8> SecondPixelCode{0x7e000200u, 0x7e020280u, 0xf800180fu, 0x00000001u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u};
constexpr std::array<const std::array<std::uint32_t, 8>*, Programs> VertexCodes{&FirstVertexCode, &SecondVertexCode};
constexpr std::array<const std::array<std::uint32_t, 8>*, Programs> PixelCodes{&FirstPixelCode, &SecondPixelCode};
TestGuest::Array<std::byte, 1024> ColorMemory;

constexpr std::uint32_t Rgba8 = 0x28028u;
constexpr std::uint32_t Unorm10_11_11 = 0x28018u;
constexpr std::array<std::uint32_t, Formats> ColorInfos{Rgba8, Unorm10_11_11};

int failures = 0;

void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::cerr << "FAIL: " << what << '\n';
    ++failures;
}

struct Combination {
    std::size_t vertex;
    std::size_t pixel;
    std::uint32_t colorInfo;
};

Combination Pipeline(std::size_t index) {
    return {index % Programs, (index / Programs) % Programs, ColorInfos[index / (Programs * Programs)]};
}

std::string Name(const Combination& combination) {
    return "vertex program " + std::to_string(combination.vertex) + ", pixel program " + std::to_string(combination.pixel) + " and a " + (combination.colorInfo == Rgba8 ? "8_8_8_8" : "10_11_11") + " target";
}

Registers Context(std::uint32_t colorInfo) {
    const auto color = reinterpret_cast<std::uintptr_t>(ColorMemory.data());
    return {
        {0x2d5, 0x2000},
        {0x1b6, 0}, {0x207, 0}, {0x200, 0}, {0x203, 0x800},
        {0x2dc, 0xaa00}, {0x2f8, 0}, {0x292, 2}, {0x293, 0},
        {0x80, 0}, {0x8d, 0}, {0x83, 0xffff}, {0x8c, 0xa},
        {0x2f9, 0x2d}, {0x313, 0x6000}, {0x30e, 0xffffffff}, {0x30f, 0xffffffff},
        {0x206, 0x43f}, {0x204, 0x80000}, {0x205, 0x240},
        {0x8e, 0xf}, {0x8f, 0xf}, {0x202, 0xcc0010},
        {0x1c4, 0}, {0x1c5, 9}, {0x1c3, 4}, {0x31c, colorInfo},
        {0x31b, 0}, {0x31d, 0}, {0x3b0, (63u << 14u) | 3u},
        {0x3b8, 0x9000000}, {0x1e0, 0},
        {0xc, 0}, {0xd, 0x40040},
        {0x81, 0x80000000}, {0x82, 0x40040},
        {0x90, 0x80000000}, {0x91, 0x40040},
        {0x94, 0x80000000}, {0x95, 0x40040},
        {0x318, static_cast<std::uint32_t>(color >> 8u)}, {0x390, static_cast<std::uint32_t>(color >> 40u)},
        {0x10f, std::bit_cast<std::uint32_t>(32.0f)}, {0x110, std::bit_cast<std::uint32_t>(32.0f)},
        {0x111, std::bit_cast<std::uint32_t>(-2.0f)}, {0x112, std::bit_cast<std::uint32_t>(2.0f)},
        {0x113, std::bit_cast<std::uint32_t>(1.0f)}, {0x114, 0},
        {0xb4, 0}, {0xb5, std::bit_cast<std::uint32_t>(1.0f)},
    };
}

Registers VertexRegisters(std::size_t program) {
    const auto vertex = reinterpret_cast<std::uintptr_t>(VertexCodes[program]->data());
    return {{0xc8, static_cast<std::uint32_t>(vertex >> 8u)}, {0xc9, static_cast<std::uint32_t>(vertex >> 40u)}, {0x8a, 0}, {0x8b, 1u << 1u}, {0x8c, 0}};
}

Registers PixelRegisters(std::size_t program) {
    const auto pixel = reinterpret_cast<std::uintptr_t>(PixelCodes[program]->data());
    return {{0x008, static_cast<std::uint32_t>(pixel >> 8u)}, {0x009, static_cast<std::uint32_t>(pixel >> 40u)}, {0x00a, 0}, {0x00b, 1u << 1u}, {0x00c, std::bit_cast<std::uint32_t>(1.0f)}};
}

struct Header {
    Shader shader{};
    ShaderUserData userData{};
    std::array<ShaderRegister, 5> shaderRegisters{};
    std::array<ShaderRegister, 50> contextRegisters{};
};

struct alignas(8) Headers {
    std::array<Header, Programs> vertex{};
    std::array<Header, Programs> pixel{};
};

Headers Registrations;

void Registered(Header& header, const std::uint32_t* code, std::size_t bytes, std::uint8_t type, const Registers& shader, const Registers& context) {
    header.shader.file_header = 0x34333231u;
    header.shader.version = 0x18u;
    header.shader.header_size = sizeof(Header);
    header.shader.shader_size = static_cast<std::uint32_t>(bytes);
    header.shader.code = code;
    header.shader.type = type;
    header.shader.user_data = &header.userData;
    Require(shader.size() == header.shaderRegisters.size() && context.size() == header.contextRegisters.size(), "the test's register lists do not fit its headers");
    for (std::size_t index = 0; index < shader.size(); ++index) header.shaderRegisters[index] = {shader[index].first, shader[index].second};
    for (std::size_t index = 0; index < context.size(); ++index) header.contextRegisters[index] = {context[index].first, context[index].second};
    header.shader.sh_registers = header.shaderRegisters.data();
    header.shader.num_sh_registers = static_cast<std::uint32_t>(header.shaderRegisters.size());
    header.shader.cx_registers = header.contextRegisters.data();
    header.shader.num_cx_registers = static_cast<std::uint32_t>(header.contextRegisters.size());
    AgcDriverRegisterShader_nid_postfix(&header.shader);
}

void Append(TestGuest::Vector<std::uint32_t>& commands, std::uint32_t opcode, std::initializer_list<std::uint32_t> payload) {
    commands.push_back(0xc0000000u | (static_cast<std::uint32_t>(payload.size() - 1) << 16u) | (opcode << 8u));
    commands.insert(commands.end(), payload);
}

void Register() {
    for (std::size_t program = 0; program < Programs; ++program) {
        Registered(Registrations.vertex[program], VertexCodes[program]->data(), sizeof(FirstVertexCode), 2, VertexRegisters(program), Context(Rgba8));
        Registered(Registrations.pixel[program], PixelCodes[program]->data(), sizeof(FirstPixelCode), 1, PixelRegisters(program), Context(Rgba8));
    }
}

void Draw(const Combination& combination) {
    TestGuest::Vector<std::uint32_t> commands;
    Append(commands, 0x79, {0x242, 4});
    for (const auto& [offset, value] : Context(combination.colorInfo)) Append(commands, 0x69, {offset, value});
    for (const auto& registers : {VertexRegisters(combination.vertex), PixelRegisters(combination.pixel)}) {
        for (const auto& [offset, value] : registers) Append(commands, 0x76, {offset, value});
    }
    Append(commands, 0x2d, {3, 2});
    Packet packet{commands.data(), static_cast<std::uint32_t>(commands.size()), 0, {}};
    Require(sceAgcDriverSubmitDcb(&packet) == 0, "the draw submission failed");
    AgcDriverWaitIdle_nid_postfix();
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
    AgcDriver::DriverDetail::CloseRegistrationPreparation();
    const auto before = AgcDriver::PipelineReplay::Totals();
    for (std::size_t index = 0; index < Pipelines; ++index) Draw(Pipeline(index));
    const auto after = AgcDriver::PipelineReplay::Totals();
    Require(after.inFrames == before.inFrames + Pipelines && after.recorded == before.recorded + Pipelines, "the recording run created " + std::to_string(after.inFrames - before.inFrames) + " graphics pipelines inside frames and recorded " + std::to_string(after.recorded - before.recorded) + ", expected " + std::to_string(Pipelines) + " of each");
    AgcDriverShutdown_nid_postfix();
}

void Replay() {
    Register();
    AgcDriver::DriverDetail::CloseRegistrationPreparation();
    const auto registered = AgcDriver::PipelineReplay::Totals();
    Expect(registered.loaded == Pipelines && registered.replayed == Pipelines && registered.failed == 0u, "the registration of two vertex and two pixel programs, recorded in " + std::to_string(Pipelines) + " graphics pipelines by the first run, created " + std::to_string(registered.replayed) + " of the " + std::to_string(registered.loaded) + " loaded ahead (" + std::to_string(registered.failed) + " failed), expected all " + std::to_string(Pipelines));
    auto seen = registered;
    for (std::size_t index = 0; index < Pipelines; ++index) {
        const auto combination = Pipeline(index);
        Draw(combination);
        const auto drawn = AgcDriver::PipelineReplay::Totals();
        Expect(drawn.inFrames == seen.inFrames, "the first draw with " + Name(combination) + " created " + std::to_string(drawn.inFrames - seen.inFrames) + " graphics pipelines inside the frame");
        Expect(drawn.used == seen.used + 1u, "the first draw with " + Name(combination) + " used " + std::to_string(drawn.used - seen.used) + " pipelines created ahead from the manifest, expected one");
        seen = drawn;
    }
    for (std::size_t index = 0; index < Pipelines; ++index) Draw(Pipeline(Pipelines - 1u - index));
    const auto again = AgcDriver::PipelineReplay::Totals();
    Expect(again.inFrames == seen.inFrames && again.used == seen.used, "the second draws of the " + std::to_string(Pipelines) + " combinations, in reverse order, created " + std::to_string(again.inFrames - seen.inFrames) + " pipelines inside frames and used " + std::to_string(again.used - seen.used) + " more manifest entries");
    AgcDriverShutdown_nid_postfix();
}

}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--record") {
            Record();
            return 0;
        }
        Require(argc == 1, "usage: [--record]");
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
            if (!device->GraphicsPipelineLibraries()) return VulkanTestSkipped;
        }
        const auto directory = std::filesystem::temp_directory_path() / ("aps5-graphics-prebuild-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        SetVariable("ANYPS5_NO_SHADER_CACHE", "");
        SetVariable("ANYPS5_SHADER_CACHE_DIR", directory.string());
        Require(RunRecording(argv[0]) == 0, "the recording run failed");
        Replay();
        std::error_code error;
        std::filesystem::remove_all(directory, error);
        if (failures != 0) {
            std::cerr << failures << " graphics pipeline prebuild checks failed\n";
            return 1;
        }
        std::puts("graphics pipeline prebuild tests passed");
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
