#include "BdaTests.hpp"
#include "prx/libSceAgcDriver/Graphics/include/BdaResources.hpp"
#include "prx/libSceAgcDriver/Graphics/include/DccMetadata.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestTextureResource.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libc/include/GuestWriteWatch.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace {

constexpr std::size_t imageBytes = 16u << 20u;
std::byte imageStorage[imageBytes + 65536];
std::byte* const imageData = reinterpret_cast<std::byte*>((reinterpret_cast<std::uint64_t>(imageStorage) + 65535) & ~std::uint64_t{65535});

long long setting(const char* name, long long fallback) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::atoll(value) : fallback;
}

std::byte* allocateWatched(std::size_t bytes) {
#ifdef _WIN32
    auto* block = static_cast<std::byte*>(GuestArena::GuestArenaAllocate_nid_postfix(bytes, 65536));
    AgcDriver::Graphics::Require(block != nullptr, "the arena refused a heap run");
    GuestArena::GuestArenaCommit_nid_postfix(block, bytes, PAGE_READWRITE, bytes);
    return block;
#else
    void* raw = mmap(nullptr, bytes + 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    AgcDriver::Graphics::Require(raw != MAP_FAILED, "cannot map a heap run");
    auto* block = reinterpret_cast<std::byte*>((reinterpret_cast<std::uintptr_t>(raw) + 65535) & ~std::uintptr_t{65535});
    GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(block, bytes);
    return block;
#endif
}

double since(std::chrono::steady_clock::time_point& from) {
    const auto now = std::chrono::steady_clock::now();
    const auto us = std::chrono::duration<double, std::micro>(now - from).count();
    from = now;
    return us;
}

struct CaptureRecord {
    const void* object;
    AgcDriver::Graphics::GuestTextureResource resource;
    std::uint64_t bytes;
    AgcDriver::Graphics::DccKeys keys;
    std::uint64_t generation;
    const void* source;
    bool depth = false;
};

void captureBench() {
    const auto rounds = static_cast<std::size_t>(setting("BENCH_CAPTURE_ROUNDS", 200));
    std::vector<CaptureRecord> log;
    std::vector<std::byte> objects(1u << 20u);
    std::vector<CaptureRecord> found;
    std::printf("[capture-bench] sizeof(record) %zu bytes\n", sizeof(CaptureRecord));
    for (const std::size_t textures : {16u, 64u, 256u, 512u, 1024u, 2048u}) {
        for (const bool logged : {true, false}) {
            double scanUs = 0;
            double mapUs = 0;
            std::size_t matched = 0;
            for (std::size_t round = 0; round < rounds; ++round) {
                log.clear();
                const std::size_t others = 16;
                for (std::size_t other = 0; other < others; ++other) log.push_back({objects.data() + (1u << 19u) + other * 64, {}, 0, {}, 0, nullptr});
                for (std::size_t texture = 0; logged && texture < textures; ++texture) {
                    if (log.size() >= 1024) log.erase(log.begin(), log.begin() + 512);
                    log.push_back({objects.data() + texture * 64, {}, texture, {}, texture, nullptr});
                }
                auto at = std::chrono::steady_clock::now();
                found.assign(textures, {});
                for (std::size_t texture = 0; texture < textures; ++texture) {
                    const void* object = objects.data() + texture * 64;
                    for (auto it = log.rbegin(); it != log.rend(); ++it) {
                        if (it->object != object) continue;
                        found[texture] = *it;
                        ++matched;
                        break;
                    }
                }
                scanUs += since(at);
                std::unordered_map<const void*, const CaptureRecord*> newest;
                newest.reserve(log.size());
                for (const auto& record : log) newest[record.object] = &record;
                found.assign(textures, {});
                for (std::size_t texture = 0; texture < textures; ++texture) {
                    if (const auto it = newest.find(objects.data() + texture * 64); it != newest.end()) found[texture] = *it->second;
                }
                mapUs += since(at);
            }
            std::printf("[capture-bench] %zu textures, %s, log %zu records: reverse scans %.1f us per capture (%zu matched per capture), one map pass %.1f us\n", textures, logged ? "each looked up this build" : "bound from a reused table (no records)", log.size(), scanUs / static_cast<double>(rounds), matched / rounds, mapUs / static_cast<double>(rounds));
        }
    }
}

}

void RunPerDrawBench(const AgcDriver::Graphics::Context& context) {
    if (setting("BENCH_CAPTURE", 0) != 0) {
        captureBench();
        return;
    }
    using namespace AgcDriver::Graphics;
    namespace GuestMemory = AgcDriver::GuestMemory;
#ifdef _WIN32
    _putenv_s("APS5_HEAP_MIRROR_MIB", "24576");
#else
    setenv("APS5_HEAP_MIRROR_MIB", "24576", 1);
#endif
    const auto runs = static_cast<std::size_t>(setting("BENCH_HEAP_RUNS", 383));
    const auto runBytes = static_cast<std::size_t>(setting("BENCH_RUN_KIB", 128)) << 10u;
    const auto builds = static_cast<std::size_t>(setting("BENCH_BUILDS", 400));
    const auto epochBuilds = static_cast<std::size_t>(setting("BENCH_EPOCH_BUILDS", 50));
    const auto dirtyBlocks = static_cast<std::size_t>(setting("BENCH_DIRTY_BLOCKS", 4));
    const auto imageDirty = static_cast<std::size_t>(setting("BENCH_IMAGE_DIRTY", 16));
    const auto snapshots = static_cast<std::size_t>(setting("BENCH_SNAPSHOTS", 4));
    const bool snapshotVary = setting("BENCH_SNAPSHOT_VARY", 0) != 0;
    const auto writes = static_cast<std::size_t>(setting("BENCH_WRITES", 1));
    const auto reads = static_cast<std::size_t>(setting("BENCH_READS", 16));
    const bool image = setting("BENCH_IMAGE", 1) != 0;
    Require(GuestMemory::WriteWatched(), "the bench needs the guest write watch");
    std::memset(imageData, 0x5a, imageBytes);
    std::vector<std::byte*> blocks;
    for (std::size_t index = 0; index < runs; ++index) {
        auto* block = allocateWatched(runBytes * 2);
        std::memset(block, static_cast<int>(index & 0xff), runBytes);
        blocks.push_back(block);
    }
    {
        GuestAllocations::Mutation mutation;
        for (auto* block : blocks) mutation.Add(block, runBytes, true, true, true);
        if (image) {
            mutation.RegisterMainImage();
            mutation.Protect(imageData, imageBytes, true, true, true, [] {});
        }
    }
    const auto registered = GuestAllocations::GuestAllocationsAcquire_nid_postfix().size();
    auto own = std::make_unique<std::byte[]>(snapshots * 4096 + 4096);
    std::memset(own.get(), 0x33, snapshots * 4096 + 4096);
    std::array<double, 6> totals{};
    std::array<double, 6> firstEpochBuild{};
    std::array<double, 6> laterBuilds{};
    std::size_t firsts = 0;
    std::size_t laters = 0;
    for (std::size_t build = 0; build < builds; ++build) {
        const bool epochStart = build % epochBuilds == 0;
        if (epochStart) GuestMemory::BumpCollectEpoch();
        for (std::size_t dirty = 0; dirty < dirtyBlocks; ++dirty) blocks[(build * 7 + dirty * 13) % runs][(dirty * 65536 + build * 64) % runBytes] = static_cast<std::byte>(build);
        for (std::size_t dirty = 0; dirty < imageDirty; ++dirty) imageData[(build * 4099 + dirty * 1048583) % imageBytes] = static_cast<std::byte>(build);
        if (snapshotVary) own[0] = static_cast<std::byte>(build);
        std::array<double, 6> us{};
        auto at = std::chrono::steady_clock::now();
        {
            GuestBufferMemory memory(context);
            memory.AcquireRegistered();
            us[0] = since(at);
            for (std::size_t read = 0; read < reads; ++read) memory.AddReadable(reinterpret_cast<std::uint64_t>(blocks[(read * 31) % runs]) + 256 + 16 * read, 64);
            for (std::size_t write = 0; write < writes; ++write) memory.AddWritable(reinterpret_cast<std::uint64_t>(blocks[(write * 17 + 5) % runs]) + 4096, 64);
            for (std::size_t snapshot = 0; snapshot < snapshots; ++snapshot) memory.AddSnapshot({reinterpret_cast<std::uint64_t>(own.get()) + snapshot * 4096, std::span<const std::byte>(own.get() + snapshot * 4096, 256)});
            GuestBufferMemory::CountAddressBuild(0);
            memory.UploadPrepare(true);
            us[1] = since(at);
            memory.UploadFinish(true);
            us[2] = since(at);
            {
                BdaResources bda(context, memory);
                us[3] = since(at);
            }
            memory.WriteBack();
            us[4] = since(at);
        }
        us[5] = since(at);
        for (std::size_t phase = 0; phase < us.size(); ++phase) totals[phase] += us[phase];
        if (build < epochBuilds) continue;
        auto& bucket = epochStart ? firstEpochBuild : laterBuilds;
        (epochStart ? firsts : laters) += 1;
        for (std::size_t phase = 0; phase < us.size(); ++phase) bucket[phase] += us[phase];
    }
    const auto print = [](const char* label, const std::array<double, 6>& sums, std::size_t count) {
        if (count == 0) return;
        const auto per = [&](std::size_t phase) { return sums[phase] / static_cast<double>(count); };
        std::printf("[perdraw-bench] %s (%zu builds), us per build: acquire %.1f, prepare %.1f, finish %.1f, bda %.1f, write-back %.1f, release %.1f, total %.1f\n", label, count, per(0), per(1), per(2), per(3), per(4), per(5), per(0) + per(1) + per(2) + per(3) + per(4) + per(5));
    };
    std::printf("[perdraw-bench] %zu heap runs of %zu KiB, %zu registered ranges, image %d (writable stand-in %zu MiB), %zu builds, epoch every %zu, %zu dirty heap blocks and %zu image bytes per build, %zu reads, %zu writes, %zu snapshots%s\n", runs, runBytes >> 10u, registered, image ? 1 : 0, imageBytes >> 20u, builds, epochBuilds, dirtyBlocks, imageDirty, reads, writes, snapshots, snapshotVary ? " (varying)" : "");
    print("all", totals, builds);
    print("first build of an epoch", firstEpochBuild, firsts);
    print("later builds of an epoch", laterBuilds, laters);
    {
        GuestAllocations::Mutation mutation;
        for (auto* block : blocks) mutation.Remove(block);
    }
    ClearImageMirrors(context.device);
    ClearSnapshotCopies(context.device);
}
