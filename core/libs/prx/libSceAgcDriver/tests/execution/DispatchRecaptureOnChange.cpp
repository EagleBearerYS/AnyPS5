#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Synchronization/SynchronizationStatistics.hpp"
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
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t Lanes = 32;
constexpr std::uint32_t BufferBytes = Lanes * 4u;
constexpr std::size_t BlockBytes = 65536;

alignas(256) constexpr std::array<std::uint32_t, 11> TableCode{0xf4080200u, 0xfa000000u, 0xbf8cc07fu, 0x34020082u, 0xe0301000u, 0x80020201u, 0xbf8c3f70u, 0x4a060481u, 0xe0701000u, 0x80010301u, 0xbf810000u};

void Expect(bool condition, const std::string& what) {
    if (!condition) throw std::runtime_error("dispatch recapture: " + what);
}

class GuestBlock {
public:
    GuestBlock() {
#ifdef _WIN32
        block = static_cast<std::uint32_t*>(GuestArena::GuestArenaAllocate_nid_postfix(BlockBytes, 65536));
        if (block != nullptr) GuestArena::GuestArenaCommit_nid_postfix(block, BlockBytes, PAGE_READWRITE, BlockBytes);
#else
        constexpr std::uintptr_t alignment = 65536;
        void* mapped = mmap(nullptr, BlockBytes + alignment, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapped != MAP_FAILED) {
            const auto begin = reinterpret_cast<std::uintptr_t>(mapped);
            const auto aligned = (begin + alignment - 1) & ~(alignment - 1);
            if (aligned != begin) munmap(mapped, aligned - begin);
            if (aligned + BlockBytes != begin + BlockBytes + alignment) munmap(reinterpret_cast<void*>(aligned + BlockBytes), begin + alignment - aligned);
            block = reinterpret_cast<std::uint32_t*>(aligned);
            GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(block, BlockBytes);
        }
#endif
        Expect(block != nullptr, "cannot allocate a guest block");
        std::memset(block, 0, BlockBytes);
        GuestAllocations::Mutation().Add(block, BlockBytes, true, true, true);
    }

    GuestBlock(const GuestBlock&) = delete;
    GuestBlock& operator=(const GuestBlock&) = delete;

    std::uint32_t* Words() const { return block; }

private:
    std::uint32_t* block = nullptr;
};

std::array<std::uint32_t, 4> BufferDescriptor(const void* data) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu), BufferBytes, 0x31016facu};
}

const GuestBlock* Table = nullptr;
const GuestBlock* Moved = nullptr;
const GuestBlock* Unmapped = nullptr;
std::atomic<bool> TableChangeArmed{false};
std::atomic<bool> TableUnmapArmed{false};
std::atomic<int> TableChanges{0};
std::atomic<int> TableUnmaps{0};

void UnmapTable(const GuestBlock& block) {
    GuestAllocations::Mutation().Remove(block.Words());
#ifdef _WIN32
    GuestArena::GuestArenaReset_nid_postfix(block.Words(), BlockBytes);
#else
    GuestWriteWatch::GuestWriteWatchUnregister_nid_postfix(block.Words(), BlockBytes);
    munmap(block.Words(), BlockBytes);
#endif
}

void ChangeTableBeforeCheck() {
    if (TableUnmapArmed.exchange(false)) {
        UnmapTable(*Unmapped);
        TableUnmaps.fetch_add(1);
        return;
    }
    if (!TableChangeArmed.exchange(false)) return;
    const auto descriptor = BufferDescriptor(Moved->Words());
    std::memcpy(Table->Words(), descriptor.data(), sizeof(descriptor));
    TableChanges.fetch_add(1);
}

void Dispatch(const GuestBlock& table, const GuestBlock& output) {
    const auto codeAddress = reinterpret_cast<std::uintptr_t>(TableCode.data());
    const auto tableAddress = reinterpret_cast<std::uintptr_t>(table.Words());
    const auto stored = BufferDescriptor(output.Words());
    std::vector<std::uint32_t> commands{
        0xc0027600u, 0x20cu, static_cast<std::uint32_t>(codeAddress >> 8u), static_cast<std::uint32_t>(codeAddress >> 40u),
        0xc0037600u, 0x207u, Lanes, 1u, 1u,
        0xc0017600u, 0x213u, 8u << 1u,
        0xc0087600u, 0x240u, static_cast<std::uint32_t>(tableAddress), static_cast<std::uint32_t>(tableAddress >> 32u), 0u, 0u, stored[0], stored[1], stored[2], stored[3],
        0xc0031500u, 1u, 1u, 1u, 0x8041u,
    };
    Packet packet{commands.data(), static_cast<std::uint32_t>(commands.size()), 0, {}};
    Expect(sceAgcDriverSubmitDcb(&packet) == 0, "the dispatch submission failed");
    AgcDriverWaitIdle_nid_postfix();
    AgcDriver::GuestMemory::FlushGpuWrites(reinterpret_cast<std::uintptr_t>(output.Words()), BufferBytes);
}

std::uint32_t Lane(const GuestBlock& output, std::uint32_t lane) {
    return output.Words()[lane];
}

void ExpectOutput(const GuestBlock& output, std::uint32_t base, const std::string& what) {
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) Expect(Lane(output, lane) == base + lane + 1u, what + ": lane " + std::to_string(lane) + " stored " + std::to_string(Lane(output, lane)) + ", expected " + std::to_string(base + lane + 1u));
}

}

int main() {
    try {
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
        }
        Shader shader{};
        shader.file_header = 0x34333231;
        shader.version = 0x18;
        shader.header_size = sizeof(Shader);
        shader.shader_size = sizeof(TableCode);
        shader.code = TableCode.data();
        AgcDriverRegisterShader_nid_postfix(&shader);
        GuestBlock table;
        GuestBlock first;
        GuestBlock moved;
        GuestBlock output;
        for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
            first.Words()[lane] = 0x1000u + lane;
            moved.Words()[lane] = 0x2000u + lane;
        }
        const auto initial = BufferDescriptor(first.Words());
        std::memcpy(table.Words(), initial.data(), sizeof(initial));
        Table = &table;
        Moved = &moved;
        AgcDriver::DriverDetail::Driver::SetCaptureCheckHook(&ChangeTableBeforeCheck);

        Dispatch(table, output);
        ExpectOutput(output, 0x1000u, "the first dispatch through the table");
        std::printf("the first dispatch stored 0x%x in lane 0 through the table's first V#\n", Lane(output, 0));

        const auto retries = AgcDriver::DriverDetail::captureChangeRetries.load();
        TableChangeArmed.store(true);
        Dispatch(table, output);
        const auto redone = AgcDriver::DriverDetail::captureChangeRetries.load() - retries;
        std::printf("the same dispatch with its table's V# moved to another buffer after its capture: the table changed %d time(s), lane 0 stored 0x%x (%s), %llu packet(s) prepared again\n", TableChanges.load(), Lane(output, 0), Lane(output, 0) == 0x2001u ? "the moved V# was bound" : "the V# captured before the change was bound", static_cast<unsigned long long>(redone));
        Expect(TableChanges.load() == 1, "the check seam did not run before the dispatch was recorded");
        ExpectOutput(output, 0x2000u, "the dispatch whose table changed before it was recorded");
        Expect(redone == 1, "the dispatch was prepared again " + std::to_string(redone) + " times, not once");

        const auto steady = AgcDriver::DriverDetail::captureChangeRetries.load();
        Dispatch(table, output);
        const auto again = AgcDriver::DriverDetail::captureChangeRetries.load() - steady;
        std::printf("the same dispatch with its table unchanged: lane 0 stored 0x%x, %llu packet(s) prepared again\n", Lane(output, 0), static_cast<unsigned long long>(again));
        ExpectOutput(output, 0x2000u, "the dispatch with an unchanged table");
        Expect(again == 0, "a dispatch whose captured memory did not change was prepared again");

        GuestBlock doomed;
        std::memcpy(doomed.Words(), initial.data(), sizeof(initial));
        Dispatch(doomed, output);
        ExpectOutput(output, 0x1000u, "the dispatch through the table that is unmapped next");
        Unmapped = &doomed;
        const auto beforeUnmap = AgcDriver::DriverDetail::captureChangeRetries.load();
        TableUnmapArmed.store(true);
        std::string failure;
        try {
            Dispatch(doomed, output);
        } catch (const std::exception& error) {
            failure = error.what();
        }
        const auto unmappedRedone = AgcDriver::DriverDetail::captureChangeRetries.load() - beforeUnmap;
        char unreadable[64];
        std::snprintf(unreadable, sizeof(unreadable), "not readable at 0x%llx", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(doomed.Words())));
        std::printf("the same dispatch through a table unmapped after its capture: the table was unmapped %d time(s), %llu packet(s) prepared again, and preparing it again reported: %s\n", TableUnmaps.load(), static_cast<unsigned long long>(unmappedRedone), failure.c_str());
        Expect(TableUnmaps.load() == 1, "the check seam did not unmap the table before the dispatch was recorded");
        Expect(unmappedRedone == 1, "a dispatch whose captured table was unmapped was prepared again " + std::to_string(unmappedRedone) + " times, not once");
        Expect(failure.find(unreadable) != std::string::npos, "preparing the dispatch again did not read the unmapped table: '" + failure + "'");

        AgcDriver::DriverDetail::Driver::SetCaptureCheckHook(nullptr);
        try {
            AgcDriverShutdown_nid_postfix();
        } catch (const std::exception& error) {
            Expect(std::string(error.what()) == failure, std::string("shutdown reported another failure: ") + error.what());
        }
        std::puts("dispatch recapture: a dispatch whose captured descriptor table changed before it was recorded binds the table in memory, and one whose table was unmapped is prepared again from current memory");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        try { AgcDriverShutdown_nid_postfix(); } catch (...) {}
        return 1;
    }
}
