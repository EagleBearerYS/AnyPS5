#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "VulkanTestDevice.hpp"
#include "GuestTestMemory.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;

constexpr std::uint32_t Threads = 64;
constexpr std::uint32_t Width = 64;
constexpr std::uint32_t Height = 4;
constexpr std::uint32_t Format32UInt = 20;
constexpr std::uint32_t Type2D = 9;
constexpr std::uint32_t Untouched = 0xcafebabeu;

alignas(256) constexpr std::array<std::uint32_t, 25> Caller{
    0x36020083u, 0xbe9e047eu, 0x7e280501u, 0x7d840214u, 0xbea0246au, 0x8f148414u, 0xf4080400u, 0xfa000000u,
    0xbf8cc07fu, 0xf4280208u, 0x28000000u, 0xbf8cc07fu, 0xbe8e0308u, 0xbe8f0309u, 0xbe8e210eu, 0x8afe7e20u,
    0xbf89fff1u, 0xbefe041eu, 0xf4080600u, 0xfa000010u, 0x34060082u, 0xbf8cc07fu, 0xe0701000u, 0x80060203u,
    0xbf810000u};
alignas(256) constexpr std::array<std::uint32_t, 9> LoadThroughArgument{
    0xf40c0905u, 0xfa000000u, 0xbf8cc07fu, 0x7e0a0300u, 0x7e0c0280u, 0xf0000108u, 0x00090205u, 0xbf8c3f70u,
    0xbe80200eu};
TestGuest::Array<std::uint32_t, 9> MovedLoadThroughArgument;
alignas(256) constexpr std::array<std::uint32_t, 12> LoadThroughArgumentBuffer{
    0xf4080b05u, 0xfa000020u, 0xbf8cc07fu, 0xf42c0916u, 0xfa000020u, 0xbf8cc07fu, 0x7e0a0300u, 0x7e0c0280u,
    0xf0000108u, 0x00090205u, 0xbf8c3f70u, 0xbe80200eu};
TestGuest::Array<std::uint32_t, Width * Height> FirstTexels;
TestGuest::Array<std::uint32_t, Width * Height> SecondTexels;
TestGuest::Array<std::uint32_t, Width * Height> ThirdTexels;
TestGuest::Array<std::uint32_t, 12> FirstArgument;
TestGuest::Array<std::uint32_t, 12> SecondArgument;
TestGuest::Array<std::uint32_t, 12> ThirdArgument;
TestGuest::Array<std::uint32_t, 16> FirstHeap;
TestGuest::Array<std::uint32_t, 16> SecondHeap;
TestGuest::Array<std::uint32_t, 16> ThirdHeap;
TestGuest::Array<std::uint32_t, 16> Table;
TestGuest::Array<std::uint32_t, 8> Srt;
TestGuest::Array<std::uint32_t, Threads> Output;

std::array<std::uint32_t, 4> Image(const void* texels) {
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(texels));
    return {static_cast<std::uint32_t>(address >> 8u), static_cast<std::uint32_t>((address >> 40u) & 0xffu) | (Format32UInt << 20u) | (((Width - 1u) & 3u) << 30u), ((Width - 1u) >> 2u) | ((Height - 1u) << 14u) | (1u << 31u), 0xfacu | (Type2D << 28u)};
}

std::array<std::uint32_t, 4> Buffer(const void* base, std::uint32_t bytes) {
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(base));
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), bytes, 0x31016facu};
}

void SetRecord(std::uint32_t record, const void* target, const void* argument) {
    const auto code = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(target));
    const auto data = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(argument));
    Table[record * 4u] = static_cast<std::uint32_t>(code);
    Table[record * 4u + 1u] = static_cast<std::uint32_t>(code >> 32u);
    Table[record * 4u + 2u] = static_cast<std::uint32_t>(data);
    Table[record * 4u + 3u] = static_cast<std::uint32_t>(data >> 32u);
}

std::vector<std::uint32_t> Commands() {
    std::vector<std::uint32_t> words;
    const auto registers = [&](std::uint32_t first, std::span<const std::uint32_t> values) {
        words.push_back(0xc0007600u | (static_cast<std::uint32_t>(values.size()) << 16u));
        words.push_back(first);
        words.insert(words.end(), values.begin(), values.end());
    };
    const std::array<std::uint32_t, 3> shape{Threads, 1, 1};
    registers(0x207, shape);
    const auto address = reinterpret_cast<std::uintptr_t>(Caller.data());
    const std::array<std::uint32_t, 2> program{static_cast<std::uint32_t>(address >> 8u), static_cast<std::uint32_t>(address >> 40u)};
    registers(0x20c, program);
    const std::array<std::uint32_t, 1> resource{2u << 1u};
    registers(0x213, resource);
    const auto srt = reinterpret_cast<std::uintptr_t>(Srt.data());
    const std::array<std::uint32_t, 2> data{static_cast<std::uint32_t>(srt), static_cast<std::uint32_t>(srt >> 32u)};
    registers(0x240, data);
    words.insert(words.end(), {0xc0031500u, 1, 1, 1, 0x41u});
    return words;
}

void Dispatch() {
    Output.fill(Untouched);
    const auto words = Commands();
    Packet packet{const_cast<std::uint32_t*>(words.data()), static_cast<std::uint32_t>(words.size()), 0, {}};
    AgcDriver::Submit(&packet, 0);
    AgcDriverWaitIdle_nid_postfix();
    AgcDriver::GuestMemory::FlushGpuWrites(reinterpret_cast<std::uintptr_t>(Output.data()), decltype(Output)::ByteSize);
}

void Check(const std::array<std::uint32_t, Width * Height>& even, const std::array<std::uint32_t, Width * Height>& odd, const std::string& what) {
    Dispatch();
    for (std::uint32_t lane = 0; lane < Threads; ++lane) {
        const auto expected = (lane & 1u) != 0u ? odd[lane] : even[lane];
        Require(Output[lane] == expected, what + ": lane " + std::to_string(lane) + " is " + std::to_string(Output[lane]) + ", expected " + std::to_string(expected));
    }
}

}

int main() {
    try {
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
        }
        for (std::uint32_t texel = 0; texel < Width * Height; ++texel) {
            FirstTexels[texel] = 0x1000u + texel;
            SecondTexels[texel] = 0x2000u + texel;
            ThirdTexels[texel] = 0x3000u + texel;
        }
        {
            GuestAllocations::Mutation mutation;
            mutation.Add(FirstTexels.data(), decltype(FirstTexels)::ByteSize, true, true, true);
            mutation.Add(SecondTexels.data(), decltype(SecondTexels)::ByteSize, true, true, true);
            mutation.Add(ThirdTexels.data(), decltype(ThirdTexels)::ByteSize, true, true, true);
        }
        const auto first = Image(FirstTexels.data());
        const auto second = Image(SecondTexels.data());
        const auto third = Image(ThirdTexels.data());
        std::copy(first.begin(), first.end(), FirstArgument.begin());
        std::copy(second.begin(), second.end(), SecondArgument.begin());
        std::copy(third.begin(), third.end(), ThirdArgument.begin());
        std::copy(first.begin(), first.end(), FirstHeap.begin() + 8);
        std::copy(second.begin(), second.end(), SecondHeap.begin() + 8);
        std::copy(third.begin(), third.end(), ThirdHeap.begin() + 8);
        const auto firstHeap = Buffer(FirstHeap.data(), static_cast<std::uint32_t>(decltype(FirstHeap)::ByteSize));
        const auto secondHeap = Buffer(SecondHeap.data(), static_cast<std::uint32_t>(decltype(SecondHeap)::ByteSize));
        const auto thirdHeap = Buffer(ThirdHeap.data(), static_cast<std::uint32_t>(decltype(ThirdHeap)::ByteSize));
        std::copy(firstHeap.begin(), firstHeap.end(), FirstArgument.begin() + 8);
        std::copy(secondHeap.begin(), secondHeap.end(), SecondArgument.begin() + 8);
        std::copy(thirdHeap.begin(), thirdHeap.end(), ThirdArgument.begin() + 8);
        const auto table = Buffer(Table.data(), static_cast<std::uint32_t>(decltype(Table)::ByteSize));
        const auto output = Buffer(Output.data(), static_cast<std::uint32_t>(decltype(Output)::ByteSize));
        std::copy(table.begin(), table.end(), Srt.begin());
        std::copy(output.begin(), output.end(), Srt.begin() + 4);
        SetRecord(0, LoadThroughArgumentBuffer.data(), FirstArgument.data());
        SetRecord(1, LoadThroughArgumentBuffer.data(), SecondArgument.data());
        SetRecord(2, LoadThroughArgument.data(), FirstArgument.data());
        SetRecord(3, LoadThroughArgument.data(), SecondArgument.data());
        Check(FirstTexels, SecondTexels, "two callees, each called through two records whose arguments lead to different T#s");
        SetRecord(0, LoadThroughArgumentBuffer.data(), SecondArgument.data());
        SetRecord(1, LoadThroughArgumentBuffer.data(), FirstArgument.data());
        SetRecord(2, LoadThroughArgument.data(), SecondArgument.data());
        SetRecord(3, LoadThroughArgument.data(), FirstArgument.data());
        Check(SecondTexels, FirstTexels, "the records' arguments swapped");
        SetRecord(1, LoadThroughArgumentBuffer.data(), ThirdArgument.data());
        SetRecord(3, LoadThroughArgument.data(), ThirdArgument.data());
        Check(SecondTexels, ThirdTexels, "two records given an argument no copy was linked for");
        std::copy(LoadThroughArgument.begin(), LoadThroughArgument.end(), MovedLoadThroughArgument.begin());
        SetRecord(2, MovedLoadThroughArgument.data(), SecondArgument.data());
        SetRecord(3, MovedLoadThroughArgument.data(), ThirdArgument.data());
        Check(SecondTexels, ThirdTexels, "two records whose callee's code moved to another address");
        {
            GuestAllocations::Mutation mutation;
            mutation.Remove(FirstTexels.data());
            mutation.Remove(SecondTexels.data());
            mutation.Remove(ThirdTexels.data());
        }
        AgcDriverShutdown_nid_postfix();
        std::puts("compute table call tests passed");
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
