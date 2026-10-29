#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/VideoOutput.hpp"
#include "prx/libSceAgcDriver/Graphics/include/DrawKeyCensus.hpp"
#include "prx/libSceAgcDriver/Submit/include/Dcb.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libc/include/GuestWriteWatch.hpp"
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
#include <atomic>
#include <bit>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
namespace Census = AgcDriver::Graphics::DrawKeyCensus;

constexpr std::uint32_t Width = 64;
constexpr std::uint32_t Height = 16;
constexpr std::uint32_t SetContextReg = 0x69u;
constexpr std::uint32_t SetShReg = 0x76u;
constexpr std::uint32_t SetUconfigReg = 0x79u;
constexpr std::uint32_t DrawIndexAuto = 0x2du;
constexpr std::uint32_t OutputHandle = 7;
constexpr std::uint32_t Frames = 4;
constexpr std::uint32_t MovedCopies = 3;
constexpr std::uint32_t VertexBufferRegister = 0x08c;
constexpr std::uint32_t ViewportZOffsetRegister = 0x114;
constexpr std::size_t Block = 65536;
constexpr std::size_t CopyBytes = 256;
std::byte* ColorMemory = nullptr;
alignas(64) volatile std::uint32_t EndOfPipe = 0;

std::byte* AllocateWatched(std::size_t bytes) {
#ifdef _WIN32
    void* block = GuestArena::GuestArenaAllocate_nid_postfix(bytes, Block);
    GuestArena::GuestArenaCommit_nid_postfix(block, bytes, PAGE_READWRITE, bytes);
#else
    void* raw = mmap(nullptr, bytes + Block, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (raw == MAP_FAILED) throw std::runtime_error("cannot map the watched block");
    const auto begin = reinterpret_cast<std::uintptr_t>(raw);
    const auto aligned = (begin + Block - 1) & ~static_cast<std::uintptr_t>(Block - 1);
    if (aligned != begin) munmap(raw, aligned - begin);
    if (aligned + bytes != begin + bytes + Block) munmap(reinterpret_cast<void*>(aligned + bytes), begin + Block - aligned);
    void* block = reinterpret_cast<void*>(aligned);
    GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(block, bytes);
#endif
    return static_cast<std::byte*>(block);
}

alignas(256) constexpr std::array<std::uint32_t, 6> VertexCode{
    0xe0382000, 0x80020005, 0xbf8c3f70, 0xf80008cf, 0x03020100, 0xbf810000,
};

alignas(256) constexpr std::array<std::uint32_t, 7> PixelCode{
    0x7e0002f2, 0x7e0202f2, 0x7e0402f2, 0x7e0602f2, 0xf800080f, 0x03020100, 0xbf810000,
};

constexpr std::array<std::array<float, 4>, 3> Vertices{{
    {-1.0f, -1.0f, 0.5f, 1.0f}, {3.0f, -1.0f, 0.5f, 1.0f}, {-1.0f, 3.0f, 0.5f, 1.0f},
}};

struct RegisteredShader {
    Shader shader;
    ShaderUserData userData;
};
RegisteredShader VertexShader{};
RegisteredShader PixelShader{};

const std::vector<std::pair<std::uint32_t, std::uint32_t>> ContextBaseline{
    {0x2d5, 0x2000},
    {0x1b6, 0}, {0x207, 0}, {0x200, 0},
    {0x2dc, 0xaa00}, {0x2f8, 0}, {0x292, 2}, {0x293, 0},
    {0x80, 0}, {0x8d, 0}, {0x83, 0xffff}, {0x8c, 0xa},
    {0x2f9, 0x2d}, {0x313, 0x6000},
    {0x206, 0x43f}, {0x204, 0x80000}, {0x205, 0x240},
    {0x8e, 0xf}, {0x8f, 0xf}, {0x202, 0xcc0010},
    {0x1c4, 0}, {0x1c5, 9}, {0x1c3, 4}, {0x31c, 0x28028},
    {0x31b, 0}, {0x31d, 0}, {0x3b8, 0x9000000}, {0x1e0, 0},
    {0xc, 0},
    {0x81, 0x80000000}, {0x90, 0x80000000}, {0x94, 0x80000000},
    {0xb4, 0}, {0xb5, std::bit_cast<std::uint32_t>(1.0f)}, {0x114, 0}, {0x113, std::bit_cast<std::uint32_t>(1.0f)},
    {0x203, 0x800u}, {0x30e, 0xffffffffu}, {0x30f, 0xffffffffu},
};

void Register(RegisteredShader& registered, const void* code, std::uint32_t bytes, std::uint8_t type) {
    registered.shader.file_header = 0x34333231u;
    registered.shader.version = 0x18u;
    registered.shader.user_data = &registered.userData;
    registered.shader.code = code;
    registered.shader.header_size = sizeof(RegisteredShader);
    registered.shader.shader_size = bytes;
    registered.shader.type = type;
    AgcDriverRegisterShader_nid_postfix(&registered.shader);
}

std::array<std::uint32_t, 4> BufferDescriptor(const void* data, std::uint32_t stride, std::uint32_t count) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (stride << 16u), count, 0x01016facu};
}

void SetRegister(std::vector<std::uint32_t>& words, std::uint32_t opcode, std::uint32_t offset, std::uint32_t value) {
    words.push_back(0xc0010000u | (opcode << 8u));
    words.push_back(offset);
    words.push_back(value);
}

void AppendDraw(std::vector<std::uint32_t>& words, const std::byte* vertices) {
    const auto descriptor = BufferDescriptor(vertices, 16u, static_cast<std::uint32_t>(Vertices.size()));
    for (std::uint32_t i = 0; i < descriptor.size(); ++i) SetRegister(words, SetShReg, VertexBufferRegister + i, descriptor[i]);
    words.insert(words.end(), {0xc0000000u | (1u << 16u) | (DrawIndexAuto << 8u), static_cast<std::uint32_t>(Vertices.size()), 2u});
}

std::vector<std::uint32_t> Frame(const std::byte* fixed, const std::byte* moved, std::uint32_t zOffset) {
    const auto colorAddress = reinterpret_cast<std::uintptr_t>(ColorMemory);
    const auto vertexAddress = reinterpret_cast<std::uintptr_t>(VertexCode.data());
    const auto pixelAddress = reinterpret_cast<std::uintptr_t>(PixelCode.data());
    std::vector<std::uint32_t> words;
    for (const auto& [offset, value] : ContextBaseline) SetRegister(words, SetContextReg, offset, value);
    if (zOffset != 0) SetRegister(words, SetContextReg, ViewportZOffsetRegister, zOffset);
    SetRegister(words, SetContextReg, 0x318, static_cast<std::uint32_t>(colorAddress >> 8u));
    SetRegister(words, SetContextReg, 0x390, static_cast<std::uint32_t>(colorAddress >> 40u));
    SetRegister(words, SetContextReg, 0x3b0, ((Width - 1u) << 14u) | (Height - 1u));
    SetRegister(words, SetContextReg, 0x10f, std::bit_cast<std::uint32_t>(Width / 2.0f));
    SetRegister(words, SetContextReg, 0x110, std::bit_cast<std::uint32_t>(Width / 2.0f));
    SetRegister(words, SetContextReg, 0x111, std::bit_cast<std::uint32_t>(-(Height / 2.0f)));
    SetRegister(words, SetContextReg, 0x112, std::bit_cast<std::uint32_t>(Height / 2.0f));
    const auto extent = (Height << 16u) | Width;
    SetRegister(words, SetContextReg, 0xd, extent);
    SetRegister(words, SetContextReg, 0x82, extent);
    SetRegister(words, SetContextReg, 0x91, extent);
    SetRegister(words, SetContextReg, 0x95, extent);
    SetRegister(words, SetUconfigReg, 0x242, 4u);
    SetRegister(words, SetShReg, 0x008, static_cast<std::uint32_t>(pixelAddress >> 8u));
    SetRegister(words, SetShReg, 0x009, static_cast<std::uint32_t>(pixelAddress >> 40u));
    SetRegister(words, SetShReg, 0x00b, 0u);
    SetRegister(words, SetShReg, 0x0c8, static_cast<std::uint32_t>(vertexAddress >> 8u));
    SetRegister(words, SetShReg, 0x0c9, static_cast<std::uint32_t>(vertexAddress >> 40u));
    SetRegister(words, SetShReg, VertexBufferRegister - 1u, 4u << 1u);
    AppendDraw(words, fixed);
    AppendDraw(words, moved);
    const auto labelAddress = reinterpret_cast<std::uintptr_t>(&EndOfPipe);
    words.insert(words.end(), {0xc0064900u, 0x514u, (1u << 29u) | (2u << 24u), static_cast<std::uint32_t>(labelAddress), static_cast<std::uint32_t>(static_cast<std::uint64_t>(labelAddress) >> 32u), 1u, 0u, 0u});
    words.insert(words.end(), {AgcDriver::FlipPacketHeader, OutputHandle, 0xfffffffeu, 1, 0, 0});
    return words;
}

class Request final : public AgcDriver::IFlipRequest {
public:
    explicit Request(std::atomic<int>& ready) : ready(ready) {}
    void GpuReady(const std::shared_ptr<AgcDriver::FrameTiming>&) override { ++ready; }
    void Fail(std::exception_ptr) noexcept override {}
private:
    std::atomic<int>& ready;
};

class Output final : public AgcDriver::IVideoOutput {
public:
    std::shared_ptr<AgcDriver::IFlipRequest> Reserve(const AgcDriver::FlipInfo&) override { return std::make_shared<Request>(ready); }
    void Fail(std::exception_ptr) noexcept override {}
    std::atomic<int> ready = 0;
};

std::string Describe(const Census::FrameSummary& frame) {
    return "census frame " + std::to_string(frame.serial) + " (" + std::to_string(frame.keyed) + " keyed, repeats " + std::to_string(frame.keyRepeats) + " key / " + std::to_string(frame.shapeRepeats) + " shape / " + std::to_string(frame.shaderSetRepeats) + " shader set, " + std::to_string(frame.shapeOnlyRepeats) + " shape-only)";
}

void CheckConsistent(const Census::FrameSummary& frame) {
    const auto what = Describe(frame);
    const auto count = [&](Census::Count kind) { return frame.counts[static_cast<std::size_t>(kind)]; };
    Require(frame.keyRepeats <= frame.keyed && frame.shapeRepeats <= frame.keyed && frame.shaderSetRepeats <= frame.keyed, what + ": more repeats than keyed draws");
    Require(frame.keyRepeats <= frame.shapeRepeats, what + ": more full-key repeats than shape-key repeats");
    Require(frame.shapeOnlyRepeats + frame.keyRepeats == frame.shapeRepeats, what + ": shape-only and full-key repeats do not add up to the shape-key repeats");
    Require(frame.keyed == count(Census::Count::Lookups) && count(Census::Count::Lookups) <= count(Census::Count::Draws), what + ": keyed draws, lookups and draws disagree");
    Require(count(Census::Count::KeyCached) <= count(Census::Count::Lookups), what + ": more cached keys than lookups");
    Require(frame.addressLikeWords <= frame.differingWords, what + ": more address-like words than differing words");
    Require(frame.contentSame + frame.contentChanged + frame.contentUnhashed <= frame.shapeRepeats, what + ": more content verdicts than shape-key repeats");
    const auto sum = [](const auto& values) { return std::accumulate(values.begin(), values.end(), std::uint64_t{0}); };
    Require(sum(frame.uncacheable) == count(Census::Count::ResourceUncacheable), what + ": the uncacheable lookups by reason do not add up to the uncacheable lookups");
    Require(sum(frame.notRecordable) == frame.uncacheable[static_cast<std::size_t>(Census::Uncacheable::NotRecordable)], what + ": the not-recordable lookups by condition do not add up to the not-recordable lookups");
    Require(frame.missBuilds == count(Census::Count::ResourceMisses) && frame.hitLookups == count(Census::Count::ResourceHits), what + ": the timed misses and hits differ from the counted ones");
    Require(frame.comparedRegisterDraws <= frame.shaderSetOnlyRepeats && frame.shaderSetOnlyRepeats <= frame.shaderSetRepeats, what + ": more register comparisons than shader-set-only repeats, or more of those than shader-set repeats");
    std::uint64_t slotted = 0;
    for (const auto& slot : frame.registerSlots) slotted += slot.second;
    Require(frame.registerSlots.size() <= 8 && slotted <= frame.differingRegisters, what + ": the top register slots count more than the differing registers");
}

std::uint64_t Total(const std::vector<Census::FrameSummary>& frames, const auto& field) {
    std::uint64_t total = 0;
    for (const auto& frame : frames) total += field(frame);
    return total;
}

}

int main(int argc, char** argv) {
    const std::string_view mode = argc > 1 ? argv[1] : "";
    const bool syncDraws = mode == "--sync-draws";
    const bool registers = mode == "--registers";
    try {
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
        }
        ColorMemory = AllocateWatched(Block);
        std::vector<std::byte> storage(CopyBytes * (MovedCopies + 2));
        auto* copies = reinterpret_cast<std::byte*>((reinterpret_cast<std::uintptr_t>(storage.data()) + CopyBytes - 1) & ~static_cast<std::uintptr_t>(CopyBytes - 1));
        for (std::uint32_t copy = 0; copy <= MovedCopies; ++copy) std::memcpy(copies + copy * CopyBytes, Vertices.data(), sizeof(Vertices));
        {
            GuestAllocations::Mutation mutation;
            mutation.Add(copies, CopyBytes * (MovedCopies + 1), true, true, true);
        }
        Register(VertexShader, VertexCode.data(), sizeof(VertexCode), 2);
        Register(PixelShader, PixelCode.data(), sizeof(PixelCode), 1);
        auto output = std::make_shared<Output>();
        AgcDriverRegisterVideoOutput_nid_postfix(OutputHandle, output);
        for (std::uint32_t frame = 0; frame < Frames; ++frame) {
            EndOfPipe = 0;
            auto words = Frame(copies, copies + (1u + frame % MovedCopies) * CopyBytes, registers && frame % 2u == 1u ? std::bit_cast<std::uint32_t>(0.25f) : 0u);
            Packet packet{words.data(), static_cast<std::uint32_t>(words.size()), 0, {}};
            Require(sceAgcDriverSubmitDcb(&packet) == 0, "draw key census: the frame submission failed");
            AgcDriverWaitIdle_nid_postfix();
            Require(EndOfPipe == 1u, "draw key census: the end-of-pipe label of frame " + std::to_string(frame) + " was not written");
        }
        const auto frames = Census::RecentFrames();
        AgcDriverUnregisterVideoOutput_nid_postfix(OutputHandle, output);
        AgcDriverShutdown_nid_postfix();
        {
            GuestAllocations::Mutation mutation;
            mutation.Remove(copies);
        }
        if (!Census::Enabled()) {
            Require(frames.empty(), "the draw key census recorded frames while APS5_DRAW_KEY_CENSUS was unset");
            std::puts("draw key census off: nothing recorded");
            return 0;
        }
        Require(frames.size() >= Frames, "the draw key census reported " + std::to_string(frames.size()) + " frames for " + std::to_string(Frames) + " flips with draws");
        for (const auto& frame : frames) CheckConsistent(frame);
        const auto& last = frames.back();
        const auto what = Describe(last);
        Require(last.flipped && last.keyed == 2, what + ": the last frame's two draws were not both keyed at a flip");
        if (registers) {
            Require(last.keyRepeats == 0 && last.shapeRepeats == 0 && last.shaderSetRepeats == 2, what + ": with the viewport Z offset changed, the draws repeated a key or a shape, or did not repeat their shader set");
            Require(last.shaderSetOnlyRepeats == 2 && last.comparedRegisterDraws == 2, what + ": the two draws were not compared as shader-set-only repeats");
            const bool zOffsetSlot = std::any_of(last.registerSlots.begin(), last.registerSlots.end(), [](const auto& slot) { return slot.first == ViewportZOffsetRegister && slot.second == 2; });
            Require(zOffsetSlot && last.differingRegisters == 2, what + ": the changed viewport Z offset was not the one register counted as differing in both draws");
            std::puts("draw key census register comparison passed");
            return 0;
        }
        Require(last.keyRepeats == 1 && last.shapeRepeats == 2, what + ": the unchanged draw did not repeat its key, or the moved one did not repeat its shape");
        Require(last.shapeOnlyRepeats == 1, what + ": the moved draw was not counted as a shape-only repeat");
        const bool movedSlot = std::any_of(last.slots.begin(), last.slots.end(), [](const auto& slot) { return slot.first == VertexBufferRegister && slot.second == 1; });
        Require(movedSlot, what + ": the moved vertex buffer's user-data slot was not the one counted as differing");
        Require(last.differingWords == 1 && last.addressLikeWords == 1, what + ": the moved draw's one differing word was not counted as one mapped address");
        Require(last.shaderSetOnlyRepeats == 0 && last.differingRegisters == 0, what + ": draws that repeat their shape were compared register by register");
        if (syncDraws) {
            const auto uncacheable = Total(frames, [](const Census::FrameSummary& frame) { return frame.counts[static_cast<std::size_t>(Census::Count::ResourceUncacheable)]; });
            const auto notRecordable = Total(frames, [](const Census::FrameSummary& frame) { return frame.uncacheable[static_cast<std::size_t>(Census::Uncacheable::NotRecordable)]; });
            const auto synchronous = Total(frames, [](const Census::FrameSummary& frame) { return frame.notRecordable[static_cast<std::size_t>(Census::NotRecordable::SyncDraws)]; });
            const auto cached = Total(frames, [](const Census::FrameSummary& frame) { return frame.hitLookups + frame.missBuilds; });
            const auto nanoseconds = Total(frames, [](const Census::FrameSummary& frame) { return frame.uncacheableNanoseconds[static_cast<std::size_t>(Census::Uncacheable::NotRecordable)]; });
            Require(uncacheable >= Frames * 2u && notRecordable == uncacheable && synchronous == uncacheable, "with APS5_SYNC_DRAWS, " + std::to_string(uncacheable) + " uncacheable lookups were counted, " + std::to_string(notRecordable) + " of them not recordable and " + std::to_string(synchronous) + " for synchronous draws");
            Require(cached == 0, "with APS5_SYNC_DRAWS, " + std::to_string(cached) + " resource lookups were counted as cache hits or misses");
            Require(nanoseconds > 0, "with APS5_SYNC_DRAWS, the not-recordable builds took no time");
            std::puts("draw key census uncacheable reasons passed");
            return 0;
        }
        std::puts("draw key census consistency passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        AgcDriverShutdown_nid_postfix();
        return 1;
    }
}
