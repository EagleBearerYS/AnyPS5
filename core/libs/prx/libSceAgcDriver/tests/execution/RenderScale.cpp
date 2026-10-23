#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Shaders/ShaderRegistry.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ColorTargetLayout.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "prx/libSceAgcDriver/Graphics/include/RenderScale.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ShaderInputState.hpp"
#include "prx/libSceAgcDriver/Graphics/include/State.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Texture.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libc/include/GuestWriteWatch.hpp"
#include "Recompiler.hpp"
#include "Optimization/ResourceProgram.hpp"
#include "VulkanTestDevice.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using AgcDriver::Graphics::ColorTargetLayout;
using AgcDriver::Graphics::ColorTileMode;
using AgcDriver::Graphics::RenderScale;
using AgcDriver::Graphics::RenderScaleDemotion;
using AgcDriver::Graphics::Require;
using AgcDriver::Graphics::StorageTexture;
using ShaderRecompiler::ShaderStage;

constexpr std::size_t BlockBytes = 65536;
constexpr std::uint32_t Rgba8Info = 0x8028u;
constexpr std::uint32_t TiledAttrib3 = 0x4dc6c000u;
constexpr std::uint32_t Float32Export = 9;

alignas(256) constexpr std::array<std::uint32_t, 6> VertexCode{
    0xe0382000, 0x80000005, 0xbf8c3f70, 0xf80008cf, 0x03020100, 0xbf810000,
};

using Vertices = std::array<std::array<float, 4>, 3>;

constexpr Vertices FullTriangle{{
    {-1.0f, -1.0f, 0.5f, 1.0f}, {3.0f, -1.0f, 0.5f, 1.0f}, {-1.0f, 3.0f, 0.5f, 1.0f}
}};

constexpr Vertices SlantedTriangle{{
    {-0.5f, -0.7f, 0.5f, 1.0f}, {0.8f, -0.2f, 0.5f, 1.0f}, {0.1f, 0.9f, 0.5f, 1.0f}
}};

using Color = std::array<float, 4>;

constexpr Color ColorA{0.25f, 0.5f, 0.75f, 1.0f};
constexpr Color ColorB{1.0f, 0.0f, 0.0f, 1.0f};
constexpr Color ColorC{0.0f, 1.0f, 0.0f, 1.0f};

struct alignas(256) ProgramCode {
    std::array<std::uint32_t, 64> words{};
};

std::deque<ProgramCode> programs;

std::span<const std::uint32_t> Program(const Color& color) {
    auto& code = programs.emplace_back().words;
    std::size_t at = 0;
    for (std::uint32_t channel = 0; channel < 4; ++channel) {
        code[at++] = 0x7e0002ffu | ((4u + channel) << 17u);
        code[at++] = std::bit_cast<std::uint32_t>(color[channel]);
    }
    code[at++] = 0xf800180fu;
    code[at++] = 0x07060504u;
    code[at++] = 0xbf810000u;
    return std::span<const std::uint32_t>(code.data(), at);
}

std::array<std::uint8_t, 4> Texel(const Color& color) {
    std::array<std::uint8_t, 4> texel{};
    for (std::size_t i = 0; i < 4; ++i) texel[i] = static_cast<std::uint8_t>(std::lround(std::fmin(std::fmax(static_cast<double>(color[i]), 0.0), 1.0) * 255.0));
    return texel;
}

struct Block {
    explicit Block(std::size_t bytes) : bytes(bytes), watched(AgcDriver::GuestMemory::WriteWatched()) {
#ifdef _WIN32
        if (watched) {
            data = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(bytes, BlockBytes));
            GuestArena::GuestArenaCommit_nid_postfix(data, bytes, PAGE_READWRITE, bytes);
        } else {
            data = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        }
#else
        if (watched) {
            void* raw = mmap(nullptr, bytes + BlockBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            Require(raw != MAP_FAILED, "cannot map the color target");
            const auto begin = reinterpret_cast<std::uintptr_t>(raw);
            const auto aligned = (begin + BlockBytes - 1) & ~(static_cast<std::uintptr_t>(BlockBytes) - 1);
            if (aligned != begin) munmap(raw, aligned - begin);
            if (aligned + bytes != begin + bytes + BlockBytes) munmap(reinterpret_cast<void*>(aligned + bytes), begin + BlockBytes - aligned);
            data = reinterpret_cast<std::uint8_t*>(aligned);
            GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(data, bytes);
        } else {
            data = static_cast<std::uint8_t*>(std::aligned_alloc(BlockBytes, (bytes + BlockBytes - 1) / BlockBytes * BlockBytes));
        }
#endif
        Require(data != nullptr, "cannot allocate the color target");
        std::memset(data, 0, bytes);
        GuestAllocations::Mutation mutation;
        mutation.Add(data, bytes, true, true, true);
    }
    ~Block() {
        StorageTexture::FlushPending(Address(), bytes, nullptr, "test release");
        {
            GuestAllocations::Mutation mutation;
            mutation.Remove(data);
        }
#ifdef _WIN32
        if (watched) {
            GuestArena::GuestArenaReset_nid_postfix(data, bytes);
            GuestArena::GuestArenaRelease_nid_postfix(data, bytes);
        } else {
            VirtualFree(data, 0, MEM_RELEASE);
        }
#else
        if (watched) {
            munmap(data, bytes);
            GuestWriteWatch::GuestWriteWatchUnregister_nid_postfix(data, bytes);
        } else {
            std::free(data);
        }
#endif
    }
    Block(const Block&) = delete;
    Block& operator=(const Block&) = delete;
    std::uint64_t Address() const { return reinterpret_cast<std::uintptr_t>(data); }
    std::size_t bytes;
    bool watched;
    std::uint8_t* data = nullptr;
};

struct Surface {
    Surface(std::uint32_t width, std::uint32_t height) : width(width), height(height), layout(width, height, ColorTileMode::RenderTarget, 4), block(layout.Bytes()) {}
    AgcDriver::Graphics::ColorTarget Decode() const {
        AgcDriver::Registers cx;
        cx[0x318] = static_cast<std::uint32_t>(block.Address() >> 8u);
        cx[0x31b] = 0;
        cx[0x31c] = Rgba8Info;
        cx[0x31d] = 0;
        cx[0x3b0] = ((width - 1u) << 14u) | (height - 1u);
        cx[0x3b8] = TiledAttrib3;
        cx[0x390] = static_cast<std::uint32_t>(block.Address() >> 40u);
        auto color = AgcDriver::Graphics::DecodeColorBuffer(cx, 0);
        Require(color.format == VK_FORMAT_R8G8B8A8_UNORM && color.elementBytes == 4 && color.bytes == block.bytes, "the 8_8_8_8 unorm target did not decode as an R8G8B8A8 surface");
        return color;
    }
    std::shared_ptr<StorageTexture> Image() const {
        auto image = StorageTexture::FindLive(block.Address(), block.bytes);
        Require(image != nullptr, "the target has no resident image");
        return image;
    }
    std::uint32_t width;
    std::uint32_t height;
    ColorTargetLayout layout;
    Block block;
};

struct Pass {
    std::vector<AgcDriver::Graphics::ColorTarget> colors;
    VkExtent2D extent;
    VkRect2D scissor;
    const Vertices* vertices;
    Color color;
};

void Draw(AgcDriver::VulkanDevice& device, const Pass& pass) {
    const auto target = device.Target();
    constexpr std::uint32_t waveSize = 64;
    std::vector<std::uint32_t> vertexUserData(4, 0u);
    const auto address = reinterpret_cast<std::uintptr_t>(pass.vertices->data());
    const std::array<std::uint32_t, 4> vertexBuffer{static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (16u << 16u), static_cast<std::uint32_t>(pass.vertices->size()), 0x01016facu};
    std::copy(vertexBuffer.begin(), vertexBuffer.end(), vertexUserData.begin());
    const std::array<ShaderRecompiler::MemoryRegion, 1> vertexMemory{{{reinterpret_cast<std::uintptr_t>(VertexCode.data()), std::as_bytes(std::span(VertexCode))}}};
    ShaderRecompiler::RecompileRequest vertex{
        {ShaderStage::Vertex, reinterpret_cast<std::uintptr_t>(VertexCode.data()), VertexCode, 0, {}},
        {waveSize, 0, vertexUserData, std::nullopt, std::nullopt, ShaderRecompiler::ShaderVertexStageInfo{}, vertexMemory},
        target,
        {0, 0, 0, 64}
    };
    vertex.useCache = false;
    const auto vertexResult = ShaderRecompiler::Recompile(vertex);
    const auto vertexPush = static_cast<std::uint32_t>(vertexResult.pushConstants.size());

    AgcDriver::Graphics::State state{};
    state.stages = {AgcDriver::Graphics::ShaderPath::Vertex, 0u, waveSize, waveSize, std::nullopt, std::nullopt};
    state.colors = pass.colors;
    for (std::size_t index = 0; index < state.colors.size(); ++index) state.colors[index].exportIndex = static_cast<std::uint32_t>(index);
    state.color = state.colors.front();
    state.hasColorTarget = true;
    state.renderExtent = pass.extent;
    state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    state.viewport = {0, static_cast<float>(pass.extent.height), static_cast<float>(pass.extent.width), -static_cast<float>(pass.extent.height), 0, 1};
    state.negativeOneToOne = false;
    state.scissor = pass.scissor;
    state.cullMode = VK_CULL_MODE_NONE;
    state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = 0xf;
    state.blends.assign(state.colors.size(), blend);
    state.blend = blend;
    state.blendConstants = {};

    const auto pixelCode = Program(pass.color);
    const auto codeAddress = reinterpret_cast<std::uintptr_t>(pixelCode.data());
    const AgcDriver::Registers context{{0x1b6, 0x8000}, {0x1b3, 0}, {0x1b4, 0}, {0x203, 0}, {0x1c5, Float32Export}};
    const std::array<ShaderRecompiler::MemoryRegion, 1> pixelMemory{{{codeAddress, std::as_bytes(pixelCode)}}};
    ShaderRecompiler::RecompileRequest fragment{
        {ShaderStage::Fragment, codeAddress, pixelCode, 0, {}},
        {waveSize, 0, {}, std::nullopt, AgcDriver::Graphics::DecodePixelStageInfo(context, {}), std::nullopt, pixelMemory},
        target,
        PixelPushLayout(vertexPush, target)
    };
    const auto handle = ShaderRecompiler::PrepareShader(fragment);
    AgcDriver::DriverDetail::ShaderSnapshot snapshot{codeAddress, 0, 1, {pixelCode.begin(), pixelCode.end()}, {}};
    snapshot.prepared->entries.push_back({0, handle});
    fragment.shader.code = snapshot.code;
    fragment.context.pixel = AgcDriver::Graphics::DecodePixelStageInfo(context, AgcDriver::Graphics::ExportMappings(state));
    const ShaderRecompiler::SrtRuntime runtime{};
    const auto invocation = AgcDriver::DriverDetail::InvocationFor(snapshot, 0, fragment);
    const auto pixelResult = invocation.Materialize(*invocation.Capture(runtime));
    const std::array<AgcDriver::Graphics::CompiledShader, 2> shaders{{
        {ShaderStage::Vertex, &vertexResult, 0},
        {ShaderStage::Fragment, pixelResult.get(), PixelPushOffset(vertexPush, target)}
    }};
    const AgcDriver::Pm4::DrawParameters draw{0, static_cast<std::uint32_t>(pass.vertices->size()), 0, 1, 0, false};
    device.Draw(state, draw, shaders);
}

VkRect2D ScaledRect(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height, std::uint32_t numerator, std::uint32_t denominator) {
    return {{static_cast<std::int32_t>(x * numerator / denominator), static_cast<std::int32_t>(y * numerator / denominator)}, {width * numerator / denominator, height * numerator / denominator}};
}

void Stream(AgcDriver::VulkanDevice& device, const Surface& surface, std::uint32_t numerator, std::uint32_t denominator) {
    const auto color = surface.Decode();
    const VkExtent2D extent{surface.width, surface.height};
    const VkRect2D full{{0, 0}, extent};
    Draw(device, {{color}, extent, full, &FullTriangle, ColorA});
    Draw(device, {{color}, extent, ScaledRect(1000, 500, 1600, 800, numerator, denominator), &FullTriangle, ColorB});
    Draw(device, {{color}, extent, full, &SlantedTriangle, ColorC});
}

std::vector<std::byte> GuestTexels(AgcDriver::VulkanDevice& device, const Surface& surface) {
    StorageTexture::FlushPending(surface.block.Address(), surface.block.bytes, nullptr, "test read-back");
    device.WaitIdle();
    std::vector<std::byte> stored(surface.block.bytes);
    AgcDriver::GuestMemory::Read(surface.block.Address(), stored, 1);
    return stored;
}

void ExpectGuestTexel(const Surface& surface, std::span<const std::byte> stored, std::uint32_t x, std::uint32_t y, const Color& color, const std::string& what) {
    const auto offset = surface.layout.Offset(x, y);
    const auto expected = Texel(color);
    for (std::size_t i = 0; i < 4; ++i) {
        if (std::to_integer<std::uint8_t>(stored[offset + i]) == expected[i]) continue;
        char detail[192];
        std::snprintf(detail, sizeof(detail), ": pixel (%u, %u) holds %02x%02x%02x%02x, expected %02x%02x%02x%02x", x, y, std::to_integer<unsigned>(stored[offset]), std::to_integer<unsigned>(stored[offset + 1]), std::to_integer<unsigned>(stored[offset + 2]), std::to_integer<unsigned>(stored[offset + 3]), expected[0], expected[1], expected[2], expected[3]);
        throw std::runtime_error(what + detail);
    }
}

void ExpectImageTexel(std::span<const std::byte> texels, std::uint32_t width, std::uint32_t x, std::uint32_t y, const Color& color, const std::string& what) {
    const auto offset = (static_cast<std::size_t>(y) * width + x) * 4u;
    const auto expected = Texel(color);
    for (std::size_t i = 0; i < 4; ++i) {
        if (std::to_integer<std::uint8_t>(texels[offset + i]) == expected[i]) continue;
        char detail[160];
        std::snprintf(detail, sizeof(detail), ": image texel (%u, %u) channel %zu holds %u, expected %u", x, y, i, std::to_integer<unsigned>(texels[offset + i]), static_cast<unsigned>(expected[i]));
        throw std::runtime_error(what + detail);
    }
}

std::uint64_t Fnv(std::span<const std::byte> bytes) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto byte : bytes) {
        hash ^= std::to_integer<std::uint8_t>(byte);
        hash *= 1099511628211ull;
    }
    return hash;
}

void ExpectDemotions(const std::array<std::uint64_t, AgcDriver::Graphics::RenderScaleDemotionCount>& expected, const std::string& what) {
    const auto counts = AgcDriver::Graphics::RenderScaleDemotions();
    for (std::size_t i = 0; i < counts.size(); ++i) {
        if (counts[i] == expected[i]) continue;
        throw std::runtime_error(what + ": " + AgcDriver::Graphics::RenderScaleDemotionName(static_cast<RenderScaleDemotion>(i)) + " fired " + std::to_string(counts[i]) + " times, expected " + std::to_string(expected[i]));
    }
}

std::array<std::uint64_t, AgcDriver::Graphics::RenderScaleDemotionCount> Only(RenderScaleDemotion reason) {
    std::array<std::uint64_t, AgcDriver::Graphics::RenderScaleDemotionCount> counts{};
    counts[static_cast<std::size_t>(reason)] = 1;
    return counts;
}

void ExpectExtent(const StorageTexture& image, VkExtent2D extent, bool scaled, const std::string& what) {
    const auto actual = image.ImageExtent();
    if (actual.width == extent.width && actual.height == extent.height && image.RenderScaled() == scaled) return;
    throw std::runtime_error(what + ": the image is " + std::to_string(actual.width) + "x" + std::to_string(actual.height) + (image.RenderScaled() ? " (scaled)" : " (native)") + ", expected " + std::to_string(extent.width) + "x" + std::to_string(extent.height));
}

void ParseTests() {
    using AgcDriver::Graphics::ParseRenderScale;
    const auto expect = [](std::string_view text, std::optional<RenderScale> expected) {
        const auto parsed = ParseRenderScale(text);
        Require(parsed == expected, "APS5_RENDER_SCALE=" + std::string(text) + " parsed to " + (parsed ? std::to_string(parsed->numerator) + "/" + std::to_string(parsed->denominator) : std::string("nothing")));
    };
    expect("0.5", RenderScale{1, 2});
    expect("0.75", RenderScale{3, 4});
    expect("2/3", RenderScale{2, 3});
    expect("4/8", RenderScale{1, 2});
    expect("1", RenderScale{1, 1});
    expect("1/1", RenderScale{1, 1});
    for (const auto bad : {"", "0", "0.0", "1.5", "3/2", "0/4", "1/0", "abc", "0.5x", "-1/2", "1/65", "0.12345"}) expect(bad, std::nullopt);
    using AgcDriver::Graphics::ScaleExtent;
    using AgcDriver::Graphics::ScaleScissor;
    const auto half = ScaleExtent({3840, 2160}, {1, 2});
    Require(half.width == 1920 && half.height == 1080, "half of 3840x2160 is not 1920x1080");
    const auto threeQuarters = ScaleExtent({3840, 2160}, {3, 4});
    Require(threeQuarters.width == 2880 && threeQuarters.height == 1620, "three quarters of 3840x2160 is not 2880x1620");
    const auto scissor = ScaleScissor({{1001, 501}, {1599, 799}}, {1, 2}, {1920, 1080});
    Require(scissor.offset.x == 500 && scissor.offset.y == 250 && scissor.extent.width == 800 && scissor.extent.height == 400, "a scaled scissor does not cover the pixels its native rectangle touches");
    const auto clamped = ScaleScissor({{0, 0}, {16384, 16384}}, {1, 2}, {1920, 1080});
    Require(clamped.extent.width == 1920 && clamped.extent.height == 1080, "a scaled scissor exceeds the scaled render extent");
    const std::array<std::uint32_t, 9> fragCoord{0x07230203u, 0x00010000u, 0, 2, 0, (4u << 16u) | 71u, 1, 11, 15};
    const std::array<std::uint32_t, 9> position{0x07230203u, 0x00010000u, 0, 2, 0, (4u << 16u) | 71u, 1, 11, 0};
    Require(AgcDriver::Graphics::ReadsFragmentPosition(fragCoord), "a FragCoord decoration is not seen");
    Require(!AgcDriver::Graphics::ReadsFragmentPosition(position), "a Position decoration reads as FragCoord");
    std::puts("render scale parse tests passed");
}

void CompareTests(AgcDriver::VulkanDevice& device) {
    Require(AgcDriver::Graphics::ActiveRenderScale() == RenderScale{1, 2}, "the compare test runs with APS5_RENDER_SCALE=0.5");
    AgcDriver::Graphics::NoteVideoOutExtent(3840, 2160);
    Surface big(3840, 2160);
    Surface small(1920, 1080);
    Stream(device, big, 1, 1);
    Stream(device, small, 1, 2);
    device.WaitIdle();
    const auto bigImage = big.Image();
    const auto smallImage = small.Image();
    ExpectExtent(*bigImage, {1920, 1080}, true, "the 3840x2160 target at scale 0.5");
    ExpectExtent(*smallImage, {1920, 1080}, false, "the 1920x1080 target below the video-out extent");
    ExpectDemotions({}, "the scaled stream");
    Require(AgcDriver::Graphics::RenderScaledSurfaces() == 1, "one surface scales");
    const auto scaled = bigImage->ReadImageTexels();
    const auto native = smallImage->ReadImageTexels();
    Require(scaled.size() == native.size(), "the scaled and native images differ in size");
    std::size_t differing = 0;
    for (std::size_t i = 0; i < scaled.size(); ++i) differing += scaled[i] != native[i];
    if (differing != 0) throw std::runtime_error("the 3840x2160 stream at scale 0.5 differs from the native 1920x1080 stream in " + std::to_string(differing) + " bytes");
    ExpectImageTexel(scaled, 1920, 50, 50, ColorA, "the scaled image");
    ExpectImageTexel(scaled, 1920, 550, 625, ColorB, "the scaled image");
    ExpectImageTexel(scaled, 1920, 1088, 540, ColorC, "the scaled image");
    std::printf("scaled 3840x2160 at 0.5 equals native 1920x1080: %zu bytes, fnv %016llx\n", scaled.size(), static_cast<unsigned long long>(Fnv(scaled)));
    const auto stored = GuestTexels(device, big);
    ExpectExtent(*bigImage, {3840, 2160}, false, "the target after a CPU read-back");
    ExpectDemotions(Only(RenderScaleDemotion::WriteBack), "a CPU read-back of the scaled target");
    ExpectGuestTexel(big, stored, 100, 100, ColorA, "the demoted target's guest bytes");
    ExpectGuestTexel(big, stored, 1100, 1250, ColorB, "the demoted target's guest bytes");
    ExpectGuestTexel(big, stored, 2176, 1080, ColorC, "the demoted target's guest bytes");
    Draw(device, {{big.Decode()}, {3840, 2160}, {{0, 0}, {3840, 2160}}, &FullTriangle, ColorB});
    device.WaitIdle();
    ExpectExtent(*bigImage, {3840, 2160}, false, "a demoted target drawn again");
    Require(AgcDriver::Graphics::RenderScaledSurfaces() == 1, "a demoted surface scales again");
    std::puts("render scale compare tests passed");
}

void UnsetTests(AgcDriver::VulkanDevice& device) {
    Require(!AgcDriver::Graphics::ActiveRenderScale().has_value(), "the unset test runs without APS5_RENDER_SCALE");
    AgcDriver::Graphics::NoteVideoOutExtent(3840, 2160);
    Surface big(3840, 2160);
    Stream(device, big, 1, 1);
    device.WaitIdle();
    const auto image = big.Image();
    ExpectExtent(*image, {3840, 2160}, false, "the 3840x2160 target without APS5_RENDER_SCALE");
    const auto texels = image->ReadImageTexels();
    Require(texels.size() == std::size_t{3840} * 2160 * 4, "the native image is not 3840x2160");
    ExpectImageTexel(texels, 3840, 100, 100, ColorA, "the native image");
    ExpectImageTexel(texels, 3840, 1100, 1250, ColorB, "the native image");
    ExpectImageTexel(texels, 3840, 2176, 1080, ColorC, "the native image");
    const auto stored = GuestTexels(device, big);
    ExpectDemotions({}, "the unset stream");
    Require(AgcDriver::Graphics::RenderScaledSurfaces() == 0, "a surface scaled without APS5_RENDER_SCALE");
    ExpectGuestTexel(big, stored, 100, 100, ColorA, "the guest bytes");
    ExpectGuestTexel(big, stored, 1100, 1250, ColorB, "the guest bytes");
    ExpectGuestTexel(big, stored, 2176, 1080, ColorC, "the guest bytes");
    std::printf("unset 3840x2160 stream: image fnv %016llx, guest fnv %016llx\n", static_cast<unsigned long long>(Fnv(texels)), static_cast<unsigned long long>(Fnv(stored)));
    std::puts("render scale unset tests passed");
}

void MixedTests(AgcDriver::VulkanDevice& device, bool scaled) {
    AgcDriver::Graphics::NoteVideoOutExtent(256, 128);
    Surface target(256, 128);
    Surface other(128, 64);
    Draw(device, {{target.Decode()}, {256, 128}, {{0, 0}, {256, 128}}, &FullTriangle, ColorA});
    device.WaitIdle();
    const auto image = target.Image();
    ExpectExtent(*image, scaled ? VkExtent2D{128, 64} : VkExtent2D{256, 128}, scaled, "the 256x128 target");
    Draw(device, {{target.Decode(), other.Decode()}, {128, 64}, {{0, 0}, {128, 64}}, &FullTriangle, ColorB});
    device.WaitIdle();
    ExpectExtent(*image, {256, 128}, false, "the target drawn with an unscaled color target");
    ExpectDemotions(scaled ? Only(RenderScaleDemotion::MixedAttachment) : std::array<std::uint64_t, AgcDriver::Graphics::RenderScaleDemotionCount>{}, "a draw with an unscaled color target");
    const auto stored = GuestTexels(device, target);
    ExpectGuestTexel(target, stored, 10, 10, ColorB, "the demoted target");
    ExpectGuestTexel(target, stored, 200, 100, ColorA, "the demoted target");
    std::puts("render scale mixed attachment tests passed");
}

void StorageTests(AgcDriver::VulkanDevice& device) {
    AgcDriver::Graphics::NoteVideoOutExtent(256, 128);
    Surface target(256, 128);
    Draw(device, {{target.Decode()}, {256, 128}, {{0, 0}, {256, 128}}, &FullTriangle, ColorA});
    device.WaitIdle();
    const auto image = target.Image();
    ExpectExtent(*image, {128, 64}, true, "the 256x128 target at scale 0.5");
    static_cast<void>(image->StorageView(0, false));
    ExpectExtent(*image, {256, 128}, false, "the target bound as a storage image");
    ExpectDemotions(Only(RenderScaleDemotion::StorageAccess), "a storage view of the scaled target");
    device.WaitIdle();
    const auto texels = image->ReadImageTexels();
    ExpectImageTexel(texels, 256, 255, 127, ColorA, "the demoted image");
    std::puts("render scale storage tests passed");
}

}

int main(int argc, char** argv) {
    try {
        const std::string mode = argc > 1 ? argv[1] : "--parse";
        if (mode == "--parse") {
            ParseTests();
            return 0;
        }
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        if (mode == "--compare") CompareTests(*device);
        else if (mode == "--unset") UnsetTests(*device);
        else if (mode == "--mixed") MixedTests(*device, true);
        else if (mode == "--mixed-native") MixedTests(*device, false);
        else if (mode == "--storage") StorageTests(*device);
        else throw std::runtime_error("unknown mode " + mode);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
