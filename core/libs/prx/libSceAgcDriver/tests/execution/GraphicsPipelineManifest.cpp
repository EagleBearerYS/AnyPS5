#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Shaders/ShaderRegistry.hpp"
#include "prx/libSceAgcDriver/Execution/include/PipelineReplay.hpp"
#include "prx/libSceAgcDriver/Submit/include/Dcb.hpp"
#include "SceShaders.hpp"
#include "VulkanTestDevice.hpp"
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

alignas(256) constexpr std::array<std::uint32_t, 8> VertexCode{0x7e000208u, 0x7e020280u, 0x7e040280u, 0x7e0602f2u, 0xf80008cfu, 0x03020100u, 0xbf810000u, 0xbf810000u};
alignas(256) constexpr std::array<std::uint32_t, 8> PixelCode{0x7e000200u, 0x7e020280u, 0xf800180fu, 0x00010100u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u};
alignas(256) std::array<std::byte, 1024> ColorMemory{};

constexpr std::uint32_t Rgba8 = 0x28028u;
constexpr std::uint32_t Unorm10_11_11 = 0x28018u;

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

Registers VertexRegisters() {
    const auto vertex = reinterpret_cast<std::uintptr_t>(VertexCode.data());
    return {{0xc8, static_cast<std::uint32_t>(vertex >> 8u)}, {0xc9, static_cast<std::uint32_t>(vertex >> 40u)}, {0x8a, 0}, {0x8b, 1u << 1u}, {0x8c, 0}};
}

Registers PixelRegisters() {
    const auto pixel = reinterpret_cast<std::uintptr_t>(PixelCode.data());
    return {{0x008, static_cast<std::uint32_t>(pixel >> 8u)}, {0x009, static_cast<std::uint32_t>(pixel >> 40u)}, {0x00a, 0}, {0x00b, 1u << 1u}, {0x00c, std::bit_cast<std::uint32_t>(1.0f)}};
}

struct Header {
    Shader shader{};
    ShaderUserData userData{};
    std::array<ShaderRegister, 5> shaderRegisters{};
    std::array<ShaderRegister, 50> contextRegisters{};
};

alignas(8) Header VertexHeader{};
alignas(8) Header PixelHeader{};

const Shader* Registered(Header& header, const std::uint32_t* code, std::size_t bytes, std::uint8_t type, const Registers& shader, const Registers& context) {
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
    return &header.shader;
}

void Append(std::vector<std::uint32_t>& commands, std::uint32_t opcode, std::initializer_list<std::uint32_t> payload) {
    commands.push_back(0xc0000000u | (static_cast<std::uint32_t>(payload.size() - 1) << 16u) | (opcode << 8u));
    commands.insert(commands.end(), payload);
}

void Register() {
    Registered(VertexHeader, VertexCode.data(), sizeof(VertexCode), 2, VertexRegisters(), Context(Rgba8));
    Registered(PixelHeader, PixelCode.data(), sizeof(PixelCode), 1, PixelRegisters(), Context(Rgba8));
}

void Draw(std::uint32_t colorInfo) {
    std::vector<std::uint32_t> commands;
    Append(commands, 0x79, {0x242, 4});
    for (const auto& [offset, value] : Context(colorInfo)) Append(commands, 0x69, {offset, value});
    for (const auto& registers : {VertexRegisters(), PixelRegisters()}) {
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
    Draw(Rgba8);
    Draw(Unorm10_11_11);
    const auto after = AgcDriver::PipelineReplay::Totals();
    Require(after.inFrames == before.inFrames + 2u && after.recorded == before.recorded + 2u, "the recording run created " + std::to_string(after.inFrames - before.inFrames) + " graphics pipelines inside frames and recorded " + std::to_string(after.recorded - before.recorded) + ", expected two of each");
    AgcDriverShutdown_nid_postfix();
}

void Replay() {
    Register();
    AgcDriver::DriverDetail::CloseRegistrationPreparation();
    const auto registered = AgcDriver::PipelineReplay::Totals();
    Require(registered.loaded == 2u && registered.replayed == 2u && registered.failed == 0u, "the registration of the programs of two recorded graphics pipelines created " + std::to_string(registered.replayed) + " of " + std::to_string(registered.loaded) + " ahead, expected two of two");
    Draw(Rgba8);
    Draw(Unorm10_11_11);
    const auto drawn = AgcDriver::PipelineReplay::Totals();
    Require(drawn.inFrames == registered.inFrames, "the draws of two graphics pipelines created at registration created " + std::to_string(drawn.inFrames - registered.inFrames) + " inside frames");
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
        const auto directory = std::filesystem::temp_directory_path() / ("aps5-graphics-manifest-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        SetVariable("ANYPS5_NO_SHADER_CACHE", "");
        SetVariable("ANYPS5_SHADER_CACHE_DIR", directory.string());
        Require(RunRecording(argv[0]) == 0, "the recording run failed");
        Replay();
        std::error_code error;
        std::filesystem::remove_all(directory, error);
        std::puts("graphics pipeline manifest tests passed");
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
