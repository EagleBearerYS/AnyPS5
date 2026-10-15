#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ShaderResources.hpp"
#include "prx/libSceAgcDriver/Submit/include/Acb.hpp"
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
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;

constexpr std::uint32_t TextureBytes = 2048;
constexpr std::uint32_t Aliases = 256;
constexpr std::uint32_t Format8888UNorm = 56;
constexpr std::uint32_t Type2D = 9;
constexpr std::size_t SrtBytes = 256;

constexpr std::array<std::uint32_t, 17> TableCode{0xf4080100u, 0xfa000000u, 0xf4080200u, 0xfa000010u, 0xf4080300u, 0xfa000020u, 0xf4080700u, 0xfa000030u, 0x7e200500u, 0x8f108510u, 0xf42c0502u, 0x20000000u, 0xf09c0f08u, 0x00450000u, 0xe0700000u, 0x80070000u, 0xbf810000u};

std::array<std::uint32_t, 4> V(std::uint64_t address, std::uint32_t stride, std::uint32_t records) {
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (stride << 16u), records, stride == 0u ? 0x31016facu : 0x01016facu};
}

std::vector<std::uint32_t> Pm4(std::uint32_t opcode, std::initializer_list<std::uint32_t> body) {
    std::vector<std::uint32_t> words{0xc0000000u | (static_cast<std::uint32_t>(body.size() - 1) << 16u) | (opcode << 8u)};
    words.insert(words.end(), body);
    return words;
}

std::uint32_t BenchSetting(const char* name, std::uint32_t fallback) {
    const char* value = std::getenv(name);
    return value != nullptr ? static_cast<std::uint32_t>(std::strtoul(value, nullptr, 10)) : fallback;
}

constexpr std::array<std::uint32_t, 7> BenchStoreCode{0x7e020d00u, 0x082802f7u, 0x7e3c0300u, 0x7e3e0280u, 0xf0201108u, 0x0001141eu, 0xbf810000u};

class BenchBlock {
public:
    explicit BenchBlock(std::size_t bytes) : bytes((bytes + 65535u) & ~static_cast<std::size_t>(65535)) {
#ifdef _WIN32
        block = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(this->bytes, 65536));
        if (block != nullptr) GuestArena::GuestArenaCommit_nid_postfix(block, this->bytes, PAGE_READWRITE, this->bytes);
#else
        constexpr std::uintptr_t alignment = 65536;
        void* mapped = mmap(nullptr, this->bytes + alignment, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapped != MAP_FAILED) {
            const auto begin = reinterpret_cast<std::uintptr_t>(mapped);
            const auto aligned = (begin + alignment - 1) & ~(alignment - 1);
            if (aligned != begin) munmap(mapped, aligned - begin);
            if (aligned + this->bytes != begin + this->bytes + alignment) munmap(reinterpret_cast<void*>(aligned + this->bytes), begin + alignment - aligned);
            block = reinterpret_cast<std::uint8_t*>(aligned);
            GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(block, this->bytes);
        }
#endif
        Require(block != nullptr, "image table bench: cannot allocate the guest block");
        GuestAllocations::Mutation().Add(block, this->bytes, true, true, true);
    }

    BenchBlock(const BenchBlock&) = delete;
    BenchBlock& operator=(const BenchBlock&) = delete;

    std::uint64_t At(std::size_t offset) const { return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(block + offset)); }
    std::uint8_t* Bytes(std::size_t offset) const { return block + offset; }

private:
    std::size_t bytes;
    std::uint8_t* block = nullptr;
};

struct BenchLayout {
    std::uint32_t records, stride, elements, threads, outputs, storage, heaps;
    std::size_t storeCode = 256, srts = 4096, outputsAt = 0, heap = 0, heapBytes = 0, textures = 0, storageAt = 0, total = 0;

    void Place() {
        outputsAt = srts + static_cast<std::size_t>(threads) * outputs * SrtBytes;
        heap = (outputsAt + static_cast<std::size_t>(threads) * outputs * 4096u + 65535u) & ~static_cast<std::size_t>(65535);
        heapBytes = (static_cast<std::size_t>(records) * stride + 65535u) & ~static_cast<std::size_t>(65535);
        textures = heap + heapBytes * heaps;
        storageAt = (textures + static_cast<std::size_t>((elements + Aliases - 1u) / Aliases) * TextureBytes + 65535u) & ~static_cast<std::size_t>(65535);
        total = storageAt + static_cast<std::size_t>(threads) * storage * 1024u;
    }
};

std::vector<std::uint32_t> BenchTableDispatch(const BenchBlock& block, const BenchLayout& layout, std::uint32_t thread, std::uint32_t output) {
    const auto kernel = block.At(0);
    const auto srt = block.At(layout.srts + (static_cast<std::size_t>(thread) * layout.outputs + output) * SrtBytes);
    std::vector<std::uint32_t> words;
    for (const auto& packet : {Pm4(0x76, {0x20c, static_cast<std::uint32_t>(kernel >> 8u)}), Pm4(0x76, {0x20d, static_cast<std::uint32_t>(kernel >> 40u) & 0xffu}), Pm4(0x76, {0x207, 64}), Pm4(0x76, {0x208, 1}), Pm4(0x76, {0x209, 1}), Pm4(0x76, {0x213, 2u << 1u}), Pm4(0x76, {0x240, static_cast<std::uint32_t>(srt)}), Pm4(0x76, {0x241, static_cast<std::uint32_t>(srt >> 32u)}), Pm4(0x15, {1, 1, 1, 0x41})}) {
        words.insert(words.end(), packet.begin(), packet.end());
    }
    return words;
}

std::vector<std::uint32_t> BenchStoreDispatch(const BenchBlock& block, const BenchLayout& layout, std::uint32_t thread, std::uint32_t image) {
    const auto kernel = block.At(layout.storeCode);
    const auto texels = block.At(layout.storageAt + (static_cast<std::size_t>(thread) * layout.storage + image) * 1024u);
    const std::array<std::uint32_t, 4> t{static_cast<std::uint32_t>(texels >> 8u), static_cast<std::uint32_t>((texels >> 40u) & 0xffu) | (22u << 20u) | ((63u & 3u) << 30u), (63u >> 2u) | (3u << 14u) | (1u << 31u), 0xfacu | (Type2D << 28u)};
    std::vector<std::uint32_t> words;
    for (const auto& packet : {Pm4(0x76, {0x20c, static_cast<std::uint32_t>(kernel >> 8u)}), Pm4(0x76, {0x20d, static_cast<std::uint32_t>(kernel >> 40u) & 0xffu}), Pm4(0x76, {0x207, 64}), Pm4(0x76, {0x208, 1}), Pm4(0x76, {0x209, 1}), Pm4(0x76, {0x213, 16u << 1u})}) {
        words.insert(words.end(), packet.begin(), packet.end());
    }
    for (std::uint32_t slot = 0; slot < 16; ++slot) {
        const auto packet = Pm4(0x76, {0x240 + slot, slot >= 4 && slot < 8 ? t[slot - 4] : 0u});
        words.insert(words.end(), packet.begin(), packet.end());
    }
    const auto dispatch = Pm4(0x15, {1, 1, 1, 0x41});
    words.insert(words.end(), dispatch.begin(), dispatch.end());
    return words;
}

void BenchSubmit(std::uint32_t queue, std::vector<std::uint32_t>& words) {
    Packet packet{words.data(), static_cast<std::uint32_t>(words.size()), 0, {}};
    Require((queue == 0 ? sceAgcDriverSubmitDcb(&packet) : sceAgcDriverSubmitAcb(queue, &packet)) == 0, "image table bench: a submission failed");
}

int VillageBench() {
    BenchLayout layout{BenchSetting("BENCH_RECORDS", 32768), BenchSetting("BENCH_STRIDE", 48), BenchSetting("BENCH_ELEMENTS", 4096), std::max(1u, BenchSetting("BENCH_THREADS", 3)), std::max(1u, BenchSetting("BENCH_OUTPUTS", 16)), BenchSetting("BENCH_STORAGE", 8), std::max(1u, BenchSetting("BENCH_HEAPS", 1))};
    layout.Place();
    const auto batch = std::max(1u, BenchSetting("BENCH_BATCH", 8));
    const auto seconds = std::max(1u, BenchSetting("BENCH_SECONDS", 21));
    const auto writeEvery = BenchSetting("BENCH_WRITE_EVERY", 0);
    BenchBlock block(layout.total);
    auto code = TableCode;
    if (layout.stride != 32u) {
        Require(layout.stride <= 64u, "image table bench: BENCH_STRIDE above 64 needs a literal multiply");
        std::replace(code.begin(), code.end(), 0x8f108510u, 0x93108010u | (layout.stride << 8u));
    }
    std::memcpy(block.Bytes(0), code.data(), sizeof(code));
    std::memcpy(block.Bytes(layout.storeCode), BenchStoreCode.data(), sizeof(BenchStoreCode));
    for (std::uint32_t surface = 0; surface < (layout.elements + Aliases - 1u) / Aliases; ++surface) std::memset(block.Bytes(layout.textures + static_cast<std::size_t>(surface) * TextureBytes), static_cast<int>(surface), TextureBytes);
    const auto setRecord = [&](std::uint32_t heap, std::uint32_t record, std::uint32_t element) {
        const auto address = block.At(layout.textures + static_cast<std::size_t>(element / Aliases) * TextureBytes);
        const std::array<std::uint32_t, 8> words{static_cast<std::uint32_t>(address >> 8u), static_cast<std::uint32_t>((address >> 40u) & 0xffu) | (Format8888UNorm << 20u) | (3u << 30u), 7u << 14u, 0xfacu | (Type2D << 28u), 0u, 0u, element % Aliases, 0u};
        std::memcpy(block.Bytes(layout.heap + heap * layout.heapBytes + static_cast<std::size_t>(record) * layout.stride), words.data(), sizeof(words));
    };
    for (std::uint32_t heap = 0; heap < layout.heaps; ++heap) {
        for (std::uint32_t record = 0; record < layout.records; ++record) setRecord(heap, record, record % layout.elements);
    }
    for (std::uint32_t thread = 0; thread < layout.threads; ++thread) {
        for (std::uint32_t output = 0; output < layout.outputs; ++output) {
            auto* srt = reinterpret_cast<std::uint32_t*>(block.Bytes(layout.srts + (static_cast<std::size_t>(thread) * layout.outputs + output) * SrtBytes));
            std::fill(srt, srt + SrtBytes / 4u, 0u);
            const auto heap = V(block.At(layout.heap + ((static_cast<std::size_t>(thread) * layout.outputs + output) % layout.heaps) * layout.heapBytes), layout.stride, layout.records);
            const auto out = V(block.At(layout.outputsAt + (static_cast<std::size_t>(thread) * layout.outputs + output) * 4096u), 0u, 4096u);
            std::copy(heap.begin(), heap.end(), srt);
            std::copy(out.begin(), out.end(), srt + 12);
        }
    }
    std::printf("VILLAGEBENCH heap watched %d, collect %llu, write-watched %d\n", AgcDriver::GuestMemory::Watched(block.At(layout.heap), static_cast<std::size_t>(layout.records) * layout.stride) ? 1 : 0, static_cast<unsigned long long>(AgcDriver::GuestMemory::CollectWrites(block.At(layout.heap), static_cast<std::size_t>(layout.records) * layout.stride)), AgcDriver::GuestMemory::WriteWatched() ? 1 : 0);
    const auto perRound = std::max(1u, BenchSetting("BENCH_ROUND", 16));
    std::atomic<std::uint64_t> dispatches{0}, stores{0}, writes{0};
    std::vector<std::uint32_t> outputs(layout.threads, 0u), images(layout.threads, 0u);
    std::uint32_t round = 0;
    const auto started = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - started < std::chrono::seconds(seconds)) {
        if (writeEvery != 0 && ++round % writeEvery == 0) {
            setRecord(round % layout.heaps, (round * 7919u) % layout.records, (round * 31u) % layout.elements);
            writes.fetch_add(1, std::memory_order_relaxed);
        }
        std::vector<std::thread> workers;
        for (std::uint32_t thread = 0; thread < layout.threads; ++thread) {
            workers.emplace_back([&, thread] {
                const std::uint32_t queue = thread == 0 ? 0u : 0x20u + thread;
                for (std::uint32_t submission = 0; submission < perRound; ++submission) {
                    std::vector<std::uint32_t> words;
                    for (std::uint32_t index = 0; index < batch; ++index) {
                        const auto table = BenchTableDispatch(block, layout, thread, outputs[thread]);
                        words.insert(words.end(), table.begin(), table.end());
                        outputs[thread] = (outputs[thread] + 1u) % layout.outputs;
                    }
                    if (layout.storage != 0) {
                        const auto store = BenchStoreDispatch(block, layout, thread, images[thread]);
                        words.insert(words.end(), store.begin(), store.end());
                        images[thread] = (images[thread] + 1u) % layout.storage;
                        stores.fetch_add(1, std::memory_order_relaxed);
                    }
                    BenchSubmit(queue, words);
                    dispatches.fetch_add(batch, std::memory_order_relaxed);
                }
            });
        }
        for (auto& worker : workers) worker.join();
        AgcDriverWaitIdle_nid_postfix();
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("VILLAGEBENCH records %u stride %u elements %u threads %u outputs %u storage %u heaps %u batch %u write-every %u: %llu table dispatches, %llu stores, %llu table writes in %.1f s (%.0f table dispatches/s)\n", layout.records, layout.stride, layout.elements, layout.threads, layout.outputs, layout.storage, layout.heaps, batch, writeEvery, static_cast<unsigned long long>(dispatches.load()), static_cast<unsigned long long>(stores.load()), static_cast<unsigned long long>(writes.load()), elapsed, static_cast<double>(dispatches.load()) / elapsed);
    std::fflush(stdout);
    AgcDriverShutdown_nid_postfix();
    return 0;
}
}

int main(int argc, char** argv) {
    if (argc != 2 || std::string(argv[1]) != "--village-bench") {
        std::puts("image table village bench: run with --village-bench");
        return 0;
    }
    try {
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
        }
        return VillageBench();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        AgcDriverShutdown_nid_postfix();
        return 1;
    }
}
