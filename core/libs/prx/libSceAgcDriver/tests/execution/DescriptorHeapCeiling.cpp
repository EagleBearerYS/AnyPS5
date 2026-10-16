#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ColorTargetLayout.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ExpProfile.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libc/include/GuestWriteWatch.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;
namespace ExpProfile = AgcDriver::Graphics::ExpProfile;

constexpr std::uint32_t TexelWidth = 64;
constexpr std::uint32_t TexelHeight = 4;
constexpr std::uint32_t Format32Float = 22;
constexpr std::uint32_t Type2D = 9;
constexpr std::size_t BlockBytes = 0x10000;
constexpr std::uint32_t TargetWidth = 64;
constexpr std::uint32_t TargetHeight = 32;

alignas(256) constexpr std::array<std::uint32_t, 6> VertexCode{0xe0382000u, 0x80000005u, 0xbf8c3f70u, 0xf80008cfu, 0x03020100u, 0xbf810000u};

alignas(256) constexpr std::array<std::uint32_t, 21> PixelTableCode{
    0xf4200404u, 0xfa000000u, 0xbf8cc07fu, 0x8f118210u, 0xf4200482u, 0x22000000u, 0xbf8cc07fu,
    0x8f138412u, 0xf4280300u, 0x26000000u, 0x7e140280u, 0x7e160280u, 0xbf8cc07fu, 0xf0008f08u,
    0x0003100au, 0xbf8c3f70u, 0xf800180fu, 0x13121110u, 0xbf810000u, 0xbf810000u, 0xbf810000u,
};

std::size_t TargetBytes = 0;
const std::array<std::array<float, 4>, 3> Triangle{{{-1.0f, -1.0f, 0.5f, 1.0f}, {3.0f, -1.0f, 0.5f, 1.0f}, {-1.0f, 3.0f, 0.5f, 1.0f}}};

std::array<std::uint32_t, 4> Buffer(const void* base, std::uint32_t bytes) {
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(base));
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, 0x31016facu};
}

std::array<std::uint32_t, 4> VertexBuffer(const void* data, std::uint32_t stride, std::uint32_t count) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (stride << 16u), count, 0x01016facu};
}

std::array<std::uint32_t, 4> Image(const void* texels) {
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(texels));
    return {static_cast<std::uint32_t>(address >> 8u), static_cast<std::uint32_t>((address >> 40u) & 0xffu) | (Format32Float << 20u) | (((TexelWidth - 1u) & 3u) << 30u), ((TexelWidth - 1u) >> 2u) | ((TexelHeight - 1u) << 14u) | (1u << 31u), 0xfacu | (Type2D << 28u)};
}

std::vector<std::uint32_t> DirectCode(std::uint32_t images) {
    std::vector<std::uint32_t> code{0x7e140280u, 0x7e160280u, 0x7e200280u, 0x7e220280u, 0x7e240280u, 0x7e260280u};
    for (std::uint32_t image = 0; image < images; ++image) {
        code.push_back(0xf4080300u);
        code.push_back(0xfa000000u | (image * 16u));
        code.push_back(0xbf8cc07fu);
        code.push_back(0xf0008108u);
        code.push_back(0x0003140au);
        code.push_back(0xbf8c3f70u);
        code.push_back(0x06202114u);
    }
    code.push_back(0xf800180fu);
    code.push_back(0x13121110u);
    code.push_back(0xbf810000u);
    return code;
}

class GuestBlocks {
public:
    explicit GuestBlocks(std::size_t bytes) : bytes(bytes) {
#ifdef _WIN32
        base = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(bytes, BlockBytes));
        if (base != nullptr) GuestArena::GuestArenaCommit_nid_postfix(base, bytes, PAGE_READWRITE, bytes);
#else
        void* mapped = mmap(nullptr, bytes + BlockBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapped != MAP_FAILED) {
            const auto begin = reinterpret_cast<std::uintptr_t>(mapped);
            const auto aligned = (begin + BlockBytes - 1) & ~(static_cast<std::uintptr_t>(BlockBytes) - 1);
            if (aligned != begin) munmap(mapped, aligned - begin);
            if (aligned + bytes != begin + bytes + BlockBytes) munmap(reinterpret_cast<void*>(aligned + bytes), begin + BlockBytes - aligned);
            base = reinterpret_cast<std::uint8_t*>(aligned);
            if (AgcDriver::GuestMemory::WriteWatched()) GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(base, bytes);
        }
#endif
        Require(base != nullptr, "descriptor heap ceiling: cannot allocate guest memory");
        GuestAllocations::Mutation mutation;
        mutation.Add(base, bytes, true, true, true);
    }

    ~GuestBlocks() {
        {
            GuestAllocations::Mutation mutation;
            mutation.Remove(base);
        }
#ifdef _WIN32
        GuestArena::GuestArenaReset_nid_postfix(base, bytes);
        GuestArena::GuestArenaRelease_nid_postfix(base, bytes);
#else
        munmap(base, bytes);
        if (AgcDriver::GuestMemory::WriteWatched()) GuestWriteWatch::GuestWriteWatchUnregister_nid_postfix(base, bytes);
#endif
    }

    GuestBlocks(const GuestBlocks&) = delete;
    GuestBlocks& operator=(const GuestBlocks&) = delete;

    std::uint8_t* base = nullptr;
    std::size_t bytes = 0;
};

GuestBlocks* Target = nullptr;

struct Scene {
    std::uint32_t images = 0;
    bool direct = false;
    GuestBlocks blocks;
    GuestBlocks textures;
    std::vector<std::uint32_t> directCode;
    ShaderRecompiler::RecompileResult vertex;
    ShaderRecompiler::RecompileResult pixel;
    std::array<AgcDriver::Graphics::CompiledShader, 2> shaders{};
    AgcDriver::Graphics::State state{};

    Scene(std::uint32_t images, bool direct) : images(images), direct(direct), blocks(2u * BlockBytes), textures(static_cast<std::size_t>(images) * BlockBytes) {}

    float* Texels(std::uint32_t image) const { return reinterpret_cast<float*>(textures.base + static_cast<std::size_t>(image) * BlockBytes); }
    std::uint32_t* Table() const { return reinterpret_cast<std::uint32_t*>(blocks.base); }
    std::uint32_t* Keys() const { return reinterpret_cast<std::uint32_t*>(blocks.base + BlockBytes); }
    std::uint32_t* Constants() const { return reinterpret_cast<std::uint32_t*>(blocks.base + BlockBytes + BlockBytes / 2u); }
};

void Build(AgcDriver::VulkanDevice& device, Scene& scene, std::uint32_t key) {
    const auto target = device.Target();
    for (std::uint32_t image = 0; image < scene.images; ++image) {
        std::fill_n(scene.Texels(image), TexelWidth * TexelHeight, image == key ? 1.0f : 0.0f);
        const auto words = Image(scene.Texels(image));
        std::copy(words.begin(), words.end(), scene.Table() + image * 4u);
    }
    const auto tableBytes = scene.images * 16u;
    std::vector<std::uint32_t> vertexUserData(4, 0u);
    auto* triangle = reinterpret_cast<std::array<float, 4>*>(scene.Constants() + 64);
    std::copy(Triangle.begin(), Triangle.end(), triangle);
    const auto vertices = VertexBuffer(triangle, 16u, static_cast<std::uint32_t>(Triangle.size()));
    std::copy(vertices.begin(), vertices.end(), vertexUserData.begin());
    const std::array<ShaderRecompiler::MemoryRegion, 1> vertexMemory{{{reinterpret_cast<std::uintptr_t>(VertexCode.data()), std::as_bytes(std::span(VertexCode))}}};
    ShaderRecompiler::RecompileRequest vertex{
        {ShaderStage::Vertex, reinterpret_cast<std::uintptr_t>(VertexCode.data()), VertexCode, 0, {}},
        {64, 0, vertexUserData, std::nullopt, std::nullopt, ShaderRecompiler::ShaderVertexStageInfo{}, vertexMemory},
        target,
        {0, 0, 0, 64}
    };
    scene.vertex = ShaderRecompiler::Recompile(vertex);
    const auto vertexPush = static_cast<std::uint32_t>(scene.vertex.pushConstants.size());
    ShaderRecompiler::ShaderPixelStageInfo pixel{};
    pixel.wave32 = false;
    pixel.inputAddr = ShaderRecompiler::PixelInputBit(ShaderRecompiler::PixelInput::PositionX) | ShaderRecompiler::PixelInputBit(ShaderRecompiler::PixelInput::PositionY);
    pixel.posX = true;
    pixel.posY = true;
    pixel.targetOutputMode[0] = 9;
    pixel.targetExportMapping.fill(0xe4u);
    std::vector<std::uint32_t> pixelUserData(12, 0u);
    std::span<const std::uint32_t> code;
    if (scene.direct) {
        scene.directCode = DirectCode(scene.images);
        code = scene.directCode;
        const auto table = reinterpret_cast<std::uintptr_t>(scene.Table());
        pixelUserData[0] = static_cast<std::uint32_t>(table);
        pixelUserData[1] = static_cast<std::uint32_t>(table >> 32u);
    } else {
        code = PixelTableCode;
        const auto table = Buffer(scene.Table(), tableBytes);
        std::copy(table.begin(), table.end(), pixelUserData.begin());
        for (std::uint32_t image = 0; image < scene.images; ++image) scene.Keys()[image] = image;
        scene.Constants()[0] = key;
        const auto keys = Buffer(scene.Keys(), scene.images * 4u);
        const auto constants = Buffer(scene.Constants(), 16u);
        std::copy(keys.begin(), keys.end(), pixelUserData.begin() + 4);
        std::copy(constants.begin(), constants.end(), pixelUserData.begin() + 8);
    }
    const std::array<ShaderRecompiler::MemoryRegion, 4> pixelMemory{{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(code)}, {reinterpret_cast<std::uintptr_t>(scene.Table()), std::as_bytes(std::span(scene.Table(), scene.images * 4u))}, {reinterpret_cast<std::uintptr_t>(scene.Keys()), std::as_bytes(std::span(scene.Keys(), scene.images))}, {reinterpret_cast<std::uintptr_t>(scene.Constants()), std::as_bytes(std::span(scene.Constants(), 4u))}}};
    ShaderRecompiler::RecompileRequest fragment{
        {ShaderStage::Fragment, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
        {64, 0, pixelUserData, std::nullopt, pixel, std::nullopt, pixelMemory},
        target,
        PixelPushLayout(vertexPush, target)
    };
    scene.pixel = ShaderRecompiler::Recompile(fragment);
    scene.shaders = {{{ShaderStage::Vertex, &scene.vertex, 0}, {ShaderStage::Fragment, &scene.pixel, PixelPushOffset(vertexPush, target)}}};
    auto& state = scene.state;
    state.stages = {AgcDriver::Graphics::ShaderPath::Vertex, 0u, 64u, 64u, std::nullopt, std::nullopt};
    state.color = {reinterpret_cast<std::uintptr_t>(Target->base), {TargetWidth, TargetHeight}, VK_FORMAT_R8G8B8A8_UNORM, TargetBytes, 0xe4u};
    state.color.surfaceAddress = state.color.address;
    state.color.tileMode = AgcDriver::Graphics::ColorTileMode::RenderTarget;
    state.color.elementBytes = 4;
    state.colors = {state.color};
    state.hasColorTarget = true;
    state.renderExtent = {TargetWidth, TargetHeight};
    state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    state.viewport = {0, static_cast<float>(TargetHeight), static_cast<float>(TargetWidth), -static_cast<float>(TargetHeight), 0, 1};
    state.negativeOneToOne = false;
    state.scissor = {{0, 0}, {TargetWidth, TargetHeight}};
    state.cullMode = VK_CULL_MODE_NONE;
    state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    state.blend.colorWriteMask = 15;
    state.blends = {state.blend};
    state.blendConstants = {};
}

std::string Roles(const Scene& scene) {
    std::string text;
    for (const auto& binding : scene.pixel.bindings) {
        if (!text.empty()) text += ", ";
        text += std::to_string(static_cast<int>(binding.role)) + "/" + std::to_string(binding.binding) + "x" + std::to_string(binding.count);
    }
    return text;
}

std::uint32_t TableElements(const Scene& scene) {
    for (const auto& binding : scene.pixel.bindings) {
        if (binding.role == ShaderRecompiler::DescriptorRole::ImageTable) return binding.count;
    }
    return 0;
}

std::uint32_t DirectImages(const Scene& scene) {
    std::uint32_t images = 0;
    for (const auto& binding : scene.pixel.bindings) {
        if (binding.role == ShaderRecompiler::DescriptorRole::GuestImages && binding.kind == ShaderRecompiler::DescriptorKind::SampledImage) images = std::max(images, binding.count);
    }
    return images;
}

const AgcDriver::Pm4::DrawParameters TriangleDraw{0, 3, 0, 1, 0, false};

unsigned Brightest() {
    AgcDriver::GuestMemory::FlushGpuWrites(reinterpret_cast<std::uintptr_t>(Target->base), TargetBytes);
    unsigned brightest = 0;
    for (std::size_t pixel = 0; pixel < TargetBytes; pixel += 4) brightest = std::max(brightest, static_cast<unsigned>(Target->base[pixel]));
    return brightest;
}

void Check(AgcDriver::VulkanDevice& device, std::uint32_t images, bool direct) {
    const std::uint32_t key = images / 2u;
    Scene scene(images, direct);
    Build(device, scene, key);
    const auto what = std::string(direct ? "direct" : "table") + " N=" + std::to_string(images);
    if (direct) Require(DirectImages(scene) == images, what + ": expected " + std::to_string(images) + " direct sampled images, got bindings " + Roles(scene));
    else Require(TableElements(scene) == images, what + ": expected an image table of " + std::to_string(images) + " elements, got bindings " + Roles(scene));
    for (std::uint32_t round = 0; round < 3; ++round) {
        device.Draw(scene.state, TriangleDraw, scene.shaders);
        device.WaitIdle();
        Require(Brightest() == 255u, what + ": round " + std::to_string(round) + " did not sample the keyed image (brightest " + std::to_string(Brightest()) + ")");
        std::fill_n(scene.Texels(key), TexelWidth * TexelHeight, round % 2u == 0u ? 0.0f : 1.0f);
        device.Draw(scene.state, TriangleDraw, scene.shaders);
        device.WaitIdle();
        const unsigned expected = round % 2u == 0u ? 0u : 255u;
        Require(Brightest() == expected, what + ": round " + std::to_string(round) + " missed the keyed image's new texels (brightest " + std::to_string(Brightest()) + ", expected " + std::to_string(expected) + ")");
        std::fill_n(scene.Texels(key), TexelWidth * TexelHeight, 1.0f);
    }
}

double Median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2u;
    return values.size() % 2u != 0u ? values[middle] : (values[middle - 1u] + values[middle]) / 2.0;
}

struct Column {
    const char* name;
    std::size_t slot;
};

void Bench(AgcDriver::VulkanDevice& device, std::uint32_t images, std::uint32_t draws, std::uint32_t windows, bool changed, bool direct) {
    Scene scene(images, direct);
    Build(device, scene, images / 2u);
    const std::uint32_t changedImage = images > 1u ? images - 1u : 0u;
    const std::array columns{
        Column{"lookup", ExpProfile::DrawPhaseBase + 5}, Column{"resources", ExpProfile::DrawPhaseBase + 6}, Column{"pipeline", ExpProfile::DrawPhaseBase + 7}, Column{"record", ExpProfile::DrawPhaseBase + 8},
        Column{"images", ExpProfile::BuildImages}, Column{"alloc", ExpProfile::BuildDescriptorsA}, Column{"writes", ExpProfile::BuildDescriptorsB}, Column{"precollect", ExpProfile::BuildPrecollect},
        Column{"upload", ExpProfile::BuildUpload}, Column{"bda", ExpProfile::BuildBda}, Column{"bindings", ExpProfile::BuildBindings}, Column{"reuseCheck", ExpProfile::TableReuseCheck},
        Column{"prepare", ExpProfile::PrepareBindings}, Column{"bind", ExpProfile::BindSets}, Column{"skipped", ExpProfile::HeapIndexWrites}, Column{"slotReads", ExpProfile::HeapSlotLookups},
        Column{"builds", ExpProfile::Builds}, Column{"reused", ExpProfile::TablesReused}, Column{"resolved", ExpProfile::TablesResolved}, Column{"slots", ExpProfile::HeapSlotsAssigned},
    };
    std::vector<std::vector<double>> samples(columns.size() + 2u);
    const auto mode = std::string(direct ? "direct" : "table") + " N=" + std::to_string(images) + " M=" + std::to_string(draws) + (changed ? " changed" : " unchanged");
    for (std::uint32_t window = 0; window <= windows; ++window) {
        device.WaitIdle();
        ExpProfile::Take();
        double drawUs = 0;
        const auto windowStart = std::chrono::steady_clock::now();
        for (std::uint32_t draw = 0; draw < draws; ++draw) {
            if (changed) scene.Texels(changedImage)[0] = static_cast<float>((window * draws + draw) % 2u);
            const auto start = std::chrono::steady_clock::now();
            device.Draw(scene.state, TriangleDraw, scene.shaders);
            drawUs += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
        }
        device.WaitIdle();
        const auto windowUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - windowStart).count();
        const auto totals = ExpProfile::Take();
        if (window == 0) continue;
        const double perDraw = 1.0 / draws;
        std::printf("[heap-ceiling] %s window %u: draw=%.1f wall=%.1f", mode.c_str(), window, drawUs * perDraw, windowUs * perDraw);
        samples[0].push_back(drawUs * perDraw);
        samples[1].push_back(windowUs * perDraw);
        for (std::size_t column = 0; column < columns.size(); ++column) {
            const auto value = totals[columns[column].slot] * perDraw;
            samples[column + 2u].push_back(value);
            std::printf(" %s=%.2f", columns[column].name, value);
        }
        std::printf(" draws=%.0f\n", totals[ExpProfile::Draws]);
    }
    std::printf("[heap-ceiling] %s median of %u: draw=%.1f wall=%.1f", mode.c_str(), windows, Median(samples[0]), Median(samples[1]));
    for (std::size_t column = 0; column < columns.size(); ++column) std::printf(" %s=%.2f", columns[column].name, Median(samples[column + 2u]));
    std::printf("\n");
}

}

int main(int argc, char** argv) {
    try {
        const std::vector<std::string> args(argv + 1, argv + argc);
        const bool bench = !args.empty() && args[0] == "--bench";
        if (bench) {
#ifdef _WIN32
            _putenv_s("APS5_PROFILE_DRAW", "1");
#else
            setenv("APS5_PROFILE_DRAW", "1", 1);
#endif
        }
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        TargetBytes = AgcDriver::Graphics::ColorTargetLayout(TargetWidth, TargetHeight, AgcDriver::Graphics::ColorTileMode::RenderTarget, 4).Bytes();
        GuestBlocks target((TargetBytes + BlockBytes - 1) / BlockBytes * BlockBytes);
        Target = &target;
        if (bench) {
            Require(args.size() >= 4, "usage: --bench IMAGES DRAWS WINDOWS [changed] [direct]");
            const auto images = static_cast<std::uint32_t>(std::stoul(args[1]));
            const auto draws = static_cast<std::uint32_t>(std::stoul(args[2]));
            const auto windows = static_cast<std::uint32_t>(std::stoul(args[3]));
            const bool changed = std::find(args.begin(), args.end(), "changed") != args.end();
            const bool direct = std::find(args.begin(), args.end(), "direct") != args.end();
            Bench(*device, images, draws, windows, changed, direct);
            return 0;
        }
        Check(*device, 8, false);
        Check(*device, 128, false);
        Check(*device, 8, true);
        std::puts("descriptor heap ceiling checks passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
