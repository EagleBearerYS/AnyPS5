#include "GuestTestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "VulkanTestDevice.hpp"
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
constexpr std::uint32_t Results = 6;
constexpr std::uint32_t Records = 4;
constexpr std::uint32_t Untouched = 0xcafebabeu;

alignas(256) constexpr std::array<std::uint32_t, 8> First{0x852b8685u, 0x4a04000au, 0xbea8030bu, 0xbefc030au, 0x7d880090u, 0xbf068080u, 0xbe80200eu, 0xbf810000u};
alignas(256) constexpr std::array<std::uint32_t, 8> Second{0x852b8685u, 0x3a04000au, 0x8028810bu, 0xbefc030bu, 0x7d860090u, 0xbf078080u, 0xbe80200eu, 0xbf810000u};
TestGuest::Array<std::uint32_t, 64> Program32;
TestGuest::Array<std::uint32_t, 64> Program64;
TestGuest::Array<std::uint32_t, Records * 4u> Table;
TestGuest::Array<std::uint32_t, 8> Srt;
TestGuest::Array<std::uint32_t, Threads * Results> Output;

constexpr std::array<std::uint32_t, Records> Arguments{0x11110000u, 0x22220000u, 0x33330000u, 0x44440000u};
constexpr std::array<std::uint32_t, Records> Seconds{0x00000101u, 0x00000202u, 0x00000303u, 0x00000404u};

std::array<std::uint32_t, 4> Buffer(const void* base, std::uint32_t records, std::uint32_t stride = 0u) {
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(base));
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (stride << 16u), records, 0x31016facu};
}

std::vector<std::uint32_t> Program(bool wave32) {
    std::vector<std::uint32_t> words{0x36020083u};
    if (wave32) {
        words.insert(words.end(), {0xbe9e037eu, 0x7e280501u, 0xbe9f037eu, 0x7da40214u});
    } else {
        words.insert(words.end(), {0xbe9e047eu, 0x7e280501u, 0x7d840214u, 0xbea0246au});
    }
    words.insert(words.end(), {0x8f148414u, 0xf4080400u, 0xfa000000u, 0xbf8cc07fu, 0xf4280208u, 0x28000000u, 0xbf8cc07fu, 0xbe8e0308u, 0xbe8f0309u, 0xbe8e210eu,
        0x7e080228u, 0x85298281u, 0x7e0a0229u, 0x7e100284u, 0x020c1083u, 0xbeaa037cu, 0x7e0e022au, 0x7e12022bu});
    if (wave32) {
        words.insert(words.end(), {0x8a7e7e1fu, 0xbf89ffe9u, 0xbefe031eu});
    } else {
        words.insert(words.end(), {0x8afe7e20u, 0xbf89ffe9u, 0xbefe041eu});
    }
    words.insert(words.end(), {0xf4080600u, 0xfa000010u, 0x34060082u, 0xbf8cc07fu, 0xe0701000u, 0x80060203u, 0xe0701100u, 0x80060403u, 0xe0701200u, 0x80060503u,
        0xe0701300u, 0x80060603u, 0xe0701400u, 0x80060703u, 0xe0701500u, 0x80060903u, 0xbf810000u});
    return words;
}

void SetRecord(std::uint32_t record, const void* target) {
    const auto code = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(target));
    Table[record * 4u] = static_cast<std::uint32_t>(code);
    Table[record * 4u + 1u] = static_cast<std::uint32_t>(code >> 32u);
    Table[record * 4u + 2u] = Arguments[record];
    Table[record * 4u + 3u] = Seconds[record];
}

std::vector<std::uint32_t> Commands(const void* program, bool wave32) {
    std::vector<std::uint32_t> words;
    const auto registers = [&](std::uint32_t first, std::span<const std::uint32_t> values) {
        words.push_back(0xc0007600u | (static_cast<std::uint32_t>(values.size()) << 16u));
        words.push_back(first);
        words.insert(words.end(), values.begin(), values.end());
    };
    const std::array<std::uint32_t, 3> shape{Threads, 1, 1};
    registers(0x207, shape);
    const auto address = reinterpret_cast<std::uintptr_t>(program);
    const std::array<std::uint32_t, 2> code{static_cast<std::uint32_t>(address >> 8u), static_cast<std::uint32_t>(address >> 40u)};
    registers(0x20c, code);
    const std::array<std::uint32_t, 1> resource{2u << 1u};
    registers(0x213, resource);
    const auto srt = reinterpret_cast<std::uintptr_t>(Srt.data());
    const std::array<std::uint32_t, 2> data{static_cast<std::uint32_t>(srt), static_cast<std::uint32_t>(srt >> 32u)};
    registers(0x240, data);
    words.insert(words.end(), {0xc0031500u, 1, 1, 1, 0x41u | (wave32 ? 0x8000u : 0u)});
    return words;
}

std::uint32_t Expected(std::uint32_t lane, std::uint32_t result) {
    const auto record = lane & 3u;
    const bool first = (record & 1u) == 0u;
    switch (result) {
        case 0: return first ? Arguments[record] + lane : Arguments[record] ^ lane;
        case 1: return first ? Seconds[record] : Seconds[record] + 1u;
        case 2: return first ? 1u : 2u;
        case 3: return (first ? lane < 16u : lane >= 16u) ? 4u : 3u;
        case 4: return first ? Arguments[record] : Seconds[record];
        default: return record == 0u ? 6u : 5u;
    }
}

void Check(bool wave32) {
    auto& program = wave32 ? Program32 : Program64;
    const auto words = Program(wave32);
    std::copy(words.begin(), words.end(), program.begin());
    Output.fill(Untouched);
    const auto commands = Commands(program.data(), wave32);
    Packet packet{const_cast<std::uint32_t*>(commands.data()), static_cast<std::uint32_t>(commands.size()), 0, {}};
    AgcDriver::Submit(&packet, 0);
    AgcDriverWaitIdle_nid_postfix();
    AgcDriver::GuestMemory::FlushGpuWrites(reinterpret_cast<std::uintptr_t>(Output.data()), decltype(Output)::ByteSize);
    constexpr const char* Names[Results] = {"v2, written under exec", "s40", "SCC", "VCC", "M0", "the SCC its callee saw at entry"};
    for (std::uint32_t result = 0; result < Results; ++result) {
        for (std::uint32_t lane = 0; lane < Threads; ++lane) {
            const auto actual = Output[result * Threads + lane];
            const auto expected = Expected(lane, result);
            Require(actual == expected, std::string("table callees with divergent targets and exec-off lanes") + (wave32 ? " (wave32)" : " (wave64)") + ": lane " + std::to_string(lane) + " " + Names[result] +
                " read after the call is " + std::to_string(actual) + ", expected " + std::to_string(expected));
        }
    }
}

}

int main() {
    try {
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
        }
        const auto table = Buffer(Table.data(), Records, 16u);
        const auto output = Buffer(Output.data(), static_cast<std::uint32_t>(decltype(Output)::ByteSize));
        std::copy(table.begin(), table.end(), Srt.begin());
        std::copy(output.begin(), output.end(), Srt.begin() + 4);
        for (std::uint32_t record = 0; record < Records; ++record) SetRecord(record, (record & 1u) == 0u ? First.data() : Second.data());
        Check(true);
        Check(false);
        AgcDriverShutdown_nid_postfix();
        std::puts("compute callee register tests passed");
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
