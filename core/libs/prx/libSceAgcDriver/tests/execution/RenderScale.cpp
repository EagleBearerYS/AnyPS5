#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Shaders/ShaderRegistry.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ColorTargetLayout.hpp"
#include "prx/libSceAgcDriver/Graphics/include/DepthSurface.hpp"
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
#include <initializer_list>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using AgcDriver::Graphics::ColorTargetLayout;
using AgcDriver::Graphics::ColorTileMode;
using AgcDriver::Graphics::DepthTarget;
using AgcDriver::Graphics::GuestTextureResource;
using AgcDriver::Graphics::TextureDimension;
using AgcDriver::Graphics::TextureTileMode;
using AgcDriver::Graphics::RenderScale;
using AgcDriver::Graphics::RenderScaleDemotion;
using AgcDriver::Graphics::Require;
using AgcDriver::Graphics::StorageTexture;
using ShaderRecompiler::ShaderStage;

constexpr std::size_t BlockBytes = 65536;
constexpr std::uint32_t Rgba8Info = 0x8028u;
constexpr std::uint32_t R32FloatInfo = 0x8710u;
constexpr std::uint32_t Format32Float = 22;
constexpr std::uint32_t Format8Unorm = 1;
constexpr std::uint32_t PositionInputs = 0x300u;
constexpr std::uint32_t TiledAttrib3 = 0x4dc6c000u;
constexpr std::uint32_t Float32Export = 9;

alignas(256) constexpr std::array<std::uint32_t, 6> VertexCode{
    0xe0382000, 0x80000005, 0xbf8c3f70, 0xf80008cf, 0x03020100, 0xbf810000,
};

alignas(256) constexpr std::array<std::uint32_t, 3> NullExportCode{0xf8001890u, 0x00000000u, 0xbf810000u};

using Vertices = std::array<std::array<float, 4>, 3>;

constexpr Vertices FullTriangle{{
    {-1.0f, -1.0f, 0.5f, 1.0f}, {3.0f, -1.0f, 0.5f, 1.0f}, {-1.0f, 3.0f, 0.5f, 1.0f}
}};

constexpr Vertices SlantedTriangle{{
    {-0.5f, -0.7f, 0.5f, 1.0f}, {0.8f, -0.2f, 0.5f, 1.0f}, {0.1f, 0.9f, 0.5f, 1.0f}
}};

constexpr Vertices FarTriangle{{
    {-1.0f, -1.0f, 0.75f, 1.0f}, {3.0f, -1.0f, 0.75f, 1.0f}, {-1.0f, 3.0f, 0.75f, 1.0f}
}};

constexpr Vertices NearSlantedTriangle{{
    {-0.5f, -0.7f, 0.2f, 1.0f}, {0.8f, -0.2f, 0.45f, 1.0f}, {0.1f, 0.9f, 0.3f, 1.0f}
}};

constexpr Vertices CrossingTriangle{{
    {0.2f, -0.9f, 0.35f, 1.0f}, {0.95f, -0.9f, 0.6f, 1.0f}, {0.95f, 0.9f, 0.6f, 1.0f}
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

std::span<const std::uint32_t> PositionProgram(VkExtent2D extent) {
    auto& code = programs.emplace_back().words;
    const std::array<std::uint32_t, 10> words{
        0x100800ffu, std::bit_cast<std::uint32_t>(1.0f / static_cast<float>(extent.width)),
        0x100a02ffu, std::bit_cast<std::uint32_t>(1.0f / static_cast<float>(extent.height)),
        0xf800100fu, 0x04040404u,
        0xf800181fu, 0x05050505u,
        0xbf810000u, 0u,
    };
    std::copy(words.begin(), words.end(), code.begin());
    return std::span<const std::uint32_t>(code.data(), 9);
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
    Surface(std::uint32_t width, std::uint32_t height, std::uint32_t info = Rgba8Info) : width(width), height(height), info(info), layout(width, height, ColorTileMode::RenderTarget, 4), block(layout.Bytes()) {}
    AgcDriver::Graphics::ColorTarget Decode() const {
        AgcDriver::Registers cx;
        cx[0x318] = static_cast<std::uint32_t>(block.Address() >> 8u);
        cx[0x31b] = 0;
        cx[0x31c] = info;
        cx[0x31d] = 0;
        cx[0x3b0] = ((width - 1u) << 14u) | (height - 1u);
        cx[0x3b8] = TiledAttrib3;
        cx[0x390] = static_cast<std::uint32_t>(block.Address() >> 40u);
        auto color = AgcDriver::Graphics::DecodeColorBuffer(cx, 0);
        const auto expected = info == R32FloatInfo ? VK_FORMAT_R32_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
        Require(color.format == expected && color.elementBytes == 4 && color.bytes == block.bytes, "the color target did not decode as its 32-bit surface format");
        return color;
    }
    std::shared_ptr<StorageTexture> Image() const {
        auto image = StorageTexture::FindLive(block.Address(), block.bytes);
        Require(image != nullptr, "the target has no resident image");
        return image;
    }
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t info;
    ColorTargetLayout layout;
    Block block;
};

struct DepthPlane {
    DepthPlane(std::uint32_t width, std::uint32_t height, std::uint32_t layers = 1, bool stencil = false) : width(width), height(height), stencil(stencil), block(static_cast<std::size_t>(AgcDriver::Graphics::DepthSliceBytes({width, height}, 4u)) * layers + (stencil ? static_cast<std::size_t>(AgcDriver::Graphics::DepthSliceBytes({width, height}, 1u)) + BlockBytes : 0u)) {
        auto* words = reinterpret_cast<std::uint32_t*>(block.data);
        for (std::size_t i = 0; i < block.bytes / 4u; ++i) words[i] = std::bit_cast<std::uint32_t>(1.0f);
    }
    std::uint64_t StencilAddress() const {
        if (!stencil) return 0;
        const auto depthBytes = AgcDriver::Graphics::DepthSliceBytes({width, height}, 4u);
        return block.Address() + (depthBytes + BlockBytes - 1u) / BlockBytes * BlockBytes;
    }
    DepthTarget Target() const {
        DepthTarget target{block.Address(), StencilAddress(), {width, height}, stencil ? VK_FORMAT_D32_SFLOAT_S8_UINT : VK_FORMAT_D32_SFLOAT, 1.0f, 0};
        target.depthSwizzle = AgcDriver::Graphics::XorSwizzleMode(TextureTileMode::kZ64KBX);
        return target;
    }
    GuestTextureResource View(std::uint32_t format, std::uint32_t lastLayer = 0) const {
        GuestTextureResource resource{};
        resource.baseAddress = format == Format8Unorm ? StencilAddress() : block.Address();
        resource.width = width;
        resource.height = height;
        resource.mipCount = 1;
        resource.allocatedMipCount = 1;
        resource.tileMode = TextureTileMode::kZ64KBX;
        resource.dimension = lastLayer != 0 ? TextureDimension::k2DArray : TextureDimension::k2D;
        resource.depthOrLastArray = lastLayer;
        resource.format = format;
        resource.dstSelX = 4;
        resource.dstSelY = 5;
        resource.dstSelZ = 6;
        resource.dstSelW = 7;
        return resource;
    }
    std::uint32_t width;
    std::uint32_t height;
    bool stencil;
    Block block;
};

struct DepthPass {
    DepthTarget target;
    VkCompareOp compare = VK_COMPARE_OP_LESS;
    bool write = true;
};

struct Pass {
    std::vector<AgcDriver::Graphics::ColorTarget> colors;
    VkExtent2D extent;
    VkRect2D scissor;
    const Vertices* vertices;
    Color color;
    std::optional<DepthPass> depth = std::nullopt;
    std::span<const std::uint32_t> program = {};
    std::uint32_t inputs = 0;
    std::uint32_t exports = Float32Export;
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
    if (!state.colors.empty()) state.color = state.colors.front();
    state.hasColorTarget = !state.colors.empty();
    if (pass.depth.has_value()) {
        state.depth = pass.depth->target;
        state.depthTest = true;
        state.depthWrite = pass.depth->write;
        state.depthCompare = pass.depth->compare;
    }
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

    const auto pixelCode = pass.program.empty() ? Program(pass.color) : pass.program;
    const auto codeAddress = reinterpret_cast<std::uintptr_t>(pixelCode.data());
    const AgcDriver::Registers context{{0x1b6, 0x8000}, {0x1b3, pass.inputs}, {0x1b4, pass.inputs}, {0x203, 0}, {0x1c5, pass.exports}};
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

void ExpectSameBytes(std::span<const std::byte> scaled, std::span<const std::byte> native, const std::string& what) {
    Require(scaled.size() == native.size(), what + ": the scaled and native images differ in size");
    std::size_t differing = 0;
    for (std::size_t i = 0; i < scaled.size(); ++i) differing += scaled[i] != native[i];
    if (differing != 0) throw std::runtime_error(what + ": the scaled stream differs from the native stream in " + std::to_string(differing) + " bytes");
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
    ExpectExtent(*bigImage, {1920, 1080}, true, "the target after a CPU read-back");
    ExpectDemotions({}, "a CPU read-back of the scaled target");
    Require(AgcDriver::Graphics::RenderScaledStores() == 1, "the read-back stores the scaled target once at its native size");
    ExpectGuestTexel(big, stored, 100, 100, ColorA, "the stored target's guest bytes");
    ExpectGuestTexel(big, stored, 1100, 1250, ColorB, "the stored target's guest bytes");
    ExpectGuestTexel(big, stored, 2176, 1080, ColorC, "the stored target's guest bytes");
    Draw(device, {{big.Decode()}, {3840, 2160}, ScaledRect(200, 100, 1200, 600, 1, 1), &FullTriangle, ColorC});
    Draw(device, {{small.Decode()}, {1920, 1080}, ScaledRect(200, 100, 1200, 600, 1, 2), &FullTriangle, ColorC});
    device.WaitIdle();
    ExpectExtent(*bigImage, {1920, 1080}, true, "the stored target drawn again");
    ExpectDemotions({}, "a draw into the stored target");
    Require(AgcDriver::Graphics::RenderScaledSurfaces() == 1, "the stored surface scaled once");
    ExpectSameBytes(bigImage->ReadImageTexels(), smallImage->ReadImageTexels(), "the stored target drawn again");
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

std::array<std::uint64_t, AgcDriver::Graphics::RenderScaleDemotionCount> Counts(std::initializer_list<RenderScaleDemotion> reasons) {
    std::array<std::uint64_t, AgcDriver::Graphics::RenderScaleDemotionCount> counts{};
    for (const auto reason : reasons) ++counts[static_cast<std::size_t>(reason)];
    return counts;
}

void DepthStream(AgcDriver::VulkanDevice& device, const Surface& surface, const DepthPlane& depth) {
    const auto color = surface.Decode();
    const VkExtent2D extent{surface.width, surface.height};
    const VkRect2D full{{0, 0}, extent};
    const DepthPass test{depth.Target()};
    Draw(device, {{color}, extent, full, &FullTriangle, ColorA, test});
    Draw(device, {{color}, extent, full, &NearSlantedTriangle, ColorC, test});
    Draw(device, {{color}, extent, full, &FarTriangle, ColorB, test});
    Draw(device, {{color}, extent, full, &CrossingTriangle, ColorB, test});
}

std::vector<std::byte> DepthTexels(AgcDriver::VulkanDevice& device, const DepthPlane& depth) {
    device.WaitIdle();
    return AgcDriver::Graphics::ReadDepthSurfaceTexels(device.graphicsContext(), depth.Target());
}

float FloatAt(std::span<const std::byte> texels, std::uint32_t width, std::uint32_t x, std::uint32_t y) {
    float value = 0;
    std::memcpy(&value, texels.data() + (static_cast<std::size_t>(y) * width + x) * sizeof(float), sizeof(value));
    return value;
}

void ExpectDepthExtent(AgcDriver::VulkanDevice& device, const DepthPlane& depth, VkExtent2D extent, bool scaled, const std::string& what) {
    const auto context = device.graphicsContext();
    const auto actual = AgcDriver::Graphics::DepthSurfaceImageExtent(context, depth.Target());
    const bool isScaled = AgcDriver::Graphics::DepthSurfaceRenderScaled(context, depth.Target());
    if (actual.width == extent.width && actual.height == extent.height && isScaled == scaled) return;
    throw std::runtime_error(what + ": the depth image is " + std::to_string(actual.width) + "x" + std::to_string(actual.height) + (isScaled ? " (scaled)" : " (native)") + ", expected " + std::to_string(extent.width) + "x" + std::to_string(extent.height));
}

std::size_t CountTexels(std::span<const std::byte> texels, const Color& color) {
    const auto expected = Texel(color);
    std::size_t count = 0;
    for (std::size_t offset = 0; offset + 4u <= texels.size(); offset += 4u) {
        bool same = true;
        for (std::size_t i = 0; i < 4; ++i) same = same && std::to_integer<std::uint8_t>(texels[offset + i]) == expected[i];
        count += same;
    }
    return count;
}

void RedrawCompare(AgcDriver::VulkanDevice& device, const Surface& big, const DepthPlane& bigDepth, const Surface& small, const DepthPlane& smallDepth, const std::string& what) {
    for (const auto& [surface, depth] : {std::pair{&big, &bigDepth}, std::pair{&small, &smallDepth}}) {
        const VkExtent2D extent{surface->width, surface->height};
        Draw(device, {{surface->Decode()}, extent, {{0, 0}, extent}, &CrossingTriangle, ColorC, DepthPass{depth->Target(), VK_COMPARE_OP_LESS_OR_EQUAL}});
    }
    device.WaitIdle();
    ExpectExtent(*big.Image(), {1920, 1080}, true, what + ", the color target drawn again");
    ExpectDepthExtent(device, bigDepth, {1920, 1080}, true, what + ", the depth surface drawn again");
    ExpectDemotions({}, what + ", the draw after the store");
    ExpectSameBytes(big.Image()->ReadImageTexels(), small.Image()->ReadImageTexels(), what + ", color");
    ExpectSameBytes(DepthTexels(device, bigDepth), DepthTexels(device, smallDepth), what + ", depth");
}

void DepthTests(AgcDriver::VulkanDevice& device) {
    Require(AgcDriver::Graphics::ActiveRenderScale() == RenderScale{1, 2}, "the depth test runs with APS5_RENDER_SCALE=0.5");
    AgcDriver::Graphics::NoteVideoOutExtent(3840, 2160);
    Surface big(3840, 2160);
    DepthPlane bigDepth(3840, 2160);
    Surface small(1920, 1080);
    DepthPlane smallDepth(1920, 1080);
    DepthStream(device, big, bigDepth);
    DepthStream(device, small, smallDepth);
    device.WaitIdle();
    const auto bigImage = big.Image();
    ExpectExtent(*bigImage, {1920, 1080}, true, "the 3840x2160 color target drawn with depth at scale 0.5");
    ExpectExtent(*small.Image(), {1920, 1080}, false, "the 1920x1080 color target");
    ExpectDepthExtent(device, bigDepth, {1920, 1080}, true, "the 3840x2160 depth surface at scale 0.5");
    ExpectDepthExtent(device, smallDepth, {1920, 1080}, false, "the 1920x1080 depth surface");
    ExpectDemotions({}, "the scaled depth-tested stream");
    Require(AgcDriver::Graphics::RenderScaledSurfaces() == 2, "the color target and the depth surface scale");
    const auto scaledColor = bigImage->ReadImageTexels();
    const auto nativeColor = small.Image()->ReadImageTexels();
    ExpectSameBytes(scaledColor, nativeColor, "the color target of the depth-tested stream");
    const auto scaledDepth = DepthTexels(device, bigDepth);
    const auto nativeDepth = DepthTexels(device, smallDepth);
    ExpectSameBytes(scaledDepth, nativeDepth, "the depth surface of the depth-tested stream");
    ExpectImageTexel(scaledColor, 1920, 50, 50, ColorA, "the scaled color image");
    ExpectImageTexel(scaledColor, 1920, 1088, 540, ColorC, "the scaled color image");
    const auto crossing = CountTexels(scaledColor, ColorB);
    Require(crossing != 0 && crossing < std::size_t{1920} * 1080 / 4, "the crossing triangle passes the depth test only in part, and the far one nowhere");
    Require(FloatAt(scaledDepth, 1920, 50, 50) == 0.5f && FloatAt(scaledDepth, 1920, 1088, 540) < 0.5f, "the scaled depth image does not hold the tested depths");
    std::printf("scaled 3840x2160 color and D32 depth at 0.5 equal native 1920x1080: color %zu bytes fnv %016llx, depth %zu bytes fnv %016llx, %zu crossing texels\n", scaledColor.size(), static_cast<unsigned long long>(Fnv(scaledColor)), scaledDepth.size(), static_cast<unsigned long long>(Fnv(scaledDepth)), crossing);
    static_cast<void>(AgcDriver::Graphics::FlushDepthPlanes(bigDepth.block.Address(), bigDepth.block.bytes));
    device.WaitIdle();
    ExpectDepthExtent(device, bigDepth, {1920, 1080}, true, "the depth surface after its plane is stored to guest memory");
    ExpectDemotions({}, "a depth plane stored to guest memory");
    Require(AgcDriver::Graphics::RenderScaledStores() == 1, "the depth plane is stored once at its native size");
    RedrawCompare(device, big, bigDepth, small, smallDepth, "the depth-tested stream after a depth plane store");
    std::puts("render scale depth tests passed");
}

void PrepassStream(AgcDriver::VulkanDevice& device, const Surface& surface, const DepthPlane& depth) {
    const auto color = surface.Decode();
    const VkExtent2D extent{surface.width, surface.height};
    const VkRect2D full{{0, 0}, extent};
    const DepthPass prepass{depth.Target()};
    const DepthPass equal{depth.Target(), VK_COMPARE_OP_EQUAL, false};
    Draw(device, {{}, extent, full, &FullTriangle, ColorA, prepass, NullExportCode, 0, 0});
    Draw(device, {{}, extent, full, &NearSlantedTriangle, ColorA, prepass, NullExportCode, 0, 0});
    Draw(device, {{color}, extent, full, &FullTriangle, ColorA, equal});
    Draw(device, {{color}, extent, full, &NearSlantedTriangle, ColorC, equal});
}

void DepthPrepassTests(AgcDriver::VulkanDevice& device) {
    Require(AgcDriver::Graphics::ActiveRenderScale() == RenderScale{1, 2}, "the depth prepass test runs with APS5_RENDER_SCALE=0.5");
    AgcDriver::Graphics::NoteVideoOutExtent(3840, 2160);
    Surface big(3840, 2160);
    DepthPlane bigDepth(3840, 2160);
    Surface small(1920, 1080);
    DepthPlane smallDepth(1920, 1080);
    PrepassStream(device, big, bigDepth);
    PrepassStream(device, small, smallDepth);
    device.WaitIdle();
    ExpectExtent(*big.Image(), {1920, 1080}, true, "the 3840x2160 color target after a depth prepass at scale 0.5");
    ExpectDepthExtent(device, bigDepth, {1920, 1080}, true, "the 3840x2160 depth surface drawn by depth-only draws at scale 0.5");
    ExpectDepthExtent(device, smallDepth, {1920, 1080}, false, "the 1920x1080 depth surface");
    ExpectDemotions({}, "the scaled depth prepass and color pass");
    Require(AgcDriver::Graphics::RenderScaledSurfaces() == 2, "the depth surface and the color target scale");
    const auto scaledColor = big.Image()->ReadImageTexels();
    const auto nativeColor = small.Image()->ReadImageTexels();
    ExpectSameBytes(scaledColor, nativeColor, "the color target of the prepass stream");
    const auto scaledDepth = DepthTexels(device, bigDepth);
    const auto nativeDepth = DepthTexels(device, smallDepth);
    ExpectSameBytes(scaledDepth, nativeDepth, "the depth surface of the prepass stream");
    ExpectImageTexel(scaledColor, 1920, 50, 50, ColorA, "the scaled color image");
    ExpectImageTexel(scaledColor, 1920, 1088, 540, ColorC, "the scaled color image");
    Require(CountTexels(scaledColor, ColorA) + CountTexels(scaledColor, ColorC) == std::size_t{1920} * 1080, "the equal-tested color pass misses texels the prepass wrote");
    std::printf("scaled 3840x2160 depth prepass and color pass at 0.5 equal native 1920x1080: color fnv %016llx, depth fnv %016llx\n", static_cast<unsigned long long>(Fnv(scaledColor)), static_cast<unsigned long long>(Fnv(scaledDepth)));
    std::puts("render scale depth prepass tests passed");
}

void DepthUnsetTests(AgcDriver::VulkanDevice& device) {
    Require(!AgcDriver::Graphics::ActiveRenderScale().has_value(), "the unset depth test runs without APS5_RENDER_SCALE");
    AgcDriver::Graphics::NoteVideoOutExtent(3840, 2160);
    Surface big(3840, 2160);
    DepthPlane depth(3840, 2160);
    DepthStream(device, big, depth);
    device.WaitIdle();
    ExpectExtent(*big.Image(), {3840, 2160}, false, "the 3840x2160 color target without APS5_RENDER_SCALE");
    ExpectDepthExtent(device, depth, {3840, 2160}, false, "the 3840x2160 depth surface without APS5_RENDER_SCALE");
    const auto color = big.Image()->ReadImageTexels();
    const auto texels = DepthTexels(device, depth);
    ExpectImageTexel(color, 3840, 100, 100, ColorA, "the native color image");
    ExpectImageTexel(color, 3840, 2176, 1080, ColorC, "the native color image");
    Require(FloatAt(texels, 3840, 100, 100) == 0.5f && FloatAt(texels, 3840, 2176, 1080) < 0.5f, "the native depth image does not hold the tested depths");
    const auto storedColor = GuestTexels(device, big);
    static_cast<void>(AgcDriver::Graphics::FlushDepthPlanes(depth.block.Address(), depth.block.bytes));
    StorageTexture::FlushPending(depth.block.Address(), depth.block.bytes, nullptr, "test read-back");
    device.WaitIdle();
    std::vector<std::byte> storedDepth(depth.block.bytes);
    AgcDriver::GuestMemory::Read(depth.block.Address(), storedDepth, 1);
    ExpectDemotions({}, "the unset depth-tested stream");
    Require(AgcDriver::Graphics::RenderScaledSurfaces() == 0, "a surface scaled without APS5_RENDER_SCALE");
    std::printf("unset 3840x2160 depth-tested stream: color image fnv %016llx, depth image fnv %016llx, guest color fnv %016llx, guest depth fnv %016llx\n", static_cast<unsigned long long>(Fnv(color)), static_cast<unsigned long long>(Fnv(texels)), static_cast<unsigned long long>(Fnv(storedColor)), static_cast<unsigned long long>(Fnv(storedDepth)));
    std::puts("render scale depth unset tests passed");
}

void DepthDemotionTests(AgcDriver::VulkanDevice& device, const std::string& mode) {
    Require(AgcDriver::Graphics::ActiveRenderScale() == RenderScale{1, 2}, "the depth demotion test runs with APS5_RENDER_SCALE=0.5");
    AgcDriver::Graphics::NoteVideoOutExtent(256, 128);
    Surface color(256, 128);
    DepthPlane depth(256, 128, mode == "--depth-copy" ? 2u : 1u, mode == "--stencil-copy");
    const VkRect2D full{{0, 0}, {256, 128}};
    Draw(device, {{color.Decode()}, {256, 128}, full, &FullTriangle, ColorA, DepthPass{depth.Target()}});
    device.WaitIdle();
    const auto image = color.Image();
    ExpectExtent(*image, {128, 64}, true, "the 256x128 color target at scale 0.5");
    ExpectDepthExtent(device, depth, {128, 64}, true, "the 256x128 depth surface at scale 0.5");
    ExpectDemotions({}, "the scaled depth-tested draw");
    const auto context = device.graphicsContext();
    RenderScaleDemotion reason = RenderScaleDemotion::DepthGuestWrite;
    if (mode == "--depth-guest-write") {
        const std::array<float, 4> written{0.125f, 0.125f, 0.125f, 0.125f};
        AgcDriver::GuestMemory::Write(depth.block.Address(), std::as_bytes(std::span(written)), 4);
    } else {
        if (mode == "--depth-storage") {
            reason = RenderScaleDemotion::DepthStorage;
            Require(AgcDriver::Graphics::DepthSurfaceStorage(context, {}, depth.View(Format32Float), 0) != nullptr, "the depth plane has no storage image");
        } else if (mode == "--depth-copy") {
            reason = RenderScaleDemotion::DepthCopy;
            Require(AgcDriver::Graphics::DepthSurfaceTexture(context, {}, depth.View(Format32Float, 1), {}) != nullptr, "the two-layer view of the depth plane has no texture");
        } else if (mode == "--stencil-copy") {
            reason = RenderScaleDemotion::StencilCopy;
            Require(AgcDriver::Graphics::DepthSurfaceTexture(context, {}, depth.View(Format8Unorm), {}) != nullptr, "the unorm view of the stencil plane has no texture");
        } else {
            throw std::runtime_error("unknown depth demotion mode " + mode);
        }
        device.WaitIdle();
        ExpectDepthExtent(device, depth, {256, 128}, false, "the depth surface after " + mode.substr(2));
        ExpectDemotions(Only(reason), "the depth surface after " + mode.substr(2));
    }
    Draw(device, {{color.Decode()}, {256, 128}, full, &NearSlantedTriangle, ColorC, DepthPass{depth.Target()}});
    device.WaitIdle();
    ExpectExtent(*image, {256, 128}, false, "the color target drawn after " + mode.substr(2));
    ExpectDepthExtent(device, depth, {256, 128}, false, "the depth surface drawn after " + mode.substr(2));
    ExpectDemotions(Counts({reason, RenderScaleDemotion::DepthAttachment}), "a color target drawn with the demoted depth surface after " + mode.substr(2));
    const auto texels = image->ReadImageTexels();
    ExpectImageTexel(texels, 256, 2, 2, ColorA, "the demoted color image");
    ExpectImageTexel(texels, 256, 145, 64, ColorC, "the demoted color image");
    std::printf("render scale %s demotion tests passed\n", mode.substr(2).c_str());
}

void ColorStoreGuestWriteTests(AgcDriver::VulkanDevice& device) {
    Require(AgcDriver::Graphics::ActiveRenderScale() == RenderScale{1, 2}, "the color guest write test runs with APS5_RENDER_SCALE=0.5");
    AgcDriver::Graphics::NoteVideoOutExtent(256, 128);
    Surface target(256, 128);
    Draw(device, {{target.Decode()}, {256, 128}, {{0, 0}, {256, 128}}, &FullTriangle, ColorA});
    device.WaitIdle();
    const auto image = target.Image();
    ExpectExtent(*image, {128, 64}, true, "the 256x128 target at scale 0.5");
    const auto stored = GuestTexels(device, target);
    ExpectExtent(*image, {128, 64}, true, "the target after a scaled store");
    ExpectDemotions({}, "a scaled store of the target");
    Require(AgcDriver::Graphics::RenderScaledStores() == 1, "the target is stored once at its native size");
    ExpectGuestTexel(target, stored, 200, 100, ColorA, "the stored guest bytes");
    std::vector<std::byte> written(target.block.bytes);
    const auto texel = Texel(ColorB);
    for (std::size_t offset = 0; offset + 4u <= written.size(); offset += 4u) std::memcpy(written.data() + offset, texel.data(), 4);
    AgcDriver::GuestMemory::Write(target.block.Address(), written, 4);
    Draw(device, {{target.Decode()}, {256, 128}, {{0, 0}, {64, 32}}, &FullTriangle, ColorC});
    device.WaitIdle();
    ExpectExtent(*image, {256, 128}, false, "the target drawn after a guest write");
    ExpectDemotions(Only(RenderScaleDemotion::GuestWrite), "a guest write after a scaled store");
    const auto texels = image->ReadImageTexels();
    ExpectImageTexel(texels, 256, 200, 100, ColorB, "the guest-written image");
    ExpectImageTexel(texels, 256, 10, 10, ColorC, "the guest-written image");
    std::puts("render scale color store guest write tests passed");
}

std::vector<std::byte> StoredDepth(AgcDriver::VulkanDevice& device, const DepthPlane& depth) {
    static_cast<void>(AgcDriver::Graphics::FlushDepthPlanes(depth.block.Address(), depth.block.bytes));
    StorageTexture::FlushPending(depth.block.Address(), depth.block.bytes, nullptr, "test read-back");
    device.WaitIdle();
    std::vector<std::byte> stored(depth.block.bytes);
    AgcDriver::GuestMemory::Read(depth.block.Address(), stored, 1);
    return stored;
}

void StoreEqualsDemotionTests(AgcDriver::VulkanDevice& device) {
    Require(AgcDriver::Graphics::ActiveRenderScale() == RenderScale{1, 2}, "the store comparison runs with APS5_RENDER_SCALE=0.5");
    AgcDriver::Graphics::NoteVideoOutExtent(3840, 2160);
    Surface kept(3840, 2160);
    DepthPlane keptDepth(3840, 2160);
    Surface demoted(3840, 2160);
    DepthPlane demotedDepth(3840, 2160);
    DepthStream(device, kept, keptDepth);
    DepthStream(device, demoted, demotedDepth);
    device.WaitIdle();
    ExpectDemotions({}, "the two scaled streams");
    Require(AgcDriver::Graphics::RenderScaledSurfaces() == 4, "both color targets and both depth surfaces scale");
    const auto keptColor = GuestTexels(device, kept);
    const auto keptPlane = StoredDepth(device, keptDepth);
    ExpectExtent(*kept.Image(), {1920, 1080}, true, "the color target stored scaled");
    ExpectDepthExtent(device, keptDepth, {1920, 1080}, true, "the depth surface stored scaled");
    Require(AgcDriver::Graphics::RenderScaledStores() == 2, "the color target and the depth plane are stored once each at their native size");
    ExpectDemotions({}, "the scaled stores");
    static_cast<void>(demoted.Image()->DemoteRenderScale(RenderScaleDemotion::Copy));
    static_cast<void>(AgcDriver::Graphics::DemoteDepthSurfaceRenderScale(device.graphicsContext(), demotedDepth.Target(), RenderScaleDemotion::DepthCopy));
    const auto demotedColor = GuestTexels(device, demoted);
    const auto demotedPlane = StoredDepth(device, demotedDepth);
    ExpectDemotions(Counts({RenderScaleDemotion::Copy, RenderScaleDemotion::DepthCopy}), "the demoted stream");
    Require(AgcDriver::Graphics::RenderScaledStores() == 2, "a demoted surface was stored as a scaled one");
    ExpectSameBytes(keptColor, demotedColor, "the guest color bytes of a scaled store against demotion then store");
    ExpectSameBytes(keptPlane, demotedPlane, "the guest depth bytes of a scaled store against demotion then store");
    std::printf("scaled stores equal demotion then store: color %zu bytes fnv %016llx, depth %zu bytes fnv %016llx\n", keptColor.size(), static_cast<unsigned long long>(Fnv(keptColor)), keptPlane.size(), static_cast<unsigned long long>(Fnv(keptPlane)));
    std::puts("render scale store comparison tests passed");
}

void IdleStoreTests(AgcDriver::VulkanDevice& device) {
    Require(AgcDriver::Graphics::ActiveRenderScale() == RenderScale{1, 2}, "the idle store test runs with APS5_RENDER_SCALE=0.5");
    AgcDriver::Graphics::NoteVideoOutExtent(3840, 2160);
    Surface big(3840, 2160);
    DepthPlane bigDepth(3840, 2160);
    Surface small(1920, 1080);
    DepthPlane smallDepth(1920, 1080);
    DepthStream(device, big, bigDepth);
    DepthStream(device, small, smallDepth);
    device.WaitIdle();
    Require(StorageTexture::AnyPending(), "the streams left no pending results to store at idle");
    device.StoreIdleResults();
    device.WaitIdle();
    Require(!StorageTexture::AnyPending(), "the idle store left pending results");
    ExpectExtent(*big.Image(), {1920, 1080}, true, "the color target after the idle store");
    ExpectDepthExtent(device, bigDepth, {1920, 1080}, true, "the depth surface after the idle store");
    ExpectDemotions({}, "the idle store");
    Require(AgcDriver::Graphics::RenderScaledStores() == 2, "the idle store stores the scaled color target and depth plane once each");
    RedrawCompare(device, big, bigDepth, small, smallDepth, "the depth-tested stream after an idle store");
    std::puts("render scale idle store tests passed");
}

void PositionStream(AgcDriver::VulkanDevice& device, const Surface& u, const Surface& v, std::optional<VkExtent2D> divisor = std::nullopt) {
    const VkExtent2D extent{u.width, u.height};
    Draw(device, {{u.Decode(), v.Decode()}, extent, {{0, 0}, extent}, &FullTriangle, ColorA, std::nullopt, PositionProgram(divisor.value_or(extent)), PositionInputs, Float32Export | (Float32Export << 4u)});
}

void ExpectPositions(std::span<const std::byte> u, std::span<const std::byte> v, VkExtent2D extent, const std::string& what, std::optional<VkExtent2D> divisor = std::nullopt) {
    Require(u.size() == static_cast<std::size_t>(extent.width) * extent.height * sizeof(float) && v.size() == u.size(), what + ": the position images have the wrong size");
    const float stepX = 1.0f / static_cast<float>(divisor.value_or(extent).width);
    const float stepY = 1.0f / static_cast<float>(divisor.value_or(extent).height);
    for (std::uint32_t y = 0; y < extent.height; ++y) {
        for (std::uint32_t x = 0; x < extent.width; ++x) {
            const float expectedU = (static_cast<float>(x) + 0.5f) * stepX;
            const float expectedV = (static_cast<float>(y) + 0.5f) * stepY;
            const float actualU = FloatAt(u, extent.width, x, y);
            const float actualV = FloatAt(v, extent.width, x, y);
            if (std::bit_cast<std::uint32_t>(actualU) == std::bit_cast<std::uint32_t>(expectedU) && std::bit_cast<std::uint32_t>(actualV) == std::bit_cast<std::uint32_t>(expectedV)) continue;
            char detail[192];
            std::snprintf(detail, sizeof(detail), ": pixel (%u, %u) reads position (%.9g, %.9g), expected (%.9g, %.9g)", x, y, actualU, actualV, expectedU, expectedV);
            throw std::runtime_error(what + detail);
        }
    }
}

void FragCoordTests(AgcDriver::VulkanDevice& device) {
    Require(AgcDriver::Graphics::ActiveRenderScale() == RenderScale{1, 2}, "the position test runs with APS5_RENDER_SCALE=0.5");
    AgcDriver::Graphics::NoteVideoOutExtent(3840, 2160);
    Surface bigU(3840, 2160, R32FloatInfo);
    Surface bigV(3840, 2160, R32FloatInfo);
    Surface smallU(1920, 1080, R32FloatInfo);
    Surface smallV(1920, 1080, R32FloatInfo);
    PositionStream(device, bigU, bigV);
    PositionStream(device, smallU, smallV);
    device.WaitIdle();
    ExpectExtent(*bigU.Image(), {1920, 1080}, true, "the 3840x2160 u target at scale 0.5");
    ExpectExtent(*bigV.Image(), {1920, 1080}, true, "the 3840x2160 v target at scale 0.5");
    ExpectExtent(*smallU.Image(), {1920, 1080}, false, "the 1920x1080 u target");
    ExpectDemotions({}, "the scaled position pass");
    Require(AgcDriver::Graphics::RenderScaledSurfaces() == 2, "both position targets scale");
    const auto scaledU = bigU.Image()->ReadImageTexels();
    const auto scaledV = bigV.Image()->ReadImageTexels();
    const auto nativeU = smallU.Image()->ReadImageTexels();
    const auto nativeV = smallV.Image()->ReadImageTexels();
    ExpectPositions(nativeU, nativeV, {1920, 1080}, "the native 1920x1080 position pass");
    ExpectSameBytes(scaledU, nativeU, "the u target of the position pass");
    ExpectSameBytes(scaledV, nativeV, "the v target of the position pass");
    std::printf("scaled 3840x2160 position pass at 0.5 equals native 1920x1080: u fnv %016llx, v fnv %016llx\n", static_cast<unsigned long long>(Fnv(scaledU)), static_cast<unsigned long long>(Fnv(scaledV)));
    Surface sharedU(1920, 1080, R32FloatInfo);
    Surface sharedV(1920, 1080, R32FloatInfo);
    PositionStream(device, sharedU, sharedV, VkExtent2D{3840, 2160});
    device.WaitIdle();
    ExpectExtent(*sharedU.Image(), {1920, 1080}, false, "the 1920x1080 u target drawn by the scaled pass's shader");
    ExpectPositions(sharedU.Image()->ReadImageTexels(), sharedV.Image()->ReadImageTexels(), {1920, 1080}, "an unscaled draw with the shader of the scaled position pass", VkExtent2D{3840, 2160});
    std::puts("render scale fragment position tests passed");
}

void FragCoordUnsetTests(AgcDriver::VulkanDevice& device) {
    Require(!AgcDriver::Graphics::ActiveRenderScale().has_value(), "the unset position test runs without APS5_RENDER_SCALE");
    AgcDriver::Graphics::NoteVideoOutExtent(3840, 2160);
    Surface u(3840, 2160, R32FloatInfo);
    Surface v(3840, 2160, R32FloatInfo);
    PositionStream(device, u, v);
    device.WaitIdle();
    ExpectExtent(*u.Image(), {3840, 2160}, false, "the u target without APS5_RENDER_SCALE");
    const auto texelsU = u.Image()->ReadImageTexels();
    const auto texelsV = v.Image()->ReadImageTexels();
    ExpectPositions(texelsU, texelsV, {3840, 2160}, "the unset 3840x2160 position pass");
    const auto storedU = GuestTexels(device, u);
    const auto storedV = GuestTexels(device, v);
    ExpectDemotions({}, "the unset position pass");
    Require(AgcDriver::Graphics::RenderScaledSurfaces() == 0, "a surface scaled without APS5_RENDER_SCALE");
    std::printf("unset 3840x2160 position pass: u fnv %016llx, v fnv %016llx, guest u fnv %016llx, guest v fnv %016llx\n", static_cast<unsigned long long>(Fnv(texelsU)), static_cast<unsigned long long>(Fnv(texelsV)), static_cast<unsigned long long>(Fnv(storedU)), static_cast<unsigned long long>(Fnv(storedV)));
    std::puts("render scale fragment position unset tests passed");
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
        else if (mode == "--depth") DepthTests(*device);
        else if (mode == "--depth-unset") DepthUnsetTests(*device);
        else if (mode == "--depth-prepass") DepthPrepassTests(*device);
        else if (mode == "--depth-guest-write" || mode == "--depth-storage" || mode == "--depth-copy" || mode == "--stencil-copy") DepthDemotionTests(*device, mode);
        else if (mode == "--color-store-guest-write") ColorStoreGuestWriteTests(*device);
        else if (mode == "--store-equals-demotion") StoreEqualsDemotionTests(*device);
        else if (mode == "--idle-store") IdleStoreTests(*device);
        else if (mode == "--fragcoord") FragCoordTests(*device);
        else if (mode == "--fragcoord-unset") FragCoordUnsetTests(*device);
        else throw std::runtime_error("unknown mode " + mode);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
