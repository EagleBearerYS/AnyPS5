#include "NewPathFixture.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "VulkanTestDevice.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <string>
#include <thread>

namespace AgcDriver {

struct VulkanDeviceTestAccess {
    static Graphics::Context ContextOf(const VulkanDevice& device) { return device.graphicsContext(); }
};

}

#ifdef _WIN32

namespace {

namespace Graphics = AgcDriver::Graphics;
using namespace NewPath;
using Clock = std::chrono::steady_clock;

constexpr std::size_t RangeBytes = 0x10000;
constexpr const char* PinWaitMs = "1500";
constexpr auto Blocked = std::chrono::milliseconds(200);
constexpr auto Prompt = std::chrono::milliseconds(1000);
constexpr auto HolderLimit = std::chrono::milliseconds(4000);

int failures = 0;

void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

class HostRange {
public:
    explicit HostRange(bool arena) : arena(arena) {
        if (arena) {
            base = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(RangeBytes, RangeBytes));
            Require(base != nullptr, "pin wait dropped space: cannot reserve guest arena memory");
            GuestArena::GuestArenaCommit_nid_postfix(base, RangeBytes, PAGE_READWRITE, 16384);
        } else {
            base = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, RangeBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
            Require(base != nullptr, "pin wait dropped space: cannot allocate host memory");
        }
        GuestAllocations::Mutation().Add(base, RangeBytes, true, true, true);
        registered = true;
    }

    ~HostRange() {
        try {
            if (registered) GuestAllocations::Mutation().Remove(base);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "FAIL: pin wait dropped space: a range could not be removed at the end: %s\n", error.what());
            ++failures;
        }
        if (arena) {
            GuestArena::GuestArenaReset_nid_postfix(base, RangeBytes);
            GuestArena::GuestArenaRelease_nid_postfix(base, RangeBytes);
        } else {
            VirtualFree(base, 0, MEM_RELEASE);
        }
    }

    HostRange(const HostRange&) = delete;
    HostRange& operator=(const HostRange&) = delete;

    std::string Remove() {
        try {
            GuestAllocations::Mutation().Remove(base);
            registered = false;
        } catch (const std::exception& error) {
            return error.what();
        }
        return {};
    }

    std::uint64_t Address() const { return reinterpret_cast<std::uint64_t>(base); }

private:
    bool arena = false;
    std::uint8_t* base = nullptr;
    bool registered = false;
};

void BuildLocked(const Graphics::Context& context) {
    Graphics::GuestBufferMemory memory(context);
    memory.AcquireRegistered();
    memory.Upload();
    static_cast<void>(memory.PageTable(context));
    memory.Release();
}

void DroppedSpace(const Graphics::Context& context) {
    HostRange imported(false);
    HostRange cached(true);
    Require(Graphics::HostImportFor(context, imported.Address(), RangeBytes) != nullptr, "pin wait dropped space: the first range was not imported");
    {
        std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
        BuildLocked(context);
    }
    std::atomic<int> stage{0};
    std::atomic<bool> release{false};
    std::string importedRemoval;
    std::thread holder([&] {
        std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
        stage.store(1);
        while (stage.load() < 2) std::this_thread::yield();
        BuildLocked(context);
        stage.store(3);
        const auto until = Clock::now() + HolderLimit;
        while (!release.load() && Clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    });
    while (stage.load() < 1) std::this_thread::yield();
    std::thread remover([&] { importedRemoval = imported.Remove(); });
    std::this_thread::sleep_for(Blocked);
    stage.store(2);
    while (stage.load() < 3) std::this_thread::yield();
    const auto spaces = Graphics::AddressSpaceCounters();
    const auto start = Clock::now();
    const auto failure = cached.Remove();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);
    release.store(true);
    holder.join();
    remover.join();
    const std::string what = "removing a range held only by the cached address space, while the pin release worker waits for the device lock on behalf of another range's removal";
    Expect(failure.empty(), what + ": the removal failed after " + std::to_string(elapsed.count()) + " ms: " + failure);
    Expect(elapsed < Prompt, what + ": the removal took " + std::to_string(elapsed.count()) + " ms");
    Expect(Graphics::AddressSpaceCounters().waiterDrops > spaces.waiterDrops, what + ": the pin wait did not drop the cached address space");
}

}

#endif

int main() {
#ifdef _WIN32
    _putenv_s("APS5_PIN_WAIT_MS", PinWaitMs);
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        const auto context = AgcDriver::VulkanDeviceTestAccess::ContextOf(*device);
        if (context.hostImportAlignment == 0 || !context.bufferDeviceAddress) {
            std::puts("pin wait dropped space: host imports or buffer device addresses unavailable");
            return VulkanTestSkipped;
        }
        DroppedSpace(context);
    } catch (const std::exception& error) {
        Expect(false, std::string("exception: ") + error.what());
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d pin wait dropped space checks failed\n", failures);
        return 1;
    }
    std::puts("pin wait dropped space tests passed");
    return 0;
#else
    return VulkanTestSkipped;
#endif
}
