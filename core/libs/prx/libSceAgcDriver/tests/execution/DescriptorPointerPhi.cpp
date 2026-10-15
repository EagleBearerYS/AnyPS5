#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "prx/libSceAgcDriver/Submit/include/Dcb.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

using AgcDriver::Graphics::Require;

constexpr std::uint32_t Threads = 64;
constexpr std::uint32_t Untouched = 0xcafebabeu;
constexpr std::uint32_t Format8888UNorm = 56;
constexpr std::uint32_t Format32Float = 22;
constexpr std::uint32_t Type2D = 9;
constexpr std::byte Kept{0x40};
constexpr std::string_view Poisoned = " after a loop back edge that left a lane mask in its descriptor or in the pointer it is read through; the read returned zero";

alignas(256) constexpr std::array<std::uint32_t, 17> EntryPixelCode{
    0x7e000280u, 0x7e020280u, 0xbe86047eu, 0xf40c0200u, 0xfa000000u, 0xd4c30000u, 0x00020080u, 0xbf8cc07fu,
    0xf0000f08u, 0x00020400u, 0x8afe007eu, 0xbf89fff7u, 0xbefe0406u, 0xbf8c3f70u, 0xf800180fu, 0x07060504u,
    0xbf810000u};
alignas(256) constexpr std::array<std::uint32_t, 22> PointerBackEdgeCode{
    0xf4080600u, 0xfa000040u, 0x7e020280u, 0x7e040280u, 0x7e060288u, 0xbe86047eu, 0xf40c0200u, 0xfa000000u,
    0xd4c30000u, 0x00020103u, 0x7e060280u, 0xbf8cc07fu, 0xf0000f08u, 0x00020401u, 0x8afe007eu, 0xbf89fff6u,
    0xbefe0406u, 0xbf8c3f70u, 0x34100084u, 0xe0781000u, 0x80060408u, 0xbf810000u};
alignas(256) constexpr std::array<std::uint32_t, 22> DescriptorBackEdgeCode{
    0xf4080600u, 0xfa000040u, 0xf40c0200u, 0xfa000000u, 0x7e020280u, 0x7e040280u, 0x7e060288u, 0xbe86047eu,
    0xbf8cc07fu, 0xf0000f08u, 0x00020401u, 0xd4c30008u, 0x00020103u, 0x7e060280u, 0x8afe087eu, 0xbf89fff9u,
    0xbefe0406u, 0xbf8c3f70u, 0x34100084u, 0xe0781000u, 0x80060408u, 0xbf810000u};
alignas(256) constexpr std::array<std::uint32_t, 24> TwoPointerCode{
    0xf4000400u, 0xfa000050u, 0xf4080600u, 0xfa000040u, 0xbf8cc07fu, 0xbf068010u, 0xbf850002u, 0xf4040000u,
    0xfa000058u, 0xbf8cc07fu, 0xf40c0200u, 0xfa000000u, 0xf4080500u, 0xfa000020u, 0x7e020280u, 0x7e040280u,
    0xbf8cc07fu, 0xf09c0f08u, 0x00a20401u, 0xbf8c3f70u, 0x34100084u, 0xe0781000u, 0x80060408u, 0xbf810000u};
alignas(256) constexpr std::array<std::uint32_t, 16> VertexCode{
    0x7e000d05u, 0x080200f4u, 0x10020300u, 0x100202f4u, 0x060202f3u, 0x060400f3u, 0x10040500u, 0x060404f3u,
    0x7e060280u, 0x7e0802f2u, 0xf80008cfu, 0x04030201u, 0xbf810000u};

alignas(256) std::array<std::uint32_t, 16> ColorTexels{};
alignas(256) std::array<float, 16> FirstTexels{};
alignas(256) std::array<float, 16> SecondTexels{};
alignas(256) std::array<std::uint32_t, 8> PixelSrt{};
alignas(256) std::array<std::uint32_t, 24> Srt{};
alignas(256) std::array<std::uint32_t, 12> Table{};
alignas(256) std::array<std::uint32_t, Threads * 4> Output{};
alignas(256) std::array<std::byte, 1024> Pixels{};

class StderrCapture {
public:
    StderrCapture() {
        std::fflush(stderr);
        file = std::tmpfile();
        Require(file != nullptr, "cannot open a temporary file for stderr");
#ifdef _WIN32
        saved = _dup(_fileno(stderr));
        _dup2(_fileno(file), _fileno(stderr));
#else
        saved = dup(fileno(stderr));
        dup2(fileno(file), fileno(stderr));
#endif
    }
    std::string Finish() {
        std::fflush(stderr);
#ifdef _WIN32
        _dup2(saved, _fileno(stderr));
        _close(saved);
#else
        dup2(saved, fileno(stderr));
        close(saved);
#endif
        std::rewind(file);
        std::string text;
        char buffer[4096];
        for (std::size_t read = 0; (read = std::fread(buffer, 1, sizeof(buffer), file)) != 0;) text.append(buffer, read);
        std::fclose(file);
        std::fwrite(text.data(), 1, text.size(), stderr);
        return text;
    }

private:
    std::FILE* file = nullptr;
    int saved = -1;
};

std::array<std::uint32_t, 8> Texture(const void* texels, std::uint32_t format) {
    const auto base = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(texels));
    return {static_cast<std::uint32_t>(base >> 8u), static_cast<std::uint32_t>((base >> 40u) & 0xffu) | (format << 20u) | (3u << 30u), (3u << 14u) | (1u << 31u), 0xfacu | (Type2D << 28u), 0u, 0u, 0u, 0u};
}

std::array<std::uint32_t, 4> Buffer(const void* base, std::uint32_t bytes) {
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(base));
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, 0x31016facu};
}

void Append(std::vector<std::uint32_t>& commands, std::uint32_t opcode, std::initializer_list<std::uint32_t> payload) {
    commands.push_back(0xc0000000u | (static_cast<std::uint32_t>(payload.size() - 1) << 16u) | (opcode << 8u));
    commands.insert(commands.end(), payload);
}

std::string Draw(const void* code) {
    ColorTexels.fill(0x44332211u);
    const auto texture = Texture(ColorTexels.data(), Format8888UNorm);
    std::copy(texture.begin(), texture.end(), PixelSrt.begin());
    Pixels.fill(Kept);
    const auto vertex = reinterpret_cast<std::uintptr_t>(VertexCode.data());
    const auto pixel = reinterpret_cast<std::uintptr_t>(code);
    const auto srt = reinterpret_cast<std::uintptr_t>(PixelSrt.data());
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> context{
        {0x2d5, 0x2000},
        {0x1b6, 0}, {0x207, 0}, {0x200, 0}, {0x203, 0x800},
        {0x2dc, 0xaa00}, {0x2f8, 0}, {0x292, 2}, {0x293, 0},
        {0x80, 0}, {0x8d, 0}, {0x83, 0xffff}, {0x8c, 0xa},
        {0x2f9, 0x2d}, {0x313, 0x6000}, {0x30e, 0xffffffff}, {0x30f, 0xffffffff},
        {0x206, 0x43f}, {0x204, 0x80000}, {0x205, 0x240},
        {0x8e, 0xf}, {0x8f, 0xf}, {0x202, 0xcc0010},
        {0x1c4, 0}, {0x1c5, 9}, {0x1c3, 4}, {0x31c, 0x28028},
        {0x31b, 0}, {0x31d, 0}, {0x3b0, (63u << 14u) | 3u},
        {0x3b8, 0x9000000}, {0x1e0, 0},
        {0xc, 0}, {0xd, 0x40040},
        {0x81, 0x80000000}, {0x82, 0x40040},
        {0x90, 0x80000000}, {0x91, 0x40040},
        {0x94, 0x80000000}, {0x95, 0x40040},
        {0x10f, std::bit_cast<std::uint32_t>(32.0f)}, {0x110, std::bit_cast<std::uint32_t>(32.0f)},
        {0x111, std::bit_cast<std::uint32_t>(-2.0f)}, {0x112, std::bit_cast<std::uint32_t>(2.0f)},
        {0x113, std::bit_cast<std::uint32_t>(1.0f)}, {0x114, 0},
        {0xb4, 0}, {0xb5, std::bit_cast<std::uint32_t>(1.0f)},
        {0x2fe, 0}, {0x302, 0}, {0x306, 0}, {0x30a, 0},
    };
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> shader{
        {0xc8, static_cast<std::uint32_t>(vertex >> 8u)}, {0xc9, static_cast<std::uint32_t>(vertex >> 40u)}, {0x8b, 0u},
        {0x008, static_cast<std::uint32_t>(pixel >> 8u)}, {0x009, static_cast<std::uint32_t>(pixel >> 40u)}, {0x00b, 2u << 1u},
        {0x00c, static_cast<std::uint32_t>(srt)}, {0x00d, static_cast<std::uint32_t>(srt >> 32u)},
    };
    std::vector<std::uint32_t> commands;
    Append(commands, 0x79, {0x242, 4});
    for (const auto& [offset, value] : context) Append(commands, 0x69, {offset, value});
    for (const auto& [offset, value] : shader) Append(commands, 0x76, {offset, value});
    const auto color = reinterpret_cast<std::uintptr_t>(Pixels.data());
    Append(commands, 0x69, {0x318, static_cast<std::uint32_t>(color >> 8u)});
    Append(commands, 0x69, {0x390, static_cast<std::uint32_t>(color >> 40u)});
    Append(commands, 0x2d, {3, 2});
    Packet packet{commands.data(), static_cast<std::uint32_t>(commands.size()), 0, {}};
    StderrCapture log;
    std::string failure;
    try {
        Require(sceAgcDriverSubmitDcb(&packet) == 0, "the waterfall draw was not submitted");
        AgcDriverWaitIdle_nid_postfix();
    } catch (const std::exception& error) {
        failure = error.what();
    }
    AgcDriver::GuestMemory::FlushGpuWrites(color, Pixels.size());
    return log.Finish() + failure;
}

void EntryArmDraw() {
    const auto log = Draw(EntryPixelCode.data());
    Require(log.find("is not a valid runtime value") == std::string::npos && log.find(Poisoned) == std::string::npos, "the waterfall draw whose lanes all leave on the entry edge failed: " + log);
    std::size_t texels = 0;
    for (std::size_t pixel = 0; pixel < Pixels.size() / 4u; ++pixel) {
        const auto* bytes = Pixels.data() + pixel * 4u;
        if (std::all_of(bytes, bytes + 4, [](std::byte value) { return value == Kept; })) continue;
        Require(bytes[0] == std::byte{0x11} && bytes[1] == std::byte{0x22} && bytes[2] == std::byte{0x33} && bytes[3] == std::byte{0x44}, "pixel " + std::to_string(pixel) + " of the waterfall draw is not the texel its entry edge's T# points at");
        ++texels;
    }
    Require(texels != 0 && texels != Pixels.size() / 4u, "the waterfall draw did not render the entry edge's texel on part of the target");
}

std::string Dispatch(const void* code) {
    Output.fill(Untouched);
    std::vector<std::uint32_t> words;
    const auto registers = [&](std::uint32_t first, std::initializer_list<std::uint32_t> values) {
        words.push_back(0xc0007600u | (static_cast<std::uint32_t>(values.size()) << 16u));
        words.push_back(first);
        words.insert(words.end(), values);
    };
    const auto address = reinterpret_cast<std::uintptr_t>(code);
    const auto srt = reinterpret_cast<std::uintptr_t>(Srt.data());
    registers(0x207, {Threads, 1, 1});
    registers(0x20c, {static_cast<std::uint32_t>(address >> 8u), static_cast<std::uint32_t>(address >> 40u)});
    registers(0x213, {2u << 1u});
    registers(0x240, {static_cast<std::uint32_t>(srt), static_cast<std::uint32_t>(srt >> 32u)});
    words.insert(words.end(), {0xc0031500u, 1, 1, 1, 0x41u});
    Packet packet{words.data(), static_cast<std::uint32_t>(words.size()), 0, {}};
    StderrCapture log;
    std::string failure;
    try {
        AgcDriver::Submit(&packet, 0);
        AgcDriverWaitIdle_nid_postfix();
    } catch (const std::exception& error) {
        failure = error.what();
    }
    AgcDriver::GuestMemory::FlushGpuWrites(reinterpret_cast<std::uintptr_t>(Output.data()), sizeof(Output));
    return log.Finish() + failure;
}

void FillSrt(std::uint32_t pointer) {
    FirstTexels.fill(3.0f);
    SecondTexels.fill(5.0f);
    const auto first = Texture(FirstTexels.data(), Format32Float);
    const auto second = Texture(SecondTexels.data(), Format32Float);
    const auto output = Buffer(Output.data(), static_cast<std::uint32_t>(sizeof(Output)));
    const std::array<std::uint32_t, 4> pointClamp{0x92u, (4u * 256u) << 12u, 0u, 0u};
    const auto table = reinterpret_cast<std::uintptr_t>(Table.data());
    Srt.fill(0u);
    std::copy(first.begin(), first.end(), Srt.begin());
    std::copy(pointClamp.begin(), pointClamp.end(), Srt.begin() + 8);
    std::copy(output.begin(), output.end(), Srt.begin() + 16);
    Srt[20] = pointer;
    Srt[22] = static_cast<std::uint32_t>(table);
    Srt[23] = static_cast<std::uint32_t>(table >> 32u);
    std::copy(second.begin(), second.end(), Table.begin());
    std::copy(pointClamp.begin(), pointClamp.end(), Table.begin() + 8);
}

void TwoPointers() {
    for (const auto [pointer, texel] : {std::pair{0u, 3.0f}, std::pair{1u, 5.0f}}) {
        FillSrt(pointer);
        const auto log = Dispatch(TwoPointerCode.data());
        Require(log.find("is not a valid runtime value") == std::string::npos && log.find("deferred write-back failed") == std::string::npos, "a T# read through either of two pointers failed: " + log);
        const std::array<std::uint32_t, 4> expected{std::bit_cast<std::uint32_t>(texel), 0u, 0u, std::bit_cast<std::uint32_t>(1.0f)};
        for (std::uint32_t lane = 0; lane < Threads; ++lane) {
            Require(std::equal(expected.begin(), expected.end(), Output.begin() + lane * 4u), std::string(pointer == 0u ? "the T# its user data points at" : "the T# a pointer loaded from the SRT points at") + ": lane " + std::to_string(lane) + " read " + std::to_string(Output[lane * 4u]) + " in its first component");
        }
    }
}

void BackEdgeDispatches() {
    FillSrt(0u);
    const auto pointer = Dispatch(PointerBackEdgeCode.data());
    Require(pointer.find("read an image at program counter 0x30" + std::string(Poisoned)) != std::string::npos, "lanes that read a T# through a ballot left in its pointer were not reported: " + pointer);
    const auto descriptor = Dispatch(DescriptorBackEdgeCode.data());
    Require(descriptor.find("read an image at program counter 0x24" + std::string(Poisoned)) != std::string::npos, "lanes that read a T# overwritten by a ballot were not reported: " + descriptor);
}

}

int main() {
    try {
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
        }
        EntryArmDraw();
        TwoPointers();
        BackEdgeDispatches();
        AgcDriverShutdown_nid_postfix();
        std::puts("descriptor pointer Phi tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        try {
            AgcDriverShutdown_nid_postfix();
        } catch (const std::exception&) {
        }
        return 1;
    }
}
