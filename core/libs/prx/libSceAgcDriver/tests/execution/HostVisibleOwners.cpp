#include "prx/libSceAgcDriver/Graphics/include/BufferPool.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Resources.hpp"
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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

using namespace AgcDriver::Graphics;

constexpr std::size_t HeapBytes = std::size_t{1} << 20u;
constexpr std::size_t PoolBytes = std::size_t{4} << 20u;

class GuestBlock {
public:
    GuestBlock() {
#ifdef _WIN32
        block = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(HeapBytes, 65536));
        if (block != nullptr) GuestArena::GuestArenaCommit_nid_postfix(block, HeapBytes, PAGE_READWRITE, HeapBytes);
#else
        constexpr std::uintptr_t alignment = 65536;
        void* mapped = mmap(nullptr, HeapBytes + alignment, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapped != MAP_FAILED) {
            const auto begin = reinterpret_cast<std::uintptr_t>(mapped);
            const auto aligned = (begin + alignment - 1) & ~(alignment - 1);
            if (aligned != begin) munmap(mapped, aligned - begin);
            if (aligned + HeapBytes != begin + HeapBytes + alignment) munmap(reinterpret_cast<void*>(aligned + HeapBytes), begin + alignment - aligned);
            block = reinterpret_cast<std::uint8_t*>(aligned);
            GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(block, HeapBytes);
        }
#endif
        Require(block != nullptr, "host-visible owners: cannot allocate the guest heap block");
        std::memset(block, 0x5a, HeapBytes);
    }

    ~GuestBlock() {
        Register(false);
#ifdef _WIN32
        GuestArena::GuestArenaReset_nid_postfix(block, HeapBytes);
        GuestArena::GuestArenaRelease_nid_postfix(block, HeapBytes);
#else
        munmap(block, HeapBytes);
        GuestWriteWatch::GuestWriteWatchUnregister_nid_postfix(block, HeapBytes);
#endif
    }

    GuestBlock(const GuestBlock&) = delete;
    GuestBlock& operator=(const GuestBlock&) = delete;

    void Register(bool add) {
        if (add == registered) return;
        GuestAllocations::Mutation mutation;
        if (add) mutation.Add(block, HeapBytes, true, false, true);
        else mutation.Remove(block);
        registered = add;
    }

private:
    std::uint8_t* block = nullptr;
    bool registered = false;
};

void build(const Context& context) {
    GuestBufferMemory leased(context);
    leased.AcquireRegistered();
    leased.Upload(true);
    leased.WriteBack();
}

std::uint64_t ownerBytes(const HostVisibleCounts& counts, HostOwner owner) {
    return counts.Owner(owner).bytes;
}

std::uint64_t ownerBuffers(const HostVisibleCounts& counts, HostOwner owner) {
    return counts.Owner(owner).buffers;
}

void requireOthersUnchanged(const HostVisibleCounts& before, const HostVisibleCounts& after, HostOwner changed, const char* reason) {
    for (std::size_t owner = 0; owner < before.owners.size(); ++owner) {
        if (static_cast<HostOwner>(owner) == changed) continue;
        Require(before.owners[owner].bytes == after.owners[owner].bytes && before.owners[owner].buffers == after.owners[owner].buffers, reason);
    }
}

void poolTests(const Context& context, BufferPool& pool) {
    const auto before = pool.HostVisible();
    std::optional<Buffer> pooled;
    pooled.emplace(context, PoolBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    const auto live = pool.HostVisible();
    Require(ownerBytes(live, HostOwner::Other) == ownerBytes(before, HostOwner::Other) + PoolBytes && ownerBuffers(live, HostOwner::Other) == ownerBuffers(before, HostOwner::Other) + 1, "a live buffer with no owner scope was not counted as other");
    Require(live.allocations == before.allocations + 1 && live.retained.bytes == before.retained.bytes, "a newly allocated buffer was not counted as one allocation");
    requireOthersUnchanged(before, live, HostOwner::Other, "a buffer with no owner scope was counted under an owner");
    pooled.reset();
    const auto retained = pool.HostVisible();
    Require(ownerBytes(retained, HostOwner::Other) == ownerBytes(before, HostOwner::Other) && ownerBuffers(retained, HostOwner::Other) == ownerBuffers(before, HostOwner::Other), "a released buffer is still counted as live");
    Require(retained.retained.bytes == before.retained.bytes + PoolBytes && retained.retained.buffers == before.retained.buffers + 1, "a released buffer the pool retains is not counted under BufferPool");
    Require(retained.allocations == live.allocations && retained.Bytes() == live.Bytes(), "retaining a buffer changed the host-visible total");
    {
        const HostOwnerScope owner(HostOwner::Staging);
        pooled.emplace(context, PoolBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    }
    Require(CurrentHostOwner() == HostOwner::Other, "an owner scope outlived its block");
    const auto reused = pool.HostVisible();
    Require(ownerBytes(reused, HostOwner::Staging) == ownerBytes(before, HostOwner::Staging) + PoolBytes && ownerBuffers(reused, HostOwner::Staging) == ownerBuffers(before, HostOwner::Staging) + 1, "a pooled buffer taken under a staging scope was not counted as staging");
    Require(reused.retained.bytes == before.retained.bytes && reused.allocations == live.allocations, "a buffer taken from the pool is still counted as retained");
    pooled.reset();
    std::cout << "pool: " << FormatHostVisible(pool.HostVisible()) << '\n';
}

void heapMirrorTests(const Context& context, BufferPool& pool) {
    if (!context.bufferDeviceAddress) {
        std::cout << "buffer device address unavailable: heap mirror owner not tested\n";
        return;
    }
    GuestBlock block;
    build(context);
    const auto before = pool.HostVisible();
    const auto mirrorsBefore = MirrorCounters();
    block.Register(true);
    build(context);
    const auto mirrored = pool.HostVisible();
    const auto mirrorsMade = MirrorCounters();
    Require(mirrorsMade.heapMirrors == mirrorsBefore.heapMirrors + 1 && mirrorsMade.heapBytes == mirrorsBefore.heapBytes + HeapBytes, "the registered heap range was not mirrored");
    Require(ownerBytes(mirrored, HostOwner::HeapMirror) == ownerBytes(before, HostOwner::HeapMirror) + HeapBytes && ownerBuffers(mirrored, HostOwner::HeapMirror) == ownerBuffers(before, HostOwner::HeapMirror) + 1, "the heap mirror was not counted under heap mirrors");
    Require(ownerBytes(mirrored, HostOwner::Other) == ownerBytes(before, HostOwner::Other) && ownerBytes(mirrored, HostOwner::ImageMirror) == ownerBytes(before, HostOwner::ImageMirror), "the heap mirror was counted under another owner");
    const auto line = FormatHostVisible(mirrored);
    std::cout << "mirrored: " << line << '\n';
    Require(line.find(" heap mirrors 1.0 MiB (1),") != std::string::npos, "the [buffers] line does not show the heap mirror");
    block.Register(false);
    build(context);
    const auto released = pool.HostVisible();
    Require(MirrorCounters().heapMirrors == mirrorsBefore.heapMirrors && MirrorCounters().heapBytes == mirrorsBefore.heapBytes, "the heap mirror outlived its range");
    Require(ownerBytes(released, HostOwner::HeapMirror) == ownerBytes(before, HostOwner::HeapMirror) && ownerBuffers(released, HostOwner::HeapMirror) == ownerBuffers(before, HostOwner::HeapMirror), "a released heap mirror is still counted");
    Require(released.retained.bytes == mirrored.retained.bytes + HeapBytes && released.Bytes() == mirrored.Bytes(), "the released heap mirror's memory did not move to BufferPool");
    std::cout << "released: " << FormatHostVisible(released) << '\n';
}

void offTests(const Context& context, BufferPool& pool) {
    Require(!BufferPool::CountsHost(), "the host-visible counter is on without APS5_PROFILE_DRAW");
    std::optional<Buffer> pooled;
    {
        const HostOwnerScope owner(HostOwner::Staging);
        pooled.emplace(context, PoolBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    }
    const auto counts = pool.HostVisible();
    for (const auto& owner : counts.owners) Require(owner.bytes == 0 && owner.buffers == 0, "live buffers were counted with the counter off");
    pooled.reset();
}

}

int main(int argc, char** argv) {
    const bool off = argc > 1 && std::string_view(argv[1]) == "--off";
#ifdef _WIN32
    _putenv_s("APS5_PROFILE_DRAW", off ? "" : "1");
#else
    if (off) unsetenv("APS5_PROFILE_DRAW");
    else setenv("APS5_PROFILE_DRAW", "1", 1);
#endif
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        auto context = device->graphicsContext();
        context.hostImportAlignment = 0;
        context.sparseBinding = false;
        const auto pool = GetBufferPool(context);
        if (off) {
            offTests(context, *pool);
            std::puts("host-visible owner tests (counter off) passed");
            return 0;
        }
        Require(BufferPool::CountsHost(), "APS5_PROFILE_DRAW did not turn the host-visible counter on");
        poolTests(context, *pool);
        heapMirrorTests(context, *pool);
        std::puts("host-visible owner tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
