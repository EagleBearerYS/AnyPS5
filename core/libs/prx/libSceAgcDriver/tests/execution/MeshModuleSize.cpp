#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "Recompiler.hpp"
#include "../MeshGeometryFixture.hpp"
#include "VulkanTestDevice.hpp"
#include <spirv/unified1/spirv.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

using ShaderRecompiler::ShaderStage;

constexpr std::array<std::uint32_t, 4> AluOps{8u, 3u, 43u, 16u};

std::uint32_t Vop2(std::uint32_t op, std::uint32_t dst, std::uint32_t src0, std::uint32_t src1) {
    return (op << 25u) | (dst << 17u) | (src1 << 9u) | (256u + src0);
}

enum class Body { Alu, RawLoad, FormatLoad, Lds, RuntimeFormatLoad };

Body Kind = Body::Alu;
std::string DumpDirectory;

void AppendAlu(std::vector<std::uint32_t>& code, std::uint32_t count, std::uint32_t first, std::uint32_t other, std::uint32_t index) {
    if (Kind == Body::Alu) {
        for (std::uint32_t at = 0; at < count; ++at) code.push_back(Vop2(AluOps[at % AluOps.size()], first + at % 4u, first + (at + 1u) % 4u, other + at % 4u));
        return;
    }
    if (Kind == Body::Lds) {
        for (std::uint32_t at = 0; at < count; ++at) {
            const std::uint32_t offset = (at % 64u) * 4u + 0x800u;
            code.push_back(0xd8000000u | (13u << 18u) | offset);
            code.push_back(43u | ((first + at % 4u) << 8u));
            code.push_back(0xbf8cc07fu);
            code.push_back(0xd8000000u | (54u << 18u) | offset);
            code.push_back(43u | (60u << 24u));
            code.push_back(0xbf8cc07fu);
            code.push_back(Vop2(3u, first + (at + 1u) % 4u, first + (at + 1u) % 4u, 60u));
        }
        return;
    }
    if (Kind == Body::RuntimeFormatLoad) {
        code.push_back(0xf4000000u | (2u << 18u) | (16u << 6u) | (12u >> 1u));
        code.push_back(0xfa000000u);
        code.push_back(0xbf8cc07fu);
        for (std::uint32_t at = 0; at < count; ++at) {
            code.push_back(0xe0000000u | (3u << 18u) | 0x2000u | ((at % 16u) * 16u));
            code.push_back(0x80000000u | ((16u >> 2u) << 16u) | (60u << 8u) | index);
            code.push_back(0xbf8c3f70u);
            for (std::uint32_t component = 0; component < 4u; ++component) code.push_back(Vop2(3u, first + component, first + component, 60u + component));
        }
        return;
    }
    const std::uint32_t op = Kind == Body::RawLoad ? 14u : 3u;
    for (std::uint32_t at = 0; at < count; ++at) {
        code.push_back(0xe0000000u | (op << 18u) | 0x2000u | ((at % 16u) * 16u));
        code.push_back(0x80020000u | (60u << 8u) | index);
        code.push_back(0xbf8c3f70u);
        for (std::uint32_t component = 0; component < 4u; ++component) code.push_back(Vop2(3u, first + component, first + component, 60u + component));
    }
}

std::vector<std::uint32_t> MeshProgram(std::uint32_t count) {
    const auto& base = AgcDriver::Test::MeshGeometryCode;
    constexpr std::size_t insert = 99;
    constexpr std::size_t branch = 92;
    std::vector<std::uint32_t> code(base.begin(), base.begin() + insert);
    AppendAlu(code, count, 44u, 48u, 11u);
    code[branch] += static_cast<std::uint32_t>(code.size() - insert);
    code.insert(code.end(), base.begin() + insert, base.end());
    return code;
}

alignas(256) constexpr std::array<std::uint32_t, 20> VertexCode{
    0x7e000d05u, 0x080200f4u, 0x10020300u, 0x100202f4u, 0x060202f3u, 0x060400f3u, 0x10040500u, 0x060404f3u,
    0x7e060208u, 0x7e0802f2u, 0x7e0a02ffu, 0x44800000u, 0xf80000d1u, 0x00000005u, 0xf80008cfu, 0x04030201u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u,
};

std::vector<std::uint32_t> VertexProgram(std::uint32_t count) {
    constexpr std::size_t insert = 12;
    std::vector<std::uint32_t> code(VertexCode.begin(), VertexCode.begin() + insert);
    AppendAlu(code, count, 1u, 1u, 5u);
    code.insert(code.end(), VertexCode.begin() + insert, VertexCode.end());
    return code;
}

std::array<std::uint32_t, 4> BufferDescriptor(std::uint64_t address, std::uint32_t count) {
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (32u << 16u), count, 0x01016facu};
}

alignas(256) std::array<std::byte, 4096> VertexData{};
alignas(256) std::array<std::uint16_t, 64> IndexData{};
alignas(256) std::array<std::uint32_t, 4> DescriptorTable{};

struct Module {
    std::size_t words = 0;
    std::map<std::uint32_t, std::size_t> opcodeWords;
    std::size_t functions = 0;
    std::size_t blocks = 0;
};

Module Count(std::span<const std::uint32_t> words) {
    Module module;
    module.words = words.size();
    for (std::size_t at = 5; at < words.size();) {
        const auto length = words[at] >> 16u;
        const auto opcode = words[at] & 0xffffu;
        if (length == 0) break;
        module.opcodeWords[opcode] += length;
        if (opcode == spv::OpFunction) ++module.functions;
        if (opcode == spv::OpLabel) ++module.blocks;
        at += length;
    }
    return module;
}

std::optional<ShaderRecompiler::RecompileResult> LastResult;

Module Compile(const ShaderRecompiler::SpirvTarget& base, std::uint32_t subgroup, ShaderStage stage, std::span<const std::uint32_t> code, const ShaderRecompiler::MeshConfiguration& mesh) {
    auto target = base;
    target.subgroupSize = subgroup;
    std::vector<std::uint32_t> userData(16, 0u);
    const auto table = reinterpret_cast<std::uintptr_t>(DescriptorTable.data());
    userData[12] = static_cast<std::uint32_t>(table);
    userData[13] = static_cast<std::uint32_t>(table >> 32u);
    AgcDriver::Pm4::DrawParameters draw{};
    draw.indexAddress = reinterpret_cast<std::uintptr_t>(IndexData.data());
    draw.indexCount = static_cast<std::uint32_t>(IndexData.size());
    draw.indexSize = 2;
    draw.instanceCount = 1;
    const auto index = AgcDriver::Graphics::MeshIndexBufferDescriptor(draw);
    std::copy(index.begin(), index.end(), userData.begin() + ShaderRecompiler::MeshIndexBufferUserWord);
    const auto vertex = BufferDescriptor(reinterpret_cast<std::uintptr_t>(VertexData.data()), 64);
    std::copy(vertex.begin(), vertex.end(), userData.begin() + 8);
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(code)}}};
    ShaderRecompiler::RecompileRequest request{
        {stage, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
        {64, 0, userData, std::nullopt, std::nullopt, ShaderRecompiler::ShaderVertexStageInfo{}, memory},
        target,
        {0, 0, 0, ShaderRecompiler::MeshDrawPushOffsetBytes},
        stage == ShaderStage::Mesh ? std::optional<ShaderRecompiler::GraphicsCompileContext>(ShaderRecompiler::GraphicsCompileContext{0, {}, mesh, std::nullopt, {draw.indexAddress, draw.indexCount, draw.indexSize, draw.instanceCount}}) : std::nullopt
    };
    request.useCache = false;
    auto result = ShaderRecompiler::Recompile(request);
    LastResult = result;
    if (!DumpDirectory.empty()) {
        const auto path = DumpDirectory + "/" + (stage == ShaderStage::Mesh ? "mesh" : "vertex") + "-" + std::to_string(code.size()) + "-" + std::to_string(subgroup) + ".spv";
        if (auto* file = std::fopen(path.c_str(), "wb")) {
            const auto& words = result.spirv.Words();
            std::fwrite(words.data(), sizeof(std::uint32_t), words.size(), file);
            std::fclose(file);
        }
    }
    return Count(result.spirv.Words());
}

alignas(256) std::array<std::byte, 64 * 64 * 4> Pixels{};
alignas(256) constexpr std::array<std::uint32_t, 7> InterpolatedPixel{0xc8020002u, 0xc8060102u, 0xc80a0202u, 0xc80e0302u, 0xf800180fu, 0x03020100u, 0xbf810000u};
alignas(256) constexpr std::array<std::uint32_t, 5> ConstantPixel{0x7e000280u, 0x7e020280u, 0xf800180fu, 0x00010100u, 0xbf810000u};

ShaderRecompiler::RecompileResult PixelResult(const ShaderRecompiler::SpirvTarget& target, std::uint32_t vertexPush, bool interpolated) {
    ShaderRecompiler::ShaderPixelStageInfo pixel{};
    pixel.interpolatorCount = interpolated ? 1u : 0u;
    pixel.interpolatorSettings[0] = 0x400u;
    pixel.targetOutputMode[0] = 9;
    pixel.targetExportMapping.fill(0xe4u);
    const std::span<const std::uint32_t> code = interpolated ? std::span<const std::uint32_t>(InterpolatedPixel) : std::span<const std::uint32_t>(ConstantPixel);
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(code)}}};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Fragment, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
        {64, 0, {}, std::nullopt, pixel, std::nullopt, memory},
        target,
        target.fixedPushSlots ? PixelPushLayout(vertexPush, target) : ShaderRecompiler::BindingLayout{0, 0, vertexPush, ShaderRecompiler::MeshDrawPushOffsetBytes - vertexPush},
        std::nullopt
    };
    request.useCache = false;
    return ShaderRecompiler::Recompile(request);
}

double DrawMs(AgcDriver::VulkanDevice& device, ShaderStage stage, const ShaderRecompiler::RecompileResult& program, const ShaderRecompiler::MeshConfiguration& mesh) {
    const auto target = device.Target();
    const auto push = static_cast<std::uint32_t>(program.pushConstants.size());
    const auto pixel = PixelResult(target, push, stage == ShaderStage::Mesh);
    const std::array<AgcDriver::Graphics::CompiledShader, 2> shaders{{
        {stage, &program, 0},
        {ShaderStage::Fragment, &pixel, PixelPushOffset(push, target)}
    }};
    AgcDriver::Graphics::State state{};
    if (stage == ShaderStage::Mesh) state.stages = {AgcDriver::Graphics::ShaderPath::Geometry, 0x20u, 64u, 64u, mesh, std::nullopt};
    else state.stages = {AgcDriver::Graphics::ShaderPath::Vertex, 0u, 64u, 64u, std::nullopt, std::nullopt};
    state.color = {reinterpret_cast<std::uintptr_t>(Pixels.data()), {64, 64}, VK_FORMAT_R8G8B8A8_UNORM, Pixels.size(), 0xe4u};
    state.colors = {state.color};
    state.hasColorTarget = true;
    state.renderExtent = {64, 64};
    state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    state.viewport = {0, 64.0f, 64.0f, -64.0f, 0, 1};
    state.negativeOneToOne = false;
    state.scissor = {{0, 0}, {64, 64}};
    state.cullMode = VK_CULL_MODE_NONE;
    state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    state.blend.colorWriteMask = 15;
    state.blends = {state.blend};
    state.blendConstants = {};
    AgcDriver::Pm4::DrawParameters draw{};
    if (stage == ShaderStage::Mesh) draw = {reinterpret_cast<std::uintptr_t>(IndexData.data()), 12, 2, 1, 0, true};
    else draw = {0, 3, 0, 1, 0, false};
    std::lock_guard lock(AgcDriver::GuestMemory::GpuMutex());
    const auto start = std::chrono::steady_clock::now();
    device.Draw(state, draw, shaders);
    device.WaitIdle();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void PrintTop(const char* what, const Module& module) {
    std::vector<std::pair<std::size_t, std::uint32_t>> ranked;
    for (const auto& [opcode, words] : module.opcodeWords) ranked.emplace_back(words, opcode);
    std::sort(ranked.rbegin(), ranked.rend());
    std::printf("%s: %zu words, %zu functions, %zu blocks; top opcodes:", what, module.words, module.functions, module.blocks);
    for (std::size_t index = 0; index < std::min<std::size_t>(ranked.size(), 10); ++index) std::printf(" %u:%.1f%%", ranked[index].second, 100.0 * static_cast<double>(ranked[index].first) / static_cast<double>(module.words));
    std::printf("\n");
}

}

int main(int argc, char** argv) {
#ifdef _WIN32
    _putenv_s("ANYPS5_NO_SHADER_CACHE", "1");
#else
    setenv("ANYPS5_NO_SHADER_CACHE", "1", 1);
#endif
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        const auto target = device->Target();
        const ShaderRecompiler::MeshConfiguration mesh{4u, 4u, 12u, 12u, 4u, 64u, 1024u, 0u, 4u};
        std::vector<std::uint32_t> counts{0u, 100u};
        int first = 1;
        if (argc > 1 && std::string(argv[1]) == "--raw") Kind = Body::RawLoad;
        if (argc > 1 && std::string(argv[1]) == "--format") Kind = Body::FormatLoad;
        if (argc > 1 && std::string(argv[1]) == "--lds") Kind = Body::Lds;
        if (argc > 1 && std::string(argv[1]) == "--runtime-format") Kind = Body::RuntimeFormatLoad;
        DescriptorTable = BufferDescriptor(reinterpret_cast<std::uintptr_t>(VertexData.data()), 64);
        if (argc > 1 && argv[1][0] == '-') first = 2;
        if (const char* dump = std::getenv("MESH_MODULE_DUMP")) DumpDirectory = dump;
        if (argc > first) {
            counts.clear();
            for (int index = first; index < argc; ++index) counts.push_back(static_cast<std::uint32_t>(std::strtoul(argv[index], nullptr, 0)));
        }
        std::printf("host target subgroup %u\n", target.subgroupSize);
        if (std::getenv("MESH_MODULE_TIME") != nullptr) {
            for (const auto count : counts) {
                const auto plain = VertexProgram(count);
                const auto vertexWords = Compile(target, target.subgroupSize, ShaderStage::Vertex, plain, mesh).words;
                const auto vertexProgram = *LastResult;
                const auto first = DrawMs(*device, ShaderStage::Vertex, vertexProgram, mesh);
                const auto again = DrawMs(*device, ShaderStage::Vertex, vertexProgram, mesh);
                std::printf("time %u: vertex %zu words, first draw %.1f ms, again %.1f ms\n", count, vertexWords, first, again);
            }
            return 0;
        }
        for (const std::uint32_t subgroup : {64u, 32u}) {
            std::optional<std::size_t> first;
            for (const std::uint32_t vertices : {3u, 6u, 16u}) {
                const ShaderRecompiler::MeshConfiguration outputs{4u, 4u, 12u, std::max(12u, vertices * 4u), std::max(4u, vertices * 4u / 3u), 64u, 1024u, 0u, 4u};
                const auto words = Compile(target, subgroup, ShaderStage::Mesh, MeshProgram(counts.back()), outputs).words;
                std::printf("outputs %u (max vertices %u, max primitives %u) subgroup %u: ngg as mesh %zu words\n", vertices, outputs.maxVertices, outputs.maxPrimitives, subgroup, words);
                if (!first) first = words;
                if (words != *first) {
                    std::fprintf(stderr, "mesh module size: the module grew with the output vertex limit (%zu words, %zu at 3 vertices)\n", words, *first);
                    return 1;
                }
            }
        }
        for (const auto count : counts) {
            const auto ngg = MeshProgram(count);
            const auto plain = VertexProgram(count);
            for (const std::uint32_t subgroup : {64u, 32u, target.subgroupSize}) {
                const auto nggMesh = Compile(target, subgroup, ShaderStage::Mesh, ngg, mesh);
                const auto plainVertex = Compile(target, subgroup, ShaderStage::Vertex, plain, mesh);
                std::optional<Module> plainMesh;
                try {
                    plainMesh = Compile(target, subgroup, ShaderStage::Mesh, plain, mesh);
                } catch (const std::exception& error) {
                    std::printf("  plain program as mesh, %u, subgroup %u: %s\n", count, subgroup, error.what());
                }
                std::printf("%u items subgroup %u: ngg as mesh %zu words, plain vertex %zu, plain as mesh %zu\n", count, subgroup, nggMesh.words, plainVertex.words, plainMesh ? plainMesh->words : 0);
                if (count == counts.back()) {
                    PrintTop("  ngg as mesh", nggMesh);
                    PrintTop("  plain vertex", plainVertex);
                }
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
