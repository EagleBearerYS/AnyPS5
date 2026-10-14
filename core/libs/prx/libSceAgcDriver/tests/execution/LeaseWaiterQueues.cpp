#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "VulkanTestDevice.hpp"
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace AgcDriver {

struct VulkanDeviceTestAccess {
    static Graphics::Context ContextOf(const VulkanDevice& device) { return device.graphicsContext(); }
};

}

namespace {

using AgcDriver::Graphics::Require;

void keepLease(AgcDriver::Graphics::Recorder& recorder) {
    std::lock_guard lock(AgcDriver::GuestMemory::GpuMutex());
    recorder.Keep(std::make_shared<GuestAllocations::Lease>(GuestAllocations::GuestAllocationsAcquire_nid_postfix()));
    AgcDriver::Graphics::CountLeaseOutcome(false, recorder.CallerToken());
}

}

int main() {
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        auto* recorder = AgcDriver::Graphics::Recorder::Active();
        if (recorder == nullptr || recorder->Streams() < 2 || !recorder->HasTimeline()) {
            std::puts("skipped, the device records one stream");
            return VulkanTestSkipped;
        }
        std::vector<std::uint8_t> memory(1u << 16);
        const auto context = AgcDriver::VulkanDeviceTestAccess::ContextOf(*device);
        static_cast<void>(AgcDriver::Graphics::HostImportFor(context, reinterpret_cast<std::uint64_t>(memory.data()), 64));
        GuestAllocations::Mutation().Add(memory.data(), memory.size(), true, true, false);
        std::exception_ptr failure;
        std::thread other([&] {
            try {
                AgcDriver::GuestMemory::TagGpuLockThread(0x20);
                keepLease(*recorder);
            } catch (...) {
                failure = std::current_exception();
            }
        });
        other.join();
        if (failure) std::rethrow_exception(failure);
        keepLease(*recorder);
        const auto before = AgcDriver::Graphics::LeaseCounters();
        GuestAllocations::Mutation().Remove(memory.data());
        const auto after = AgcDriver::Graphics::LeaseCounters();
        Require(after.contentionWaits > before.contentionWaits && after.contentionSyncs > before.contentionSyncs, "lease waiter: the guest free did not wait for the lease batches");
        Require(after.contentionDrains == before.contentionDrains, "lease waiter: two queues' lease batches were finished by draining the recorder (" + std::to_string(after.contentionDrains - before.contentionDrains) + " drains), not by their tokens");
        {
            std::lock_guard lock(AgcDriver::GuestMemory::GpuMutex());
            recorder->Sync();
        }
        std::puts("lease waiter tests passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "lease waiter test failed: %s\n", error.what());
        return 1;
    }
}
