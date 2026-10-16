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

constexpr std::size_t RangeBytes = 0xc000;
constexpr std::size_t Granule = 65536;
constexpr std::uint32_t Builders = 4;
constexpr std::uint64_t WarmBuilds = 8;
constexpr std::uint64_t LaterBuilds = 16;
constexpr auto Hold = std::chrono::milliseconds(2);
constexpr auto PromptRemoval = std::chrono::milliseconds(1000);
constexpr auto Deadline = std::chrono::seconds(10);

int failures = 0;

void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

class HostRange {
public:
    HostRange() {
        memory = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, Granule, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        Require(memory != nullptr, "pin wait builds: cannot allocate a host range");
        std::memset(memory, 0x3c, Granule);
        GuestAllocations::Mutation().Add(memory, RangeBytes, true, true, true);
    }

    ~HostRange() {
        if (registered) {
            try {
                GuestAllocations::Mutation().Remove(memory);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: pin wait builds: a range could not be removed at the end: %s\n", error.what());
            }
        }
        VirtualFree(memory, 0, MEM_RELEASE);
    }

    HostRange(const HostRange&) = delete;
    HostRange& operator=(const HostRange&) = delete;

    void* Data() const { return memory; }
    void Unregistered() { registered = false; }

private:
    std::uint8_t* memory = nullptr;
    bool registered = true;
};

std::uint64_t Total(const std::array<std::atomic<std::uint64_t>, Builders>& builds) {
    std::uint64_t total = 0;
    for (const auto& count : builds) total += count.load();
    return total;
}

bool WaitFor(const std::function<bool()>& reached) {
    const auto end = std::chrono::steady_clock::now() + Deadline;
    while (!reached()) {
        if (std::chrono::steady_clock::now() >= end) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

void RemovalBetweenBuilds(const Graphics::Context& context) {
    HostRange stable;
    HostRange removed;
    {
        std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
        Graphics::GuestBufferMemory memory(context);
        memory.AcquireRegistered();
    }
    std::atomic<bool> stop{false};
    std::array<std::atomic<std::uint64_t>, Builders> builds{};
    std::mutex failureMutex;
    std::string buildFailure;
    std::vector<std::thread> builders;
    for (std::uint32_t builder = 0; builder < Builders; ++builder) {
        builders.emplace_back([&, builder] {
            std::this_thread::sleep_for(Hold * builder / Builders);
            try {
                while (!stop.load()) {
                    Graphics::GuestBufferMemory memory(context);
                    memory.AcquireRegistered();
                    std::this_thread::sleep_for(Hold);
                    builds[builder].fetch_add(1);
                }
            } catch (const std::exception& error) {
                std::lock_guard lock(failureMutex);
                if (buildFailure.empty()) buildFailure = "builder " + std::to_string(builder) + ": " + error.what();
            }
        });
    }
    const bool warm = WaitFor([&] {
        for (const auto& count : builds) {
            if (count.load() < WarmBuilds) return false;
        }
        return true;
    });
    std::string removal;
    std::chrono::milliseconds elapsed{0};
    std::uint64_t before = 0;
    if (warm) {
        before = Total(builds);
        const auto start = std::chrono::steady_clock::now();
        try {
            GuestAllocations::Mutation().Remove(removed.Data());
            removed.Unregistered();
        } catch (const std::exception& error) {
            removal = error.what();
        }
        elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    }
    const auto removedAt = Total(builds);
    const bool continued = warm && WaitFor([&] { return Total(builds) >= removedAt + LaterBuilds; });
    stop.store(true);
    for (auto& thread : builders) thread.join();
    const std::string what = "a guest removal of a registered range while " + std::to_string(Builders) + " threads build address spaces without the device lock, each holding its lease " + std::to_string(Hold.count()) + " ms";
    Expect(warm, what + ": the builders did not each finish " + std::to_string(WarmBuilds) + " builds within " + std::to_string(Deadline.count()) + " s");
    Expect(buildFailure.empty(), what + ": a build failed: " + buildFailure);
    Expect(removal.empty(), what + ": the removal failed after " + std::to_string(elapsed.count()) + " ms: " + removal);
    Expect(elapsed < PromptRemoval, what + ": the removal took " + std::to_string(elapsed.count()) + " ms (" + std::to_string(removedAt - before) + " builds finished meanwhile)");
    Expect(continued, what + ": the builders finished " + std::to_string(Total(builds) - removedAt) + " builds after the removal, expected " + std::to_string(LaterBuilds) + " within " + std::to_string(Deadline.count()) + " s");
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
            std::puts("pin wait builds: host imports unavailable");
            return VulkanTestSkipped;
        }
        RemovalBetweenBuilds(context);
    } catch (const std::exception& error) {
        Expect(false, std::string("exception: ") + error.what());
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d pin wait build checks failed\n", failures);
        return 1;
    }
    std::puts("pin wait build tests passed");
    return 0;
#else
    return VulkanTestSkipped;
#endif
}
