#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Resources.hpp"
#include "prx/libSceAgcDriver/Graphics/include/UnitShadow.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include "GuestTestMemory.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace AgcDriver {

struct VulkanDeviceTestAccess {
    static Graphics::Context ContextOf(const VulkanDevice& device) { return device.graphicsContext(); }
};

}

#ifdef _WIN32

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;
namespace Graphics = AgcDriver::Graphics;

constexpr std::uint32_t Threads = 32;
constexpr std::uint64_t Unit = 65536;
constexpr std::size_t BlockBytes = 4 * Unit;
constexpr std::uint8_t ImportByte = 0x11;

TestGuest::Array<std::uint32_t, 8> PointerReader{0x7e040200u, 0x7e060201u, 0xdc308000u, 0x017d0002u, 0xbf8c3f70u, 0xe0700000u, 0x80010100u, 0xbf810000u};
TestGuest::Array<std::uint32_t, 8> BufferReader{0xe0300000u, 0x80000100u, 0xbf8c3f70u, 0xe0700000u, 0x80010100u, 0xbf810000u, 0u, 0u};
TestGuest::Array<std::uint32_t, 4> Output;

std::array<std::uint32_t, 4> BufferDescriptor(std::uint64_t address, std::uint32_t bytes) {
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, 0x31016facu};
}

std::string Hex(std::uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08x", value);
    return text;
}

std::uint32_t Read(AgcDriver::VulkanDevice& device, TestGuest::Array<std::uint32_t, 8>& code, const std::array<std::uint32_t, 4>& source, bool unbounded) {
    std::vector<std::uint32_t> userData(source.begin(), source.end());
    const auto output = BufferDescriptor(reinterpret_cast<std::uintptr_t>(Output.data()), static_cast<std::uint32_t>(decltype(Output)::ByteSize));
    userData.insert(userData.end(), output.begin(), output.end());
    const std::span<const std::uint32_t> program(code.data(), code.size());
    const auto address = reinterpret_cast<std::uintptr_t>(program.data());
    const std::array<ShaderRecompiler::MemoryRegion, 1> memory{{{address, std::as_bytes(program)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{Threads, 1, 1}, 0u, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, address, program, 0, {}},
        {32, 0, userData, compute, std::nullopt, std::nullopt, memory},
        device.Target(),
        {0, 0, 0, 128}
    };
    request.useCache = false;
    const auto result = ShaderRecompiler::Recompile(request);
    Require(result.unboundedReads == unbounded && !result.unboundedWrites, std::string("shadowed page-table reads: the ") + (unbounded ? "pointer" : "buffer") + " reader's accesses are not of the expected kind");
    Output.fill(0u);
    device.Dispatch(result, 1, 1, 1, address);
    device.WaitIdle();
    return Output[0];
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        const auto context = AgcDriver::VulkanDeviceTestAccess::ContextOf(*device);
        if (!Graphics::UnitShadowEnabled() || context.hostImportAlignment == 0) {
            std::puts("shadowed page-table reads: unit shadows or host imports unavailable");
            return VulkanTestSkipped;
        }
        auto* block = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(BlockBytes, Unit));
        Require(block != nullptr, "shadowed page-table reads: cannot reserve the block");
        GuestArena::GuestArenaCommit_nid_postfix(block, BlockBytes, PAGE_READWRITE, BlockBytes);
        std::memset(block, ImportByte, BlockBytes);
        const auto address = reinterpret_cast<std::uint64_t>(block);
        GuestAllocations::Mutation().Add(block, BlockBytes, true, true, true);
        const auto* import = Graphics::HostImportFor(context, address, BlockBytes);
        Require(import != nullptr && AgcDriver::GuestMemory::Watched(address, BlockBytes), "shadowed page-table reads: the block is not imported and watched");
        AgcDriver::GuestMemory::CollectWritesUncached(address, BlockBytes);
        const auto unit = address + Unit;
        const std::array<std::uint32_t, 4> pointer{static_cast<std::uint32_t>(unit + 64u), static_cast<std::uint32_t>((unit + 64u) >> 32u), 0u, 0u};
        const auto view = BufferDescriptor(unit + 128u, 256u);
        Require(Read(*device, PointerReader, pointer, true) == 0x11111111u, "shadowed page-table reads: the first read did not see the import");
        const auto shadow = [&](std::uint8_t value) {
            std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
            auto* recorder = Graphics::Recorder::Active();
            Require(recorder != nullptr, "shadowed page-table reads: the driver opened no recorder");
            auto destination = Graphics::ShadowDestinationFor(context, *import, unit, unit + Unit);
            Require(destination.has_value() && destination->seedUnits.empty(), "shadowed page-table reads: no slab destination for a whole unit");
            auto pattern = std::make_shared<Graphics::Buffer>(context, static_cast<std::size_t>(Unit), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            std::memset(pattern->Bytes().data(), value, pattern->Bytes().size());
            const auto commands = recorder->Commands();
            recorder->Keep(pattern);
            recorder->Keep(destination->slab);
            Graphics::RecordMemoryBarrier(context, commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
            const VkBufferCopy piece{0, Graphics::SlabOffset(*import, *destination->slab, unit), Unit};
            context.Function<PFN_vkCmdCopyBuffer>("vkCmdCopyBuffer")(commands, pattern->Handle(), destination->buffer, 1, &piece);
            Graphics::RecordMemoryBarrier(context, commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
            recorder->MarkCovered(VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
            const Graphics::ShadowedRange range{unit, unit + Unit, destination->slab};
            Graphics::MarkShadowed(*import, std::span(&range, 1), AgcDriver::GuestMemory::TrackerGeneration());
        };
        shadow(0x22);
        Require(!Graphics::StorageTexture::AnyPending() && Graphics::AnyShadowedOverlaps(unit, Unit), "shadowed page-table reads: the unit is not shadowed with no image pending");
        const auto pointed = Read(*device, PointerReader, pointer, true);
        Require(pointed == 0x22222222u, "shadowed page-table reads: a pointer read of a shadowed unit read " + Hex(pointed) + ", expected the shadow's bytes");
        shadow(0x44);
        const auto viewed = Read(*device, BufferReader, view, false);
        Require(viewed == 0x44444444u, "shadowed page-table reads: a buffer read of a shadowed unit read " + Hex(viewed) + ", expected the shadow's bytes");
        GuestAllocations::Mutation().Remove(block);
        std::puts("shadowed page-table reads tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#else

int main() {
    return VulkanTestSkipped;
}

#endif
