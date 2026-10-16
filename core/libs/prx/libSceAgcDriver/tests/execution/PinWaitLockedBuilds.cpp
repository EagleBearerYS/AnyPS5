#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "VulkanTestDevice.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace AgcDriver {

struct VulkanDeviceTestAccess {
    static Graphics::Context ContextOf(const VulkanDevice& device) { return device.graphicsContext(); }
};

}

#ifdef _WIN32

namespace {

namespace Graphics = AgcDriver::Graphics;
using Graphics::Require;

constexpr std::size_t SmallBytes = 0x10000;
constexpr std::size_t SplitBytes = 0x40000;
constexpr std::size_t PieceOffset = 0x18000;
constexpr std::size_t PieceBytes = 0x10000;
constexpr std::uint64_t WarmBuilds = 8;
constexpr std::uint64_t LaterBuilds = 16;
constexpr auto Hold = std::chrono::milliseconds(2);
constexpr auto PromptChange = std::chrono::milliseconds(1000);
constexpr auto Deadline = std::chrono::seconds(10);

int failures = 0;

void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

class HostRange {
public:
    explicit HostRange(std::size_t bytes) : bytes(bytes) {
        memory = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        Require(memory != nullptr, "pin wait locked builds: cannot allocate a host range");
        std::memset(memory, 0x3c, bytes);
        GuestAllocations::Mutation().Add(memory, bytes, true, true, true);
    }

    ~HostRange() {
        for (std::size_t offset = 0; offset < bytes; offset += 0x4000u) {
            try {
                GuestAllocations::Mutation mutation;
                if (mutation.Covers(memory + offset, 1u)) mutation.Remove(memory + offset);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: pin wait locked builds: a range could not be removed at the end: %s\n", error.what());
                break;
            }
        }
        VirtualFree(memory, 0, MEM_RELEASE);
    }

    HostRange(const HostRange&) = delete;
    HostRange& operator=(const HostRange&) = delete;

    std::uint8_t* Data() const { return memory; }
    std::uint64_t Address(std::size_t offset = 0) const { return reinterpret_cast<std::uint64_t>(memory) + offset; }

private:
    std::uint8_t* memory = nullptr;
    std::size_t bytes = 0;
};

class Builders {
public:
    explicit Builders(const Graphics::Context& context) {
        for (std::uint32_t builder = 0; builder < Count; ++builder) {
            threads.emplace_back([this, &context, builder] {
                std::this_thread::sleep_for(Hold * builder / Count);
                try {
                    while (!stop.load()) {
                        Graphics::GuestBufferMemory memory(context);
                        {
                            std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
                            memory.AcquireRegistered();
                        }
                        std::this_thread::sleep_for(Hold);
                        builds[builder].fetch_add(1);
                    }
                } catch (const std::exception& error) {
                    std::lock_guard lock(failureMutex);
                    if (failure.empty()) failure = "builder " + std::to_string(builder) + ": " + error.what();
                }
            });
        }
    }

    ~Builders() { Stop(); }

    Builders(const Builders&) = delete;
    Builders& operator=(const Builders&) = delete;

    void Stop() {
        stop.store(true);
        for (auto& thread : threads) {
            if (thread.joinable()) thread.join();
        }
    }

    std::uint64_t Total() const {
        std::uint64_t total = 0;
        for (const auto& count : builds) total += count.load();
        return total;
    }

    bool Warm() const {
        return WaitFor([&] {
            for (const auto& count : builds) {
                if (count.load() < WarmBuilds) return false;
            }
            return true;
        });
    }

    bool Continue(std::uint64_t from) const {
        return WaitFor([&] { return Total() >= from + LaterBuilds; });
    }

    std::string Failure() {
        std::lock_guard lock(failureMutex);
        return failure;
    }

    static bool WaitFor(const std::function<bool()>& reached) {
        const auto end = std::chrono::steady_clock::now() + Deadline;
        while (!reached()) {
            if (std::chrono::steady_clock::now() >= end) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    static constexpr std::uint32_t Count = 4;

private:
    std::atomic<bool> stop{false};
    std::array<std::atomic<std::uint64_t>, Count> builds{};
    std::mutex failureMutex;
    std::string failure;
    std::vector<std::thread> threads;
};

struct Change {
    std::string failure;
    std::chrono::milliseconds elapsed{0};
};

Change Timed(const std::function<void()>& change) {
    Change result;
    const auto start = std::chrono::steady_clock::now();
    try {
        change();
    } catch (const std::exception& error) {
        result.failure = error.what();
    }
    result.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    return result;
}

void ExpectPrompt(Builders& builders, const Change& change, std::uint64_t before, const std::string& what) {
    const auto after = builders.Total();
    Expect(change.failure.empty(), what + ": the change failed after " + std::to_string(change.elapsed.count()) + " ms: " + change.failure);
    Expect(change.elapsed < PromptChange, what + ": the change took " + std::to_string(change.elapsed.count()) + " ms (" + std::to_string(after - before) + " builds finished meanwhile), expected under " + std::to_string(PromptChange.count()) + " ms");
    Expect(builders.Continue(after), what + ": the builders did not finish " + std::to_string(LaterBuilds) + " builds within " + std::to_string(Deadline.count()) + " s after the change");
}

bool Leased(std::uint64_t address, std::size_t bytes) {
    const auto lease = GuestAllocations::GuestAllocationsAcquire_nid_postfix();
    return std::any_of(lease.begin(), lease.end(), [&](const auto& range) { return range->address == address && range->bytes == bytes && range->readable; });
}

void ChangesUnderLockedBuilds(const Graphics::Context& context) {
    HostRange stable(SmallBytes);
    HostRange protectedRange(SmallBytes);
    HostRange split(SplitBytes);
    HostRange removed(SmallBytes);
    {
        std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
        Graphics::GuestBufferMemory memory(context);
        memory.AcquireRegistered();
    }
    Builders builders(context);
    const std::string shape = "while " + std::to_string(Builders::Count) + " threads build address spaces under the device lock, each holding its lease " + std::to_string(Hold.count()) + " ms";
    const bool warm = builders.Warm();
    Expect(warm, shape + ": the builders did not each finish " + std::to_string(WarmBuilds) + " builds within " + std::to_string(Deadline.count()) + " s");
    if (warm) {
        auto before = builders.Total();
        const auto reserved = Timed([&] { GuestAllocations::Mutation().Protect(protectedRange.Data(), SmallBytes, false, false, false, [] {}); });
        ExpectPrompt(builders, reserved, before, "a guest protection of a registered range to no access and no GPU mapping " + shape);

        before = builders.Total();
        const auto unmapped = Timed([&] { GuestAllocations::Mutation().Unmap(split.Data() + PieceOffset, PieceBytes, [](const void*, std::size_t, const void*, bool) {}); });
        const std::string piece = "a guest unmap of 64 KiB in the middle of a 256 KiB registered range " + shape;
        ExpectPrompt(builders, unmapped, before, piece);
        Expect(Leased(split.Address(), PieceOffset) && Leased(split.Address(PieceOffset + PieceBytes), SplitBytes - PieceOffset - PieceBytes), piece + ": a later lease does not hold the two remaining pieces, readable, with their bounds");
        Expect(!Leased(split.Address(PieceOffset), PieceBytes), piece + ": a later lease still holds the unmapped piece");

        before = builders.Total();
        const auto gone = Timed([&] { GuestAllocations::Mutation().Remove(removed.Data()); });
        ExpectPrompt(builders, gone, before, "a guest removal of a registered range " + shape);
    }
    builders.Stop();
    Expect(builders.Failure().empty(), shape + ": a build failed: " + builders.Failure());
}

}

#endif

int main() {
#ifdef _WIN32
    _putenv_s("APS5_PIN_WAIT_MS", "2000");
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        const auto context = AgcDriver::VulkanDeviceTestAccess::ContextOf(*device);
        if (context.hostImportAlignment == 0) {
            std::puts("pin wait locked builds: host imports unavailable");
            return VulkanTestSkipped;
        }
        ChangesUnderLockedBuilds(context);
    } catch (const std::exception& error) {
        Expect(false, std::string("exception: ") + error.what());
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d pin wait locked build checks failed\n", failures);
        return 1;
    }
    std::puts("pin wait locked build tests passed");
    return 0;
#else
    return VulkanTestSkipped;
#endif
}
