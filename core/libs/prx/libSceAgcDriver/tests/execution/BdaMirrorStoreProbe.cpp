#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libc/include/GuestWriteWatch.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;

constexpr std::uint32_t Threads = 64;
constexpr std::uint64_t Block = 65536;
constexpr std::uint64_t HeapBytes = 2u << 20u;
constexpr std::uint64_t StoreOffset = 5 * Block + 0x200;
constexpr std::uint32_t Marker = 0x5a000000u;

alignas(256) constexpr std::array<std::uint32_t, 6> StoreCode{0x34020082, 0x380402ff, Marker, 0xdc708000, 0x00000201, 0xbf810000};

class GuestHeap {
public:
    GuestHeap() {
#ifdef _WIN32
        raw = GuestArena::GuestArenaAllocate_nid_postfix(HeapBytes, Block);
        Require(raw != nullptr, "BDA mirror store probe: cannot allocate the guest heap");
        base = static_cast<std::uint8_t*>(raw);
        GuestArena::GuestArenaCommit_nid_postfix(raw, HeapBytes, PAGE_READWRITE, HeapBytes);
#else
        raw = mmap(nullptr, HeapBytes + Block, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        Require(raw != MAP_FAILED, "BDA mirror store probe: cannot map the guest heap");
        base = reinterpret_cast<std::uint8_t*>((reinterpret_cast<std::uintptr_t>(raw) + Block - 1) / Block * Block);
#endif
        GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(base, HeapBytes);
        GuestAllocations::Mutation().Add(base, HeapBytes, true, true, true);
    }

    ~GuestHeap() {
        GuestAllocations::Mutation().Remove(base);
        GuestWriteWatch::GuestWriteWatchUnregister_nid_postfix(base, HeapBytes);
#ifdef _WIN32
        GuestArena::GuestArenaReset_nid_postfix(raw, HeapBytes);
        GuestArena::GuestArenaRelease_nid_postfix(raw, HeapBytes);
#else
        munmap(raw, HeapBytes + Block);
#endif
    }

    GuestHeap(const GuestHeap&) = delete;
    GuestHeap& operator=(const GuestHeap&) = delete;

    std::uint64_t Address() const { return reinterpret_cast<std::uintptr_t>(base); }

    std::uint32_t Load(std::uint64_t offset) const {
        std::uint32_t value = 0;
        std::memcpy(&value, base + offset, sizeof(value));
        return value;
    }

private:
    void* raw = nullptr;
    std::uint8_t* base = nullptr;
};

struct ImportBudget {
    explicit ImportBudget(std::uint64_t bytes) { AgcDriver::Graphics::SetHostImportBudget(bytes); }
    ~ImportBudget() { AgcDriver::Graphics::SetHostImportBudget(0); }
    ImportBudget(const ImportBudget&) = delete;
    ImportBudget& operator=(const ImportBudget&) = delete;
};

class StderrCapture {
public:
    StderrCapture() {
        std::fflush(stderr);
        file = std::tmpfile();
        Require(file != nullptr, "BDA mirror store probe: cannot open a temporary file for stderr");
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

std::uint32_t Stored(std::uint32_t thread) {
    return Marker | (thread * 4u);
}

std::string Store(AgcDriver::VulkanDevice& device, std::uint64_t address) {
    const std::vector<std::uint32_t> userData{static_cast<std::uint32_t>(address), static_cast<std::uint32_t>(address >> 32u)};
    const std::span<const std::uint32_t> code(StoreCode);
    const std::array<ShaderRecompiler::MemoryRegion, 1> regions{{{reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{Threads, 1, 1}, 0, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderRecompiler::ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
        {32, 0, userData, compute, std::nullopt, std::nullopt, regions},
        device.Target(),
        {0, 0, 0, 128}
    };
    request.useCache = false;
    const auto result = ShaderRecompiler::Recompile(request);
    StderrCapture log;
    std::string thrown;
    try {
        device.Dispatch(result, 1, 1, 1, {}, reinterpret_cast<std::uintptr_t>(code.data()));
        device.WaitIdle();
    } catch (const std::exception& error) {
        thrown = error.what();
    }
    return log.Finish() + thrown;
}

}

int main(int argc, char** argv) {
    try {
        const std::string_view mode = argc > 1 ? argv[1] : "";
        Require(mode.empty() || mode == "--expect-refused" || mode == "--expect-stored", "usage: agc_driver_bda_mirror_store_probe_tests [--expect-refused | --expect-stored]");
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        GuestHeap heap;
        const ImportBudget budget(AgcDriver::Graphics::HostImportLiveBytes() + Block);
        const auto log = Store(*device, heap.Address() + StoreOffset);
        const auto mirrors = AgcDriver::Graphics::HeapMirrorsWithin(heap.Address(), heap.Address() + HeapBytes).mirrors;
        const bool refused = log.find("read-only in the BDA table") != std::string::npos;
        const bool failed = log.find("BDA access failed") != std::string::npos || log.find("write-back failed") != std::string::npos;
        std::uint32_t landed = 0;
        for (std::uint32_t thread = 0; thread < Threads; ++thread) {
            if (heap.Load(StoreOffset + thread * sizeof(std::uint32_t)) == Stored(thread)) ++landed;
        }
        std::printf("BDA mirror store probe: %llu heap mirror(s) over the heap, refused as read-only: %s, %u of %u GLOBAL stores reached guest memory\n",
                static_cast<unsigned long long>(mirrors), refused ? "yes" : "no", landed, Threads);
        Require(mirrors == 1, "BDA mirror store probe: the writable heap was not heap mirrored, so the probe doesn't reach the case");
        if (mode == "--expect-refused") Require(refused && landed == 0, "BDA mirror store probe: a GLOBAL store into a writable heap mirror was not refused as read-only:\n" + log);
        if (mode == "--expect-stored") Require(!refused && !failed && landed == Threads, "BDA mirror store probe: a GLOBAL store into a writable heap mirror was refused or lost:\n" + log);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
