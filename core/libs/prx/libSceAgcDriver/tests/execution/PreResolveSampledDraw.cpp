#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ColorTargetTransfer.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp"
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
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Width = 64;
constexpr std::uint32_t Height = 32;
constexpr std::uint32_t Format8888Unorm = 56;
constexpr std::uint32_t Type2D = 9;
constexpr std::size_t TextureBytes = Width * Height * 4u;
constexpr std::size_t TargetOffset = 65536;
constexpr std::size_t TargetBytes = 65536;
constexpr std::size_t VertexOffset = TargetOffset + TargetBytes;
constexpr std::size_t ArenaBytes = VertexOffset + 65536;
constexpr std::array<std::array<float, 4>, 3> Triangle{{{-1.0f, -1.0f, 0.5f, 1.0f}, {3.0f, -1.0f, 0.5f, 1.0f}, {-1.0f, 3.0f, 0.5f, 1.0f}}};

alignas(256) constexpr std::array<std::uint32_t, 6> VertexCode{
    0xe0382000, 0x80000005, 0xbf8c3f70, 0xf80008cf, 0x03020100, 0xbf810000,
};

alignas(256) constexpr std::array<std::uint32_t, 13> SampledPixelCode{
    0xbe8c047e, 0xbefe0a7e, 0x100000ff, 0x3c800000, 0x100202ff, 0x3d000000, 0xf0800f08, 0x00400200,
    0xbf8c3f70, 0x87fe0c7e, 0xf800180f, 0x05040302, 0xbf810000,
};

std::uint64_t AddressOf(const void* data) {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(data));
}

class GuestBlock {
public:
    GuestBlock() {
#ifdef _WIN32
        block = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(ArenaBytes, 65536));
        if (block != nullptr) GuestArena::GuestArenaCommit_nid_postfix(block, ArenaBytes, PAGE_READWRITE, ArenaBytes);
#else
        constexpr std::uintptr_t alignment = 65536;
        void* mapped = mmap(nullptr, ArenaBytes + alignment, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapped != MAP_FAILED) {
            const auto begin = reinterpret_cast<std::uintptr_t>(mapped);
            const auto aligned = (begin + alignment - 1) & ~(alignment - 1);
            if (aligned != begin) munmap(mapped, aligned - begin);
            if (aligned + ArenaBytes != begin + ArenaBytes + alignment) munmap(reinterpret_cast<void*>(aligned + ArenaBytes), begin + alignment - aligned);
            block = reinterpret_cast<std::uint8_t*>(aligned);
            GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(block, ArenaBytes);
        }
#endif
        Require(block != nullptr, "pre-resolved sampled draw: cannot allocate the guest block");
        Require(AgcDriver::GuestMemory::Watched(AddressOf(block), ArenaBytes), "pre-resolved sampled draw: the guest block is not write-watched");
        GuestAllocations::Mutation().Add(block, ArenaBytes, true, true, true);
    }

    ~GuestBlock() {
        GuestAllocations::Mutation().Remove(block);
#ifdef _WIN32
        GuestArena::GuestArenaReset_nid_postfix(block, ArenaBytes);
        GuestArena::GuestArenaRelease_nid_postfix(block, ArenaBytes);
#else
        munmap(block, ArenaBytes);
        GuestWriteWatch::GuestWriteWatchUnregister_nid_postfix(block, ArenaBytes);
#endif
    }

    GuestBlock(const GuestBlock&) = delete;
    GuestBlock& operator=(const GuestBlock&) = delete;

    std::uint8_t* Data() { return block; }

private:
    std::uint8_t* block = nullptr;
};

std::array<std::uint32_t, 4> BufferDescriptor(const void* data, std::uint32_t stride, std::uint32_t count) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (stride << 16u), count, 0x01016facu};
}

std::array<std::uint32_t, 8> TextureDescriptor(const std::uint8_t* texels) {
    const auto address = AddressOf(texels);
    return {
        static_cast<std::uint32_t>(address >> 8u),
        static_cast<std::uint32_t>((address >> 40u) & 0xffu) | (Format8888Unorm << 20u) | (((Width - 1u) & 3u) << 30u),
        ((Width - 1u) >> 2u) | ((Height - 1u) << 14u),
        0xfacu | (Type2D << 28u),
        0u, 0u, 0u, 0u,
    };
}

std::array<std::uint32_t, 4> SamplerDescriptor() {
    return {0u, 0xfffu << 12u, 1u << 26u, 0u};
}

void Fill(std::uint8_t* texels, std::array<std::uint8_t, 4> color) {
    for (std::size_t offset = 0; offset < TextureBytes; offset += color.size()) std::memcpy(texels + offset, color.data(), color.size());
}

void Check(const std::string& what, std::span<const std::byte> pixels, std::array<std::uint8_t, 4> color) {
    for (std::uint32_t y = 0; y < Height; ++y) {
        for (std::uint32_t x = 0; x < Width; ++x) {
            const auto* written = &pixels[(y * Width + x) * 4u];
            for (std::uint32_t channel = 0; channel < 4u; ++channel) {
                const auto value = std::to_integer<std::uint32_t>(written[channel]);
                Require(value == color[channel], what + ": pixel (" + std::to_string(x) + ", " + std::to_string(y) + ") channel " + std::to_string(channel) + " is " + std::to_string(value) + ", expected " + std::to_string(color[channel]));
            }
        }
    }
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        GuestBlock block;
        auto* texels = block.Data();
        const std::array<std::uint8_t, 4> first{0x20u, 0x40u, 0x60u, 0xffu};
        const std::array<std::uint8_t, 4> stored{0x80u, 0x30u, 0x10u, 0xffu};
        const std::array<std::uint8_t, 4> cpu{0x50u, 0xa0u, 0xc0u, 0xffu};
        const std::array<std::uint8_t, 4> built{0x11u, 0x22u, 0x33u, 0xffu};
        Fill(texels, first);
        AgcDriver::GuestMemory::BumpCollectEpoch();
        const auto target = device->Target();
        std::vector<std::uint32_t> vertexUserData(4, 0u);
        std::memcpy(texels + VertexOffset, Triangle.data(), sizeof(Triangle));
        const auto vertexBuffer = BufferDescriptor(texels + VertexOffset, 16u, static_cast<std::uint32_t>(Triangle.size()));
        std::copy(vertexBuffer.begin(), vertexBuffer.end(), vertexUserData.begin());
        const std::array<ShaderRecompiler::MemoryRegion, 1> vertexMemory{{{reinterpret_cast<std::uintptr_t>(VertexCode.data()), std::as_bytes(std::span(VertexCode))}}};
        ShaderRecompiler::RecompileRequest vertex{
            {ShaderStage::Vertex, reinterpret_cast<std::uintptr_t>(VertexCode.data()), VertexCode, 0, {}},
            {64, 0, vertexUserData, std::nullopt, std::nullopt, ShaderRecompiler::ShaderVertexStageInfo{}, vertexMemory},
            target,
            {0, 0, 0, 64}
        };
        vertex.useCache = false;
        const auto vertexResult = ShaderRecompiler::Recompile(vertex);
        const auto vertexPush = static_cast<std::uint32_t>(vertexResult.pushConstants.size());
        ShaderRecompiler::ShaderPixelStageInfo pixel{};
        pixel.inputAddr = ShaderRecompiler::PixelInputBit(ShaderRecompiler::PixelInput::PositionX) | ShaderRecompiler::PixelInputBit(ShaderRecompiler::PixelInput::PositionY);
        pixel.posX = true;
        pixel.posY = true;
        pixel.targetOutputMode[0] = 9;
        pixel.targetExportMapping.fill(0xe4u);
        pixel.quadPixelMask = 0xfu;
        std::vector<std::uint32_t> pixelUserData(12, 0u);
        const auto texture = TextureDescriptor(texels);
        const auto sampler = SamplerDescriptor();
        std::copy(texture.begin(), texture.end(), pixelUserData.begin());
        std::copy(sampler.begin(), sampler.end(), pixelUserData.begin() + 8);
        const std::array<ShaderRecompiler::MemoryRegion, 1> pixelMemory{{{reinterpret_cast<std::uintptr_t>(SampledPixelCode.data()), std::as_bytes(std::span(SampledPixelCode))}}};
        ShaderRecompiler::RecompileRequest fragment{
            {ShaderStage::Fragment, reinterpret_cast<std::uintptr_t>(SampledPixelCode.data()), SampledPixelCode, 0, {}},
            {64, 0, pixelUserData, std::nullopt, pixel, std::nullopt, pixelMemory},
            target,
            PixelPushLayout(vertexPush, target)
        };
        fragment.useCache = false;
        const auto pixelResult = ShaderRecompiler::Recompile(fragment);
        const std::array<AgcDriver::Graphics::CompiledShader, 2> shaders{{
            {ShaderStage::Vertex, &vertexResult, 0},
            {ShaderStage::Fragment, &pixelResult, PixelPushOffset(vertexPush, target)}
        }};
        AgcDriver::Graphics::State state{};
        state.stages = {AgcDriver::Graphics::ShaderPath::Vertex, 0u, 64u, 64u, std::nullopt, std::nullopt};
        const auto targetAddress = AddressOf(texels + TargetOffset);
        state.color = {targetAddress, {Width, Height}, VK_FORMAT_R8G8B8A8_UNORM, TargetBytes, 0xe4u, AgcDriver::Graphics::ColorTileMode::RenderTarget, 4};
        state.colors = {state.color};
        state.hasColorTarget = true;
        state.renderExtent = {Width, Height};
        state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        state.viewport = {0, static_cast<float>(Height), static_cast<float>(Width), -static_cast<float>(Height), 0, 1};
        state.negativeOneToOne = false;
        state.scissor = {{0, 0}, {Width, Height}};
        state.cullMode = VK_CULL_MODE_NONE;
        state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        state.blend.colorWriteMask = 15;
        state.blends = {state.blend};
        state.blendConstants = {};
        const AgcDriver::Pm4::DrawParameters parameters{0, static_cast<std::uint32_t>(Triangle.size()), 0, 1, 0, false};
        const auto draw = [&](const std::string& what, const std::function<void()>& race, std::array<std::uint8_t, 4> color, std::uint64_t found, std::uint64_t builds) {
            const auto before = AgcDriver::Graphics::PreResolveCounted();
            const auto pre = device->PreResolveDraw(state, parameters, shaders, {});
            Require(pre != nullptr, what + ": the draw was not pre-resolved");
            race();
            {
                std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
                device->Draw(state, parameters, shaders, {}, nullptr, nullptr, 0, pre.get());
            }
            device->WaitIdle();
            std::vector<std::byte> pixels(Width * Height * 4u);
            {
                std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
                AgcDriver::Graphics::ReadColorTarget(state.color, pixels);
            }
            Check(what, pixels, color);
            const std::vector<std::byte> cleared(TargetBytes);
            AgcDriver::GuestMemory::Write(targetAddress, cleared);
            const auto after = AgcDriver::Graphics::PreResolveCounted();
            Require(after.found - before.found == found && after.built - before.built == builds && after.used - before.used == 1u && after.unused == before.unused && after.failed == before.failed, what + ": pre-resolve counts moved by found " + std::to_string(after.found - before.found) + ", built " + std::to_string(after.built - before.built) + ", used " + std::to_string(after.used - before.used) + ", unused " + std::to_string(after.unused - before.unused) + ", failed " + std::to_string(after.failed - before.failed));
        };
        const auto store = [&](std::array<std::uint8_t, 4> color) {
            Fill(texels, color);
            AgcDriver::GuestMemory::MarkWritten(AddressOf(texels), TextureBytes);
        };
        draw("a draw built in stage A before the GPU mutex", [] {}, first, 0u, 1u);
        draw("a cache hit carried into the GPU mutex over unchanged texels", [] {}, first, 1u, 0u);
        draw("a cache hit carried into the GPU mutex with a driver store before it", [&] { store(stored); }, stored, 1u, 0u);
        draw("a cache hit carried into the GPU mutex with a texture cache eviction before it", [&] {
            std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
            AgcDriver::Graphics::ClearCachedTextures(device->Device());
        }, stored, 1u, 0u);
        draw("a cache hit carried into the GPU mutex with a CPU store and an ordering point before it", [&] {
            Fill(texels, cpu);
            AgcDriver::GuestMemory::BumpCollectEpoch();
        }, cpu, 1u, 0u);
        draw("a cache hit evicted from the resource cache before the GPU mutex", [&] {
            std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
            AgcDriver::Graphics::SharedResourceCache().Clear();
        }, cpu, 1u, 0u);
        draw("a stage A build with a driver store before the GPU mutex", [&] { store(built); }, built, 0u, 1u);
        {
            std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
            AgcDriver::Graphics::SharedResourceCache().Clear();
            AgcDriver::Graphics::FlushCachedTextures(device->Device());
        }
        device->WaitIdle();
        std::puts("pre-resolved sampled draw tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
