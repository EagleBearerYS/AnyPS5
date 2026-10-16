#include "NewPathFixture.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "VulkanTestDevice.hpp"
#include <cstdio>
#include <exception>
#include <memory>
#include <string>

namespace AgcDriver {

struct VulkanDeviceTestAccess {
    static Graphics::Context ContextOf(const VulkanDevice& device) { return device.graphicsContext(); }
};

}

#ifdef _WIN32

namespace {

namespace Graphics = AgcDriver::Graphics;
using namespace NewPath;

constexpr std::size_t RangeBytes = 0x10000;
constexpr const char* PinWaitMs = "300";

int failures = 0;

void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

class HostRange {
public:
    HostRange() {
        base = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, RangeBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        Require(base != nullptr, "pin wait holders: cannot allocate host memory");
        for (std::size_t word = 0; word < RangeBytes / 4u; ++word) reinterpret_cast<std::uint32_t*>(base)[word] = 0x51000000u | static_cast<std::uint32_t>(word);
        GuestAllocations::Mutation().Add(base, RangeBytes, true, true, true);
    }

    ~HostRange() {
        try {
            GuestAllocations::Mutation().Remove(base);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "FAIL: pin wait holders: the range could not be removed at the end: %s\n", error.what());
            ++failures;
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    HostRange(const HostRange&) = delete;
    HostRange& operator=(const HostRange&) = delete;

    std::uint64_t Address() const { return reinterpret_cast<std::uint64_t>(base); }
    void* Pointer() const { return base; }

    std::string Text() const {
        char text[64];
        std::snprintf(text, sizeof(text), "0x%llx+0x%llx", static_cast<unsigned long long>(Address()), static_cast<unsigned long long>(RangeBytes));
        return text;
    }

private:
    std::uint8_t* base = nullptr;
};

std::unique_ptr<Graphics::GuestBufferMemory> Acquire(const Graphics::Context& context) {
    std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
    auto memory = std::make_unique<Graphics::GuestBufferMemory>(context);
    memory->AcquireRegistered();
    return memory;
}

void Finish(const Graphics::Context& context, std::unique_ptr<Graphics::GuestBufferMemory>& memory) {
    std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
    memory->Upload();
    static_cast<void>(memory->PageTable(context));
    memory->Release();
    memory.reset();
}

std::string Protect(const HostRange& range) {
    try {
        GuestAllocations::Mutation().Protect(range.Pointer(), RangeBytes, true, true, true, [] {});
    } catch (const std::exception& error) {
        return error.what();
    }
    return {};
}

std::string Remove(const HostRange& range) {
    try {
        GuestAllocations::Mutation().Remove(range.Pointer());
    } catch (const std::exception& error) {
        return error.what();
    }
    GuestAllocations::Mutation().Add(range.Pointer(), RangeBytes, true, true, true);
    return {};
}

void ExpectNamed(const std::string& failure, const HostRange& range, const char* change, const char* holder, const std::string& what) {
    Expect(!failure.empty(), what + ": the " + change + " did not fail although the " + holder + " still holds the range");
    if (failure.empty()) return;
    Expect(failure.find(range.Text()) != std::string::npos, what + ": the " + change + " failed without naming the range " + range.Text() + ": " + failure);
    Expect(failure.find(holder) != std::string::npos, what + ": the " + change + " failed without naming its holder (" + holder + "): " + failure);
}

void HeldByBuild(const Graphics::Context& context) {
    HostRange range;
    auto memory = Acquire(context);
    Expect(memory->HoldsLease(), "held by a build: the build acquired no lease");
    const std::string what = "a range held by the lease of a build that acquired it and has not released it";
    ExpectNamed(Protect(range), range, "protection", "build", what);
    ExpectNamed(Remove(range), range, "unmap", "build", what);
    Finish(context, memory);
    Expect(Protect(range).empty(), what + ": the protection failed after the build released its lease");
}

void ReleasedImport(const Graphics::Context& context) {
    HostRange range;
    const std::string what = "a range held only by its host import, which the pin wait can retire";
    for (const char* change : {"protection", "unmap"}) {
        Expect(Graphics::HostImportFor(context, range.Address(), RangeBytes) != nullptr, what + ": the range was not imported before the " + change);
        Expect(Graphics::HostImportSerial(context, range.Address(), RangeBytes, false) != 0u, what + ": the range has no host import before the " + change);
        const auto failure = std::string(change) == "protection" ? Protect(range) : Remove(range);
        Expect(failure.empty(), what + ": the " + change + " failed: " + failure);
    }
}

void HeldByAddressSpace(const Graphics::Context& context) {
    HostRange range;
    auto first = Acquire(context);
    Finish(context, first);
    const auto hits = Graphics::AddressSpaceCounters().hits;
    auto cached = Acquire(context);
    Expect(Graphics::AddressSpaceCounters().hits == hits + 1 && cached->HoldsLease(), "held by a cached address space: the second build did not take the cached address space");
    const std::string what = "a range held by the cached address space a build took and has not released";
    ExpectNamed(Protect(range), range, "protection", "address space", what);
    ExpectNamed(Remove(range), range, "unmap", "address space", what);
    Finish(context, cached);
    Expect(Protect(range).empty(), what + ": the protection failed after the build released the address space");
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
            std::puts("pin wait holders: host imports or buffer device addresses unavailable");
            return VulkanTestSkipped;
        }
        HeldByBuild(context);
        ReleasedImport(context);
        HeldByAddressSpace(context);
    } catch (const std::exception& error) {
        Expect(false, std::string("exception: ") + error.what());
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d pin wait holder checks failed\n", failures);
        return 1;
    }
    std::puts("pin wait holder tests passed");
    return 0;
#else
    return VulkanTestSkipped;
#endif
}
