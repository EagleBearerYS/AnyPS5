#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/DccMetadata.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestTextureResource.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Texture.hpp"
#include "prx/libSceAgcDriver/Graphics/include/UnitShadow.hpp"
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
#include <chrono>
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

namespace AgcDriver {

struct VulkanDeviceTestAccess {
    static Graphics::Context ContextOf(const VulkanDevice& device) { return device.graphicsContext(); }
};

}

namespace {

auto DriverDeviceMember();
template <auto Member>
struct DriverDeviceReach {
    friend auto DriverDeviceMember() { return Member; }
};
template struct DriverDeviceReach<&AgcDriver::DriverDetail::Driver::device>;

using AgcDriver::Graphics::Require;
using AgcDriver::Graphics::StorageTexture;
using Clock = std::chrono::steady_clock;

constexpr std::size_t Block = 65536;
constexpr std::uint32_t SetContextReg = 0x69u;
constexpr std::uint32_t SetShReg = 0x76u;
constexpr std::uint32_t SetUconfigReg = 0x79u;
constexpr std::uint32_t DrawIndexAuto = 0x2du;
constexpr std::uint32_t Rgba8Info = 0x28028u;
constexpr std::uint32_t DccEnable = 0x10000000u;
constexpr std::uint32_t TiledAttrib3 = 0x4dc6c000u;
constexpr std::uint32_t Format8888Unorm = 56;
constexpr std::uint32_t TileRenderTarget64KB = 0x1b;
constexpr std::uint32_t Type2D = 9;
constexpr std::uint8_t Stored = 0x40;

alignas(256) constexpr std::array<std::uint32_t, 6> VertexCode{
    0xe0382000u, 0x80020005u, 0xbf8c3f70u, 0xf80008cfu, 0x03020100u, 0xbf810000u,
};

alignas(256) constexpr std::array<std::uint32_t, 7> PixelCode{
    0x7e0002f2u, 0x7e0202f2u, 0x7e0402f2u, 0x7e0602f2u, 0xf800080fu, 0x03020100u, 0xbf810000u,
};

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
    Require(block != nullptr, "resident target proof bench: cannot reserve a guest block");
    const auto section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_EXECUTE_READWRITE, 0, static_cast<DWORD>(bytes), nullptr);
    Require(section != nullptr, "resident target proof bench: cannot create a shared section");
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
    Require(block != nullptr, "resident target proof bench: cannot allocate a guest block");
    Require(AgcDriver::GuestMemory::Watched(Address(block), bytes), "resident target proof bench: the guest block is not write-watched");
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
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t* surface = nullptr;
    std::uint8_t* keys = nullptr;
    std::uint8_t* data = nullptr;
    std::uint32_t* label = nullptr;

    std::size_t Bytes() const { return std::size_t{width} * height * 4u; }

    std::array<std::uint32_t, 8> Texture() const {
        const auto address = Address(surface);
        const auto meta = Address(keys) >> 8u;
        return {
            static_cast<std::uint32_t>(address >> 8u),
            static_cast<std::uint32_t>((address >> 40u) & 0xffu) | (Format8888Unorm << 20u) | (((width - 1u) & 3u) << 30u),
            ((width - 1u) >> 2u) | ((height - 1u) << 14u),
            0xfacu | (TileRenderTarget64KB << 20u) | (Type2D << 28u),
            0u,
            0u,
            (1u << 20u) | (1u << 21u) | (static_cast<std::uint32_t>(meta & 0xffu) << 24u),
            static_cast<std::uint32_t>(meta >> 8u),
        };
    }

    std::size_t KeyBytes() const {
        const auto words = Texture();
        return AgcDriver::Graphics::DccKeyCount(AgcDriver::Graphics::DecodeTextureResource(words), Bytes());
    }
};

Target NewTarget(std::uint32_t width, std::uint32_t height) {
    Target target;
    target.width = width;
    target.height = height;
    target.surface = NewBlock(target.Bytes());
    const auto keyBlock = std::max(Block, (target.Bytes() / 256u + Block - 1u) / Block * Block);
    target.keys = NewBlock(keyBlock);
    target.data = NewBlock(Block);
    std::memset(target.surface, Stored, target.Bytes());
    std::memset(target.keys, 0xff, keyBlock);
    std::memcpy(target.data, Triangle.data(), sizeof(Triangle));
    target.label = reinterpret_cast<std::uint32_t*>(target.data + 4096);
    return target;
}

void Draw(const Target& target, const std::string& what) {
    const auto surface = Address(target.surface);
    const auto keys = Address(target.keys);
    const auto width = target.width;
    const auto height = target.height;
    std::vector<std::uint32_t> words;
    for (const auto& [offset, value] : ContextBaseline) SetRegister(words, SetContextReg, offset, value);
    SetRegister(words, SetContextReg, 0x318, static_cast<std::uint32_t>(surface >> 8u));
    SetRegister(words, SetContextReg, 0x390, static_cast<std::uint32_t>(surface >> 40u));
    SetRegister(words, SetContextReg, 0x31c, Rgba8Info | DccEnable);
    SetRegister(words, SetContextReg, 0x3b0, ((width - 1u) << 14u) | (height - 1u));
    SetRegister(words, SetContextReg, 0x3b8, TiledAttrib3);
    SetRegister(words, SetContextReg, 0x325, static_cast<std::uint32_t>(keys >> 8u));
    SetRegister(words, SetContextReg, 0x3a8, static_cast<std::uint32_t>(keys >> 40u));
    SetRegister(words, SetContextReg, 0x10f, std::bit_cast<std::uint32_t>(width / 2.0f));
    SetRegister(words, SetContextReg, 0x110, std::bit_cast<std::uint32_t>(width / 2.0f));
    SetRegister(words, SetContextReg, 0x111, std::bit_cast<std::uint32_t>(-(height / 2.0f)));
    SetRegister(words, SetContextReg, 0x112, std::bit_cast<std::uint32_t>(height / 2.0f));
    const auto extent = (height << 16u) | width;
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

AgcDriver::Graphics::Context DriverContext() {
    const auto device = (AgcDriver::DriverDetail::Driver::Get().*DriverDeviceMember()).Load();
    Require(device != nullptr, "resident target proof bench: the driver has no device");
    return AgcDriver::VulkanDeviceTestAccess::ContextOf(*device);
}

struct Row {
    double p50 = 0;
    double p90 = 0;
    double min = 0;
    double pages = 0;
};

Row Measure(std::size_t iterations, bool newEpoch, const std::function<void()>& step) {
    std::vector<double> samples;
    samples.reserve(iterations);
    const auto walkedBefore = AgcDriver::GuestMemory::ThreadCollectedBytes();
    for (std::size_t i = 0; i < iterations; ++i) {
        if (newEpoch) AgcDriver::GuestMemory::BumpCollectEpoch();
        const auto start = Clock::now();
        step();
        samples.push_back(std::chrono::duration<double, std::micro>(Clock::now() - start).count());
    }
    const auto walked = AgcDriver::GuestMemory::ThreadCollectedBytes() - walkedBefore;
    std::sort(samples.begin(), samples.end());
    Row row;
    row.p50 = samples[samples.size() / 2];
    row.p90 = samples[std::min(samples.size() - 1, samples.size() * 9 / 10)];
    row.min = samples.front();
    row.pages = static_cast<double>(walked) / 4096.0 / static_cast<double>(iterations);
    return row;
}

void Print(const char* name, const Row& row) {
    std::printf("  %-58s p50 %9.2f us  p90 %9.2f us  min %9.2f us  pages/call %8.1f\n", name, row.p50, row.p90, row.min, row.pages);
}

void BenchTarget(std::uint32_t width, std::uint32_t height, std::size_t iterations) {
    auto target = NewTarget(width, height);
    Draw(target, "bench draw");
    const auto context = DriverContext();
    const auto words = target.Texture();
    const auto resource = AgcDriver::Graphics::DecodeTextureResource(words);
    const auto surface = Address(target.surface);
    const auto bytes = target.Bytes();
    const auto keys = Address(target.keys);
    const auto keyBytes = target.KeyBytes();
    std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
    const auto pending = StorageTexture::FindPending(surface, bytes);
    const auto image = AgcDriver::Graphics::CachedStorageSurface(context, resource);
    Require(image != nullptr, "resident target proof bench: no storage image for the target");
    const auto guestBytes = image->GuestBytes();
    std::printf("target %ux%u, %zu KiB (%zu pages), keys %zu bytes at 0x%llx; the draw's pending image is %s the lookup's image; proved refreshes so far %llu\n", width, height, bytes / 1024u, bytes / 4096u, keyBytes, static_cast<unsigned long long>(keys), pending == image ? "" : "NOT", static_cast<unsigned long long>(StorageTexture::RefreshesProved()));
    for (int warm = 0; warm < 3; ++warm) image->Refresh();
    const auto provedBefore = StorageTexture::RefreshesProved();
    Print("lookup (CachedStorageSurface), a new epoch each", Measure(iterations, true, [&] { static_cast<void>(AgcDriver::Graphics::CachedStorageSurface(context, resource)); }));
    Print("lookup (CachedStorageSurface), one epoch", Measure(iterations, false, [&] { static_cast<void>(AgcDriver::Graphics::CachedStorageSurface(context, resource)); }));
    Print("StorageTexture::Refresh, a new epoch each", Measure(iterations, true, [&] { image->Refresh(); }));
    Print("StorageTexture::Refresh, one epoch", Measure(iterations, false, [&] { image->Refresh(); }));
    std::printf("  proved refreshes in these %zu lookups and refreshes: %llu\n", iterations * 4, static_cast<unsigned long long>(StorageTexture::RefreshesProved() - provedBefore));
    Print("CompletePendingCpuWrites (surface, then keys)", Measure(iterations, true, [&] {
        AgcDriver::Graphics::CompletePendingCpuWrites(context, surface, bytes);
        AgcDriver::Graphics::CompletePendingCpuWrites(context, keys, keyBytes);
    }));
    std::uint64_t generation = 0;
    Print("CollectWrites over the surface, a new epoch each", Measure(iterations, true, [&] { generation = AgcDriver::GuestMemory::CollectWrites(surface, bytes); }));
    Print("CollectWrites over the surface, one epoch", Measure(iterations, false, [&] { generation = AgcDriver::GuestMemory::CollectWrites(surface, bytes); }));
    Print("CollectWrites over the keys, a new epoch each", Measure(iterations, true, [&] { static_cast<void>(AgcDriver::GuestMemory::CollectWrites(keys, keyBytes)); }));
    const std::array<AgcDriver::GuestMemory::UnchangedQuery, 2> queries{{{surface, bytes, generation}, {keys, keyBytes, generation}}};
    bool unchanged = true;
    Print("UnchangedSinceAll (surface and keys)", Measure(iterations, false, [&] { unchanged = AgcDriver::GuestMemory::UnchangedSinceAll(queries) && unchanged; }));
    std::printf("  unchanged since the collect: %s\n", unchanged ? "yes" : "no");
    Print("TextureClearKeys (the DCC key scan)", Measure(iterations, false, [&] { static_cast<void>(AgcDriver::Graphics::TextureClearKeys(resource, guestBytes)); }));
    AgcDriver::Graphics::DccKeyProof proof{};
    static_cast<void>(AgcDriver::Graphics::ProvedClearKeys(resource, guestBytes, proof));
    Print("ProvedClearKeys with a held proof, a new epoch each", Measure(iterations, true, [&] { static_cast<void>(AgcDriver::Graphics::ProvedClearKeys(resource, guestBytes, proof)); }));
    const bool stored = StorageTexture::FlushPending(surface, bytes, nullptr, "bench", AgcDriver::Graphics::PublishScope::None);
    std::printf("  the first FlushPending stored the draw's results: %s\n", stored ? "yes" : "no");
    Print("FlushPending with nothing pending", Measure(iterations, false, [&] { static_cast<void>(StorageTexture::FlushPending(surface, bytes, nullptr, "bench", AgcDriver::Graphics::PublishScope::None)); }));
    Print("StorageTexture::Refresh after the write-back, a new epoch each", Measure(iterations, true, [&] { image->Refresh(); }));
}

}

int main(int argc, char** argv) {
    const std::string_view mode = argc > 1 ? argv[1] : "";
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
        if (mode == "--bench") {
            const std::size_t iterations = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 200;
            BenchTarget(1024, 1024, iterations);
            BenchTarget(2048, 1024, iterations);
        } else if (mode == "--draws") {
            const double seconds = argc > 2 ? std::strtod(argv[2], nullptr) : 21.0;
            const std::uint32_t width = argc > 3 ? static_cast<std::uint32_t>(std::strtoul(argv[3], nullptr, 10)) : 1024;
            const std::uint32_t height = argc > 4 ? static_cast<std::uint32_t>(std::strtoul(argv[4], nullptr, 10)) : 1024;
            auto target = NewTarget(width, height);
            const auto start = Clock::now();
            std::size_t draws = 0;
            while (std::chrono::duration<double>(Clock::now() - start).count() < seconds) {
                Draw(target, "bench draw");
                ++draws;
            }
            std::printf("%zu draws of a %ux%u target in %.1f s\n", draws, width, height, std::chrono::duration<double>(Clock::now() - start).count());
        } else {
            BenchTarget(256, 128, 3);
        }
        AgcDriverShutdown_nid_postfix();
        std::puts("resident target proof bench done");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        AgcDriverShutdown_nid_postfix();
        return 1;
    }
}
