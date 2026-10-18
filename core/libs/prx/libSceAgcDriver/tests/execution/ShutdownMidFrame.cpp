#define SDL_MAIN_HANDLED
#include "prx/libSceAgcDriver/Execution/include/DisplayBuffer.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Presentation.hpp"
#include "prx/libSceAgcDriver/Submit/include/Dcb.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libc/include/GuestWriteWatch.hpp"
#include "prx/libc/include/Shutdown.hpp"
#include "VulkanTestDevice.hpp"
#include <SDL.h>
#include <vulkan/vulkan.h>
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
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;

constexpr std::uint32_t Width = 64;
constexpr std::uint32_t Height = 16;
constexpr std::uint32_t SetContextReg = 0x69u;
constexpr std::uint32_t SetShReg = 0x76u;
constexpr std::uint32_t SetUconfigReg = 0x79u;
constexpr std::uint32_t DrawIndexAuto = 0x2du;
constexpr std::size_t Block = 65536;
constexpr std::uint32_t DrawsPerFrame = 16;
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

alignas(256) constexpr std::array<std::array<float, 4>, 3> Vertices{{
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

std::vector<std::uint32_t> Frame(bool label) {
    const auto colorAddress = reinterpret_cast<std::uintptr_t>(ColorMemory);
    const auto vertexAddress = reinterpret_cast<std::uintptr_t>(VertexCode.data());
    const auto pixelAddress = reinterpret_cast<std::uintptr_t>(PixelCode.data());
    std::vector<std::uint32_t> words;
    for (const auto& [offset, value] : ContextBaseline) SetRegister(words, SetContextReg, offset, value);
    SetRegister(words, SetContextReg, 0x203, 0x800u);
    SetRegister(words, SetContextReg, 0x30e, 0xffffffffu);
    SetRegister(words, SetContextReg, 0x30f, 0xffffffffu);
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
    SetRegister(words, SetShReg, 0x08b, 4u << 1u);
    const auto descriptor = BufferDescriptor(Vertices.data(), 16u, static_cast<std::uint32_t>(Vertices.size()));
    for (std::uint32_t i = 0; i < descriptor.size(); ++i) SetRegister(words, SetShReg, 0x08c + i, descriptor[i]);
    for (std::uint32_t draw = 0; draw < DrawsPerFrame; ++draw) words.insert(words.end(), {0xc0000000u | (1u << 16u) | (DrawIndexAuto << 8u), static_cast<std::uint32_t>(Vertices.size()), 2u});
    if (label) {
        const auto labelAddress = reinterpret_cast<std::uintptr_t>(&EndOfPipe);
        words.insert(words.end(), {0xc0064900u, 0x514u, (1u << 29u) | (2u << 24u), static_cast<std::uint32_t>(labelAddress), static_cast<std::uint32_t>(static_cast<std::uint64_t>(labelAddress) >> 32u), 1u, 0u, 0u});
    }
    return words;
}

}

int WindowContext = 0;

VkSurfaceKHR CreateHeadlessSurface(void*, VkInstance instance) {
#ifdef _WIN32
    void* library = SDL_LoadObject("vulkan-1.dll");
#else
    void* library = SDL_LoadObject("libvulkan.so.1");
#endif
    Require(library != nullptr, "cannot load the Vulkan loader");
    const auto instanceProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_LoadFunction(library, "vkGetInstanceProcAddr"));
    const auto create = reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(instanceProc(instance, "vkCreateHeadlessSurfaceEXT"));
    Require(create != nullptr, "vkCreateHeadlessSurfaceEXT is missing");
    VkHeadlessSurfaceCreateInfoEXT info{VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT};
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    Require(create(instance, &info, nullptr, &surface) == VK_SUCCESS, "vkCreateHeadlessSurfaceEXT failed");
    return surface;
}

void DrawableSize(void*, std::uint32_t* width, std::uint32_t* height) {
    *width = Width;
    *height = Height;
}

constexpr std::array<const char*, 2> WindowExtensions{VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME};

struct alignas(256) CodeCopy {
    std::array<std::uint32_t, 7> words;
};

int main(int argc, char** argv) {
    const auto settle = std::chrono::milliseconds(argc > 1 ? std::atoi(argv[1]) : 0);
    const bool registering = argc > 2 && std::strcmp(argv[2], "--register") == 0;
    const bool windowed = argc > 2 && std::strcmp(argv[2], "--window") == 0;
    try {
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
        }
        ColorMemory = AllocateWatched(Block);
        Register(VertexShader, VertexCode.data(), sizeof(VertexCode), 2);
        Register(PixelShader, PixelCode.data(), sizeof(PixelCode), 1);
        auto labelled = Frame(true);
        auto open = Frame(false);
        Packet first{labelled.data(), static_cast<std::uint32_t>(labelled.size()), 0, {}};
        Require(sceAgcDriverSubmitDcb(&first) == 0, "the first frame was not accepted");
        AgcDriverWaitIdle_nid_postfix();
        Require(EndOfPipe == 1u, "the first frame did not reach its end-of-pipe label");
        std::atomic<bool> stop{false};
        std::atomic<bool> shutDown{false};
        std::atomic<std::uint64_t> accepted{0};
        std::atomic<std::uint64_t> refused{0};
        std::atomic<std::uint64_t> afterShutdown{0};
        std::atomic<std::uint64_t> registered{0};
        std::atomic<std::uint64_t> registrationsRefused{0};
        std::atomic<std::uint64_t> registeredAfterShutdown{0};
        std::deque<RegisteredShader> shaders;
        std::deque<CodeCopy> codes;
        std::thread registrar;
        if (registering) {
            registrar = std::thread([&] {
                std::uint32_t variant = 0;
                while (!stop.load()) {
                    auto& code = codes.emplace_back();
                    code.words = PixelCode;
                    code.words[0] = 0x7e000280u + (variant % 64u);
                    ++variant;
                    auto& shader = shaders.emplace_back();
                    try {
                        Register(shader, code.words.data(), sizeof(code.words), 1);
                        registered.fetch_add(1);
                        if (shutDown.load()) registeredAfterShutdown.fetch_add(1);
                    } catch (const ProcessShutdown&) {
                        registrationsRefused.fetch_add(1);
                    } catch (const std::exception& error) {
                        std::fprintf(stderr, "registrar: %s\n", error.what());
                        registrationsRefused.fetch_add(1);
                    }
                }
            });
            while (registered.load() < 8) std::this_thread::yield();
        }
        std::thread submitter([&] {
            while (!stop.load()) {
                Packet packet{open.data(), static_cast<std::uint32_t>(open.size()), 0, {}};
                try {
                    sceAgcDriverSubmitDcb(&packet);
                    accepted.fetch_add(1);
                    if (shutDown.load()) afterShutdown.fetch_add(1);
                } catch (const ProcessShutdown&) {
                    refused.fetch_add(1);
                } catch (const std::exception& error) {
                    std::fprintf(stderr, "submitter: %s\n", error.what());
                    refused.fetch_add(1);
                    return;
                }
            }
        });
        if (windowed) {
            std::atomic<bool> presenting{true};
            std::atomic<std::uint64_t> presents{0};
            std::atomic<std::uint64_t> ready{0};
            std::atomic<bool> presenterDone{false};
            std::thread presenter([&] {
                try {
                    const AgcDriver::PresentationWindow window{&WindowContext, WindowExtensions, &CreateHeadlessSurface, &DrawableSize, Width, Height, nullptr};
                    AgcDriverAttachWindow_nid_postfix(window);
                    const AgcDriver::DisplayBuffer display{static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(ColorMemory)), 0x8000000022000000ull, Width, Height, 1u, Width};
                    while (presenting.load()) {
                        AgcDriverPresentBuffer_nid_postfix(window, display, [](void* context) { static_cast<std::atomic<std::uint64_t>*>(context)->fetch_add(1); }, &ready);
                        presents.fetch_add(1);
                    }
                    std::fprintf(stderr, "[probe] shutdown from the presenting thread after %llu presents (%llu GPU-ready) and %llu open frames (%u draws each)\n", static_cast<unsigned long long>(presents.load()), static_cast<unsigned long long>(ready.load()), static_cast<unsigned long long>(accepted.load()), DrawsPerFrame);
                    AgcDriverShutdown_nid_postfix();
                } catch (const ProcessShutdown&) {
                    std::fprintf(stderr, "[probe] the presenting thread saw the shutdown\n");
                } catch (const std::exception& error) {
                    std::fprintf(stderr, "presenter: %s\n", error.what());
                }
                shutDown.store(true);
                presenterDone.store(true);
            });
            while ((presents.load() < 16 || accepted.load() < 64) && !presenterDone.load()) std::this_thread::yield();
            std::this_thread::sleep_for(settle);
            presenting.store(false);
            presenter.join();
        } else {
            while (accepted.load() < 64) std::this_thread::yield();
            std::this_thread::sleep_for(settle);
            std::fprintf(stderr, "[probe] shutdown from the main thread after %llu open frames (%u draws each)\n", static_cast<unsigned long long>(accepted.load()), DrawsPerFrame);
            AgcDriverShutdown_nid_postfix();
            shutDown.store(true);
        }
        std::fprintf(stderr, "[probe] shutdown returned\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        stop.store(true);
        submitter.join();
        if (registrar.joinable()) registrar.join();
        if (registering) std::fprintf(stderr, "[probe] %llu shaders registered, %llu after the shutdown returned, %llu refused\n", static_cast<unsigned long long>(registered.load()), static_cast<unsigned long long>(registeredAfterShutdown.load()), static_cast<unsigned long long>(registrationsRefused.load()));
        std::fprintf(stderr, "[probe] %llu frames accepted, %llu after the shutdown returned, %llu refused\n", static_cast<unsigned long long>(accepted.load()), static_cast<unsigned long long>(afterShutdown.load()), static_cast<unsigned long long>(refused.load()));
        std::puts("shutdown mid-frame probe finished");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        AgcDriverShutdown_nid_postfix();
        return 1;
    }
}
