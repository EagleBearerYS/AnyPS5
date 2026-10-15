#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Synchronization/SynchronizationStatistics.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/DccMetadata.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestTextureResource.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Texture.hpp"
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
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using AgcDriver::Graphics::DccKeys;
using AgcDriver::Graphics::Require;
using AgcDriver::Graphics::StorageTexture;

constexpr std::uint32_t Width = 256;
constexpr std::uint32_t Height = 128;
constexpr std::size_t Block = 65536;
constexpr std::size_t SurfaceBytes = std::size_t{Width} * Height * 4;
constexpr std::uint32_t Threads = 32;
constexpr std::uint32_t SetContextReg = 0x69u;
constexpr std::uint32_t SetShReg = 0x76u;
constexpr std::uint32_t SetUconfigReg = 0x79u;
constexpr std::uint32_t DrawIndexAuto = 0x2du;
constexpr std::uint32_t DispatchDirect = 0x15u;
constexpr std::uint32_t DmaData = 0x50u;
constexpr std::uint32_t Rgba8Info = 0x28028u;
constexpr std::uint32_t DccEnable = 0x10000000u;
constexpr std::uint32_t TiledAttrib3 = 0x4dc6c000u;
constexpr std::uint32_t Format8888Unorm = 56;
constexpr std::uint32_t TileRenderTarget64KB = 0x1b;
constexpr std::uint32_t Type2D = 9;
constexpr std::uint8_t Stored = 0x40;
constexpr std::uint32_t Drawn = 0x3f800000u;
constexpr std::uint32_t Cleared = 0u;
constexpr std::uint8_t Clear0000 = 0x00;

alignas(256) constexpr std::array<std::uint32_t, 6> VertexCode{
    0xe0382000u, 0x80020005u, 0xbf8c3f70u, 0xf80008cfu, 0x03020100u, 0xbf810000u,
};

alignas(256) constexpr std::array<std::uint32_t, 7> PixelCode{
    0x7e0002f2u, 0x7e0202f2u, 0x7e0402f2u, 0x7e0602f2u, 0xf800080fu, 0x03020100u, 0xbf810000u,
};

constexpr std::array<std::uint32_t, 11> SampleCode{
    0x34060084u, 0x7e2802ffu, 0x3cb00000u, 0x7e2a02ffu, 0x3c400000u, 0xf09c0f08u, 0x00610a14u, 0xbf8c3f70u,
    0xe0781000u, 0x80000a03u, 0xbf810000u,
};

constexpr std::array<std::uint32_t, 4> PointSampler{0x00000092u, 0x00fff000u, 0u, 0u};

constexpr std::array<std::array<float, 4>, 3> Triangle{{
    {-1.0f, -1.0f, 0.5f, 1.0f}, {3.0f, -1.0f, 0.5f, 1.0f}, {-1.0f, 3.0f, 0.5f, 1.0f},
}};

const std::vector<std::pair<std::uint32_t, std::uint32_t>> ContextBaseline{
    {0x2d5, 0x2000},
    {0x1b6, 0}, {0x200, 0}, {0x203, 0x800},
    {0x2dc, 0xaa00}, {0x2f8, 0}, {0x292, 2}, {0x293, 0},
    {0x80, 0}, {0x8d, 0}, {0x83, 0xffff}, {0x8c, 0xa},
    {0x2f9, 0x2d}, {0x313, 0x6000}, {0x30e, 0xffffffff}, {0x30f, 0xffffffff},
    {0x206, 0x43f}, {0x204, 0x80000}, {0x205, 0x240},
    {0x8e, 0xf}, {0x8f, 0xf}, {0x202, 0xcc0010},
    {0x1c4, 0}, {0x1c5, 9}, {0x1c3, 4}, {0x207, 0},
    {0x31b, 0}, {0x31d, 0}, {0x1e0, 0},
    {0xc, 0},
    {0x81, 0x80000000}, {0x90, 0x80000000}, {0x94, 0x80000000},
    {0xb4, 0}, {0xb5, std::bit_cast<std::uint32_t>(1.0f)}, {0x114, 0}, {0x113, std::bit_cast<std::uint32_t>(1.0f)},
};

std::uint64_t Address(const void* data) { return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(data)); }

std::uint8_t* NewBlock(std::size_t bytes) {
#ifdef _WIN32
    auto* block = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(bytes, Block));
    Require(block != nullptr, "keys clear over pending results: cannot reserve a guest block");
    const auto section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_EXECUTE_READWRITE, 0, static_cast<DWORD>(bytes), nullptr);
    Require(section != nullptr, "keys clear over pending results: cannot create a shared section");
    GuestArena::GuestArenaMap_nid_postfix(block, bytes, section, 0, PAGE_READWRITE);
    CloseHandle(section);
#else
    std::uint8_t* block = nullptr;
    void* mapped = mmap(nullptr, bytes + Block, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapped != MAP_FAILED) {
        const auto begin = reinterpret_cast<std::uintptr_t>(mapped);
        const auto aligned = (begin + Block - 1) & ~static_cast<std::uintptr_t>(Block - 1);
        if (aligned != begin) munmap(mapped, aligned - begin);
        if (aligned + bytes != begin + bytes + Block) munmap(reinterpret_cast<void*>(aligned + bytes), begin + Block - aligned);
        block = reinterpret_cast<std::uint8_t*>(aligned);
        GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(block, bytes);
    }
#endif
    Require(block != nullptr, "keys clear over pending results: cannot allocate a guest block");
    Require(AgcDriver::GuestMemory::Watched(Address(block), bytes), "keys clear over pending results: the guest block is not write-watched");
    GuestAllocations::Mutation().Add(block, bytes, true, true, true);
    return block;
}

struct RegisteredShader {
    Shader shader;
    ShaderUserData userData;
};
RegisteredShader VertexShader{};
RegisteredShader PixelShader{};

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

void SetRegister(std::vector<std::uint32_t>& words, std::uint32_t opcode, std::uint32_t offset, std::uint32_t value) {
    words.push_back(0xc0010000u | (opcode << 8u));
    words.push_back(offset);
    words.push_back(value);
}

void Submit(std::vector<std::uint32_t> words, const std::string& what) {
    Packet packet{words.data(), static_cast<std::uint32_t>(words.size()), 0, {}};
    Require(sceAgcDriverSubmitDcb(&packet) == 0, what + ": the DCB submission failed");
    AgcDriverWaitIdle_nid_postfix();
}

std::array<std::uint32_t, 4> BufferDescriptor(std::uint64_t address, std::uint32_t stride, std::uint32_t records, std::uint32_t format) {
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (stride << 16u), records, format};
}

struct Target {
    std::uint8_t* surface;
    std::uint8_t* keys;
    std::uint8_t* code;
    std::uint8_t* data;
    std::uint32_t* label;
    std::uint8_t* output;

    std::array<std::uint32_t, 8> Texture() const {
        const auto address = Address(surface);
        const auto meta = Address(keys) >> 8u;
        return {
            static_cast<std::uint32_t>(address >> 8u),
            static_cast<std::uint32_t>((address >> 40u) & 0xffu) | (Format8888Unorm << 20u) | (((Width - 1u) & 3u) << 30u),
            ((Width - 1u) >> 2u) | ((Height - 1u) << 14u),
            0xfacu | (TileRenderTarget64KB << 20u) | (Type2D << 28u),
            0u,
            0u,
            (1u << 20u) | (1u << 21u) | (static_cast<std::uint32_t>(meta & 0xffu) << 24u),
            static_cast<std::uint32_t>(meta >> 8u),
        };
    }

    std::size_t KeyExtent() const {
        const auto words = Texture();
        return AgcDriver::Graphics::DccKeyCount(AgcDriver::Graphics::DecodeTextureResource(words), SurfaceBytes);
    }
};

Target NewTarget() {
    Target target{NewBlock(SurfaceBytes), NewBlock(Block), NewBlock(Block), NewBlock(Block), nullptr, nullptr};
    std::memset(target.surface, Stored, SurfaceBytes);
    std::memset(target.keys, 0xff, Block);
    std::memcpy(target.code, SampleCode.data(), sizeof(SampleCode));
    std::memcpy(target.data, Triangle.data(), sizeof(Triangle));
    target.label = reinterpret_cast<std::uint32_t*>(target.data + 4096);
    target.output = target.data + 8192;
    return target;
}

void Draw(const Target& target, const std::string& what) {
    const auto surface = Address(target.surface);
    const auto keys = Address(target.keys);
    std::vector<std::uint32_t> words;
    for (const auto& [offset, value] : ContextBaseline) SetRegister(words, SetContextReg, offset, value);
    SetRegister(words, SetContextReg, 0x318, static_cast<std::uint32_t>(surface >> 8u));
    SetRegister(words, SetContextReg, 0x390, static_cast<std::uint32_t>(surface >> 40u));
    SetRegister(words, SetContextReg, 0x31c, Rgba8Info | DccEnable);
    SetRegister(words, SetContextReg, 0x3b0, ((Width - 1u) << 14u) | (Height - 1u));
    SetRegister(words, SetContextReg, 0x3b8, TiledAttrib3);
    SetRegister(words, SetContextReg, 0x325, static_cast<std::uint32_t>(keys >> 8u));
    SetRegister(words, SetContextReg, 0x3a8, static_cast<std::uint32_t>(keys >> 40u));
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
    const auto pixel = Address(PixelCode.data());
    const auto vertex = Address(VertexCode.data());
    SetRegister(words, SetShReg, 0x008, static_cast<std::uint32_t>(pixel >> 8u));
    SetRegister(words, SetShReg, 0x009, static_cast<std::uint32_t>(pixel >> 40u));
    SetRegister(words, SetShReg, 0x00b, 0u);
    SetRegister(words, SetShReg, 0x0c8, static_cast<std::uint32_t>(vertex >> 8u));
    SetRegister(words, SetShReg, 0x0c9, static_cast<std::uint32_t>(vertex >> 40u));
    SetRegister(words, SetShReg, 0x08b, 4u << 1u);
    const auto vertices = BufferDescriptor(Address(target.data), sizeof(Triangle[0]), static_cast<std::uint32_t>(Triangle.size()), 0x01016facu);
    for (std::uint32_t i = 0; i < vertices.size(); ++i) SetRegister(words, SetShReg, 0x08c + i, vertices[i]);
    words.insert(words.end(), {0xc0000000u | (1u << 16u) | (DrawIndexAuto << 8u), static_cast<std::uint32_t>(Triangle.size()), 2u});
    const auto label = Address(target.label);
    *target.label = 0;
    words.insert(words.end(), {0xc0064900u, 0x514u, (1u << 29u) | (2u << 24u), static_cast<std::uint32_t>(label), static_cast<std::uint32_t>(label >> 32u), 1u, 0u, 0u});
    Submit(std::move(words), what);
    std::uint32_t landed = 0;
    AgcDriver::GuestMemory::Read(label, std::as_writable_bytes(std::span(&landed, 1)));
    Require(landed == 1u, what + ": the draw's end-of-pipe label did not land");
}

void FillKeys(const Target& target, std::size_t bytes, std::uint8_t code, const std::string& what) {
    const auto keys = Address(target.keys);
    const auto pattern = code * 0x01010101u;
    Submit({0xc0000000u | (5u << 16u) | (DmaData << 8u), 0x40000000u, pattern, 0u, static_cast<std::uint32_t>(keys), static_cast<std::uint32_t>(keys >> 32u), static_cast<std::uint32_t>(bytes)}, what);
}

std::vector<std::uint32_t> Sample(const Target& target, const std::string& what) {
    constexpr std::size_t outputBytes = Threads * 16u;
    std::memset(target.output, 0xcd, outputBytes);
    std::array<std::uint32_t, 16> userData{};
    const auto output = BufferDescriptor(Address(target.output), 0u, outputBytes, 0x31016facu);
    const auto texture = target.Texture();
    std::copy(output.begin(), output.end(), userData.begin());
    std::copy(texture.begin(), texture.end(), userData.begin() + 4);
    std::copy(PointSampler.begin(), PointSampler.end(), userData.begin() + 12);
    std::vector<std::uint32_t> words;
    const auto code = Address(target.code);
    SetRegister(words, SetShReg, 0x20c, static_cast<std::uint32_t>(code >> 8u));
    SetRegister(words, SetShReg, 0x20d, static_cast<std::uint32_t>(code >> 40u) & 0xffu);
    SetRegister(words, SetShReg, 0x207, Threads);
    SetRegister(words, SetShReg, 0x208, 1u);
    SetRegister(words, SetShReg, 0x209, 1u);
    SetRegister(words, SetShReg, 0x213, static_cast<std::uint32_t>(userData.size()) << 1u);
    for (std::uint32_t i = 0; i < userData.size(); ++i) SetRegister(words, SetShReg, 0x240 + i, userData[i]);
    words.insert(words.end(), {0xc0000000u | (3u << 16u) | (DispatchDirect << 8u), 1u, 1u, 1u, 0x41u});
    Submit(std::move(words), what);
    std::vector<std::uint32_t> sampled(Threads * 4u);
    AgcDriver::GuestMemory::Read(Address(target.output), std::as_writable_bytes(std::span(sampled)));
    return sampled;
}

std::shared_ptr<StorageTexture> PendingImage(const Target& target) {
    std::lock_guard lock(AgcDriver::GuestMemory::GpuMutex());
    return StorageTexture::FindPending(Address(target.surface), SurfaceBytes);
}

std::uint64_t PacketStores() {
    using namespace AgcDriver::DriverDetail;
    return storesOnGpu.load() + storesBehindCompletions.load() + storesOnCpu.load();
}

bool Expect(const std::vector<std::uint32_t>& sampled, std::uint32_t expected, const std::string& what, const char* meaning) {
    std::size_t matching = 0;
    for (const auto value : sampled) matching += value == expected ? 1u : 0u;
    std::printf("%s: sampled 0x%08x (%zu of %zu channels 0x%08x, %s)\n", what.c_str(), sampled[0], matching, sampled.size(), expected, meaning);
    return matching == sampled.size();
}

std::shared_ptr<StorageTexture> DrawPending(const Target& target, const std::string& what, bool frameRead) {
    Draw(target, what + ": draw");
    auto image = PendingImage(target);
    Require(image != nullptr, what + ": the draw left no pending results at the target");
    {
        std::lock_guard lock(AgcDriver::GuestMemory::GpuMutex());
        Require(image->UploadedKeys() == DccKeys::Uncompressed, what + ": the draw was not made under uncompressed keys");
    }
    if (frameRead) {
        Require(Expect(Sample(target, what + ": frame's read"), Drawn, what + ": frame's read", "expected the drawn results"), what + ": the frame's read of its target did not see the draw");
        Require(PendingImage(target) == image, what + ": the frame's read stored the pending results");
    }
    return image;
}

bool DmaFillClears(const std::string& what, bool frameRead) {
    const auto target = NewTarget();
    const auto image = DrawPending(target, what, frameRead);
    const auto extent = target.KeyExtent();
    Require(extent <= Block && extent % 4 == 0, what + ": the key extent does not fit one DMA fill of at most 64 KiB");
    const auto storesBefore = PacketStores();
    const auto fillsBefore = AgcDriver::DriverDetail::dmaFills.load();
    FillKeys(target, extent, Clear0000, what + ": key fill");
    Require(PacketStores() == storesBefore + 1u && AgcDriver::DriverDetail::dmaFills.load() == fillsBefore, what + ": the key fill was not recorded as one packet store");
    Require(PendingImage(target) == image, what + ": the key fill ended the pending results");
    std::vector<std::uint8_t> keys(extent);
    AgcDriver::GuestMemory::Read(Address(target.keys), std::as_writable_bytes(std::span(keys)));
    Require(std::all_of(keys.begin(), keys.end(), [](std::uint8_t key) { return key == Clear0000; }), what + ": the key fill did not store the 0000 code");
    {
        std::lock_guard lock(AgcDriver::GuestMemory::GpuMutex());
        std::printf("%s: 0x%zx key bytes filled by a packet store over pending results; the image's noted key fill is %s\n", what.c_str(), extent, AgcDriver::Graphics::DccKeysName(image->FilledKeys()));
    }
    return Expect(Sample(target, what + ": sampled read"), Cleared, what, "expected the 0000 clear colour; 0x3f800000 is the drawn results, 0x3e808081 the bytes stored before the draw");
}

bool CpuStoreKeepsResults() {
    const std::string what = "cpu-store";
    const auto target = NewTarget();
    const auto image = DrawPending(target, what, false);
    std::memset(target.keys, Clear0000, target.KeyExtent());
    Require(PendingImage(target) == image, what + ": the key store ended the pending results");
    return Expect(Sample(target, what + ": sampled read"), Drawn, what, "expected the drawn results; 0 is the 0000 clear colour");
}

}

int main(int argc, char** argv) {
    const std::vector<std::pair<std::string_view, std::function<bool()>>> cases{
        {"dma-fill", [] { return DmaFillClears("dma-fill", false); }},
        {"dma-fill-after-read", [] { return DmaFillClears("dma-fill-after-read", true); }},
        {"cpu-store", CpuStoreKeepsResults},
    };
    const std::string_view only = argc > 1 ? argv[1] : "";
#ifndef _WIN32
    setenv("APS5_WRITE_WATCH_IMPORTS", "watch", 0);
#endif
    try {
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
        }
        Register(VertexShader, VertexCode.data(), sizeof(VertexCode), 2);
        Register(PixelShader, PixelCode.data(), sizeof(PixelCode), 1);
        std::size_t failed = 0;
        bool ran = false;
        for (const auto& [name, run] : cases) {
            if (!only.empty() && name != only) continue;
            ran = true;
            const bool passed = run();
            std::printf("%s %.*s\n", passed ? "PASS" : "FAIL", static_cast<int>(name.size()), name.data());
            failed += passed ? 0u : 1u;
        }
        Require(ran, "no test case is named " + std::string(only));
        AgcDriverShutdown_nid_postfix();
        if (failed != 0) {
            std::printf("keys clear over pending results: %zu case(s) failed\n", failed);
            return 1;
        }
        std::puts("keys clear over pending results tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        AgcDriverShutdown_nid_postfix();
        return 1;
    }
}
