#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawPipeline.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
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
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <future>
#include <initializer_list>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using AgcDriver::DriverDetail::DrawPipeline;

constexpr std::uint8_t Kept = 0x40;
constexpr std::uint32_t BaseVertexLocation = 0x8c;
constexpr std::size_t BlockBytes = 65536;
constexpr std::size_t RecordOffset = 0;
constexpr std::size_t TargetOffset = 0x1000;
constexpr std::size_t TargetBytes = 1024;
constexpr std::size_t LabelOffset = 0x3000;
constexpr std::uint32_t HeavyInstances = 50000;
constexpr std::uint32_t LinearAttrib3 = 0x09000000u;
constexpr std::uint32_t TiledAttrib3 = 0x4dc6c000u;

alignas(256) constexpr std::array<std::uint32_t, 20> VertexCode{
    0x7e000d05u, 0x080200f4u, 0x10020300u, 0x100202f4u, 0x060202f3u, 0x060400f3u, 0x10040500u, 0x060404f3u,
    0x7e060208u, 0x7e0802f2u, 0x7e0a02ffu, 0x44800000u, 0xf80000d1u, 0x00000005u, 0xf80008cfu, 0x04030201u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u,
};
alignas(256) constexpr std::array<std::uint32_t, 8> PixelCode{0x7e000200u, 0x7e020280u, 0xf800180fu, 0x00010100u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u};
alignas(256) constexpr std::array<std::uint32_t, 11> PixelStoreOne{0x7e1a0280u, 0x7e180281u, 0xdc708000u, 0x00020c0du, 0x7e1002f2u, 0x7e1202f2u, 0x7e1402f2u, 0x7e1602f2u, 0xf800180fu, 0x0b0a0908u, 0xbf810000u};

struct Header {
    Shader shader{};
    ShaderUserData userData{};
};

alignas(8) Header VertexHeader{};
alignas(8) Header PixelHeader{};
alignas(8) Header StoreHeader{};

void Registered(Header& header, const std::uint32_t* code, std::size_t bytes, std::uint8_t type) {
    header.shader.file_header = 0x34333231u;
    header.shader.version = 0x18u;
    header.shader.header_size = sizeof(Header);
    header.shader.shader_size = static_cast<std::uint32_t>(bytes);
    header.shader.code = code;
    header.shader.type = type;
    header.shader.user_data = &header.userData;
    AgcDriverRegisterShader_nid_postfix(&header.shader);
}

class GuestBlock {
public:
    GuestBlock() {
#ifdef _WIN32
        block = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(BlockBytes, 65536));
        if (block != nullptr) GuestArena::GuestArenaCommit_nid_postfix(block, BlockBytes, PAGE_READWRITE, BlockBytes);
#else
        constexpr std::uintptr_t alignment = 65536;
        void* mapped = mmap(nullptr, BlockBytes + alignment, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapped != MAP_FAILED) {
            const auto begin = reinterpret_cast<std::uintptr_t>(mapped);
            const auto aligned = (begin + alignment - 1) & ~(alignment - 1);
            if (aligned != begin) munmap(mapped, aligned - begin);
            if (aligned + BlockBytes != begin + BlockBytes + alignment) munmap(reinterpret_cast<void*>(aligned + BlockBytes), begin + alignment - aligned);
            block = reinterpret_cast<std::uint8_t*>(aligned);
            GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(block, BlockBytes);
        }
#endif
        Require(block != nullptr, "bda store cpu read: cannot allocate a guest block");
        std::memset(block, 0, BlockBytes);
        GuestAllocations::Mutation().Add(block, BlockBytes, true, true, true);
    }

    GuestBlock(const GuestBlock&) = delete;
    GuestBlock& operator=(const GuestBlock&) = delete;

    std::uint64_t At(std::size_t offset) const { return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(block + offset)); }
    std::uint8_t* Bytes(std::size_t offset) const { return block + offset; }

private:
    std::uint8_t* block = nullptr;
};

void Append(std::vector<std::uint32_t>& commands, std::uint32_t opcode, std::initializer_list<std::uint32_t> payload) {
    commands.push_back(0xc0000000u | (static_cast<std::uint32_t>(payload.size() - 1) << 16u) | (opcode << 8u));
    commands.insert(commands.end(), payload);
}

std::uint32_t Low(std::uint64_t value) { return static_cast<std::uint32_t>(value); }

std::uint32_t High(std::uint64_t value) { return static_cast<std::uint32_t>(value >> 32u); }

void State(std::vector<std::uint32_t>& commands, const GuestBlock& records) {
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> context{
        {0x2d5, 0x2000},
        {0x1b6, 0}, {0x207, 0x01200000}, {0x200, 0}, {0x203, 0x800},
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
    const auto vertex = reinterpret_cast<std::uintptr_t>(VertexCode.data());
    Append(commands, 0x79, {0x242, 4});
    Append(commands, 0x79, {0x24a, 0});
    for (const auto& [offset, value] : context) Append(commands, 0x69, {offset, value});
    for (const auto& [offset, value] : std::initializer_list<std::pair<std::uint32_t, std::uint32_t>>{{0xc8, static_cast<std::uint32_t>(vertex >> 8u)}, {0xc9, static_cast<std::uint32_t>(vertex >> 40u)}, {0x8b, 1u << 1u}}) Append(commands, 0x76, {offset, value});
    Append(commands, 0x11, {1, Low(records.At(RecordOffset)), High(records.At(RecordOffset))});
}

void Target(std::vector<std::uint32_t>& commands, const GuestBlock& block, std::size_t offset, std::uint32_t attrib3) {
    const auto color = block.At(offset);
    Append(commands, 0x69, {0x318, static_cast<std::uint32_t>(color >> 8u)});
    Append(commands, 0x69, {0x390, static_cast<std::uint32_t>(color >> 40u)});
    Append(commands, 0x69, {0x3b8, attrib3});
    Append(commands, 0x76, {BaseVertexLocation, 0});
}

void Writer(std::vector<std::uint32_t>& commands, const GuestBlock& records, const GuestBlock& target, std::uint32_t instances) {
    const auto pixel = reinterpret_cast<std::uintptr_t>(PixelStoreOne.data());
    const auto instancesWord = records.At(RecordOffset + 4);
    Target(commands, target, 0, TiledAttrib3);
    for (const auto& [offset, value] : std::initializer_list<std::pair<std::uint32_t, std::uint32_t>>{{0x008, static_cast<std::uint32_t>(pixel >> 8u)}, {0x009, static_cast<std::uint32_t>(pixel >> 40u)}, {0x00b, 4u << 1u}, {0x00c, 0}, {0x00d, 0}, {0x00e, Low(instancesWord)}, {0x00f, High(instancesWord)}}) Append(commands, 0x76, {offset, value});
    Append(commands, 0x2f, {instances});
    Append(commands, 0x2d, {3, 2});
    Append(commands, 0x2f, {1});
}

void Plain(std::vector<std::uint32_t>& commands, const GuestBlock& target) {
    const auto pixel = reinterpret_cast<std::uintptr_t>(PixelCode.data());
    Target(commands, target, 0, TiledAttrib3);
    for (const auto& [offset, value] : std::initializer_list<std::pair<std::uint32_t, std::uint32_t>>{{0x008, static_cast<std::uint32_t>(pixel >> 8u)}, {0x009, static_cast<std::uint32_t>(pixel >> 40u)}, {0x00b, 1u << 1u}, {0x00c, std::bit_cast<std::uint32_t>(1.0f)}}) Append(commands, 0x76, {offset, value});
    Append(commands, 0x2d, {3, 2});
}

void Reader(std::vector<std::uint32_t>& commands, const GuestBlock& block) {
    const auto pixel = reinterpret_cast<std::uintptr_t>(PixelCode.data());
    Target(commands, block, TargetOffset, LinearAttrib3);
    for (const auto& [offset, value] : std::initializer_list<std::pair<std::uint32_t, std::uint32_t>>{{0x008, static_cast<std::uint32_t>(pixel >> 8u)}, {0x009, static_cast<std::uint32_t>(pixel >> 40u)}, {0x00b, 1u << 1u}, {0x00c, std::bit_cast<std::uint32_t>(1.0f)}}) Append(commands, 0x76, {offset, value});
    Append(commands, 0x24, {0, BaseVertexLocation, 0, 2});
}

void Submit(std::vector<std::uint32_t>& commands) {
    Packet packet{commands.data(), static_cast<std::uint32_t>(commands.size()), 0, {}};
    Require(sceAgcDriverSubmitDcb(&packet) == 0, "bda store cpu read: the submission failed");
}

void Reset(const GuestBlock& records, const GuestBlock& reader) {
    const std::array<std::uint32_t, 4> record{3, 0, 0, 0};
    std::memcpy(records.Bytes(RecordOffset), record.data(), sizeof(record));
    std::memset(reader.Bytes(TargetOffset), Kept, TargetBytes);
    std::memset(reader.Bytes(LabelOffset), 0, 4);
    AgcDriver::GuestMemory::MarkWritten(records.At(0), BlockBytes);
    AgcDriver::GuestMemory::MarkWritten(reader.At(0), BlockBytes);
}

void MakeResident(const GuestBlock& records, const GuestBlock& writer) {
    for (int round = 0; round < 2; ++round) {
        std::vector<std::uint32_t> commands;
        State(commands, records);
        Plain(commands, writer);
        Submit(commands);
        AgcDriverWaitIdle_nid_postfix();
    }
}

std::size_t Covered(const GuestBlock& block, std::size_t offset) {
    AgcDriver::GuestMemory::FlushGpuWrites(static_cast<std::uintptr_t>(block.At(offset)), TargetBytes);
    const auto* bytes = block.Bytes(offset);
    return static_cast<std::size_t>(std::count_if(bytes, bytes + TargetBytes, [](std::uint8_t value) { return value != Kept; }));
}

std::uint32_t StoredInstances(const GuestBlock& block) {
    std::uint32_t value = 0;
    std::memcpy(&value, block.Bytes(RecordOffset + 4), sizeof(value));
    return value;
}

bool Check(const char* name, const GuestBlock& records, const GuestBlock& reader) {
    const auto covered = Covered(reader, TargetOffset);
    const auto stored = StoredInstances(records);
    std::printf("%s: the writer stored %u instances; the CPU-path indirect draw after it covered %zu bytes\n", name, stored, covered);
    Require(stored == 1, std::string(name) + ": the store through the table did not reach the record (it holds " + std::to_string(stored) + " instances)");
    return covered != 0;
}

bool OpenBatch(const GuestBlock& records, const GuestBlock& writer, const GuestBlock& reader) {
    Reset(records, reader);
    std::vector<std::uint32_t> commands;
    State(commands, records);
    Writer(commands, records, writer, 1);
    Reader(commands, reader);
    Submit(commands);
    AgcDriverWaitIdle_nid_postfix();
    AgcDriver::GuestMemory::FlushGpuWrites(static_cast<std::uintptr_t>(writer.At(0)), BlockBytes);
    return Check("store in the open batch", records, reader);
}

bool SubmittedBatch(const GuestBlock& records, const GuestBlock& writer, const GuestBlock& reader) {
    Reset(records, reader);
    std::vector<std::uint32_t> first;
    State(first, records);
    Writer(first, records, writer, HeavyInstances);
    Append(first, 0x37, {0x00100200u, Low(reader.At(LabelOffset)), High(reader.At(LabelOffset)), 1u});
    std::vector<std::uint32_t> second;
    State(second, records);
    Reader(second, reader);
    Submit(first);
    Submit(second);
    AgcDriverWaitIdle_nid_postfix();
    std::uint32_t label = 0;
    AgcDriver::GuestMemory::FlushGpuWrites(static_cast<std::uintptr_t>(writer.At(0)), BlockBytes);
    std::memcpy(&label, reader.Bytes(LabelOffset), sizeof(label));
    Require(label == 1, "store in a submitted batch: the writer's label was not written");
    return Check("store in a submitted batch", records, reader);
}

struct Blocker {
    std::atomic<bool> release{false};
    void Enqueue() {
        DrawPipeline::Queue0().Enqueue([this] {
            while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }, {});
    }
};

void Finish() {
    auto idle = std::async(std::launch::async, [] { AgcDriverWaitIdle_nid_postfix(); });
    if (idle.wait_for(std::chrono::seconds(20)) != std::future_status::ready) {
        std::fputs("bda store cpu read: the queue worker and the draw pipeline deadlocked\n", stderr);
        std::fflush(stderr);
        std::_Exit(1);
    }
    idle.get();
}

bool HeldWriter(const GuestBlock& records, const GuestBlock& writer, const GuestBlock& reader) {
    Reset(records, reader);
    std::vector<std::uint32_t> commands;
    State(commands, records);
    Writer(commands, records, writer, 1);
    Reader(commands, reader);
    Blocker blocker;
    blocker.Enqueue();
    Submit(commands);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const auto outstanding = DrawPipeline::Queue0().Outstanding();
    std::fprintf(stderr, "[bda-store-cpu-read] releasing the blocker with %zu committer items outstanding\n", outstanding);
    blocker.release = true;
    Finish();
    AgcDriver::GuestMemory::FlushGpuWrites(static_cast<std::uintptr_t>(writer.At(0)), BlockBytes);
    std::printf("store held behind a committer item: %zu items were outstanding after 500 ms, so the reader %s\n", outstanding, outstanding <= 2 ? "drained behind the held writer" : "went past the held writer without draining");
    return Check("store held behind a committer item", records, reader);
}

}

int main(int argc, char** argv) {
#ifdef _WIN32
    _putenv_s("APS5_PIPELINED_DRAWS", "1");
#else
    setenv("APS5_PIPELINED_DRAWS", "1", 1);
#endif
    try {
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
        }
        Registered(VertexHeader, VertexCode.data(), sizeof(VertexCode), 2);
        Registered(PixelHeader, PixelCode.data(), sizeof(PixelCode), 1);
        Registered(StoreHeader, PixelStoreOne.data(), sizeof(PixelStoreOne), 1);
        GuestBlock records;
        GuestBlock writer;
        GuestBlock reader;
        MakeResident(records, writer);
        const std::string only = argc > 1 ? argv[1] : "";
        bool green = true;
        if (only.empty() || only == "--open") green = OpenBatch(records, writer, reader) && green;
        if (only.empty() || only == "--submitted") green = SubmittedBatch(records, writer, reader) && green;
        if (only.empty() || only == "--held") green = HeldWriter(records, writer, reader) && green;
        Require(green, "a CPU-path indirect draw read its record before the store through the BDA table recorded ahead of it landed");
        AgcDriverShutdown_nid_postfix();
        std::puts("CPU-path indirect draws read the record a store through the BDA table recorded before them wrote");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        try { AgcDriverShutdown_nid_postfix(); } catch (...) {}
        return 1;
    }
}
