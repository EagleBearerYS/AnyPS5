#include "NewPathFixture.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <atomic>
#include <exception>
#include <set>
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
using namespace NewPath;
using ShaderRecompiler::ShaderStage;

constexpr std::uint64_t RangeBytes = 16384;
constexpr std::uint64_t WindowBytes = std::uint64_t{8} << 20u;
constexpr std::uint32_t WindowRanges = static_cast<std::uint32_t>(WindowBytes / RangeBytes);
constexpr std::uint32_t Threads = WindowRanges;
constexpr std::uint32_t HeldRange = 100;
constexpr std::uint32_t ChangedRange = 300;
constexpr std::uint64_t WarmStamps = 1000;
constexpr std::uint64_t WarmChurns = 20;
constexpr std::size_t ChurnBytes = 0x4000;
constexpr std::uint32_t LeadRanges = 16;
constexpr std::uint64_t LeadStride = 4u * RangeBytes;

int failures = 0;

void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

std::uint32_t Pattern(std::uint64_t word) {
    return 0x70000000u | static_cast<std::uint32_t>(word & 0x0fffffffu);
}

class HostMemory {
public:
    explicit HostMemory(std::uint64_t size) : bytes(size) {
        base = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        Require(base != nullptr, "window import held stale: cannot allocate host memory");
        auto* words = reinterpret_cast<std::uint32_t*>(base);
        for (std::uint64_t word = 0; word < bytes / 4u; ++word) words[word] = Pattern(word);
    }

    ~HostMemory() {
        for (const auto address : ranges) {
            try {
                GuestAllocations::Mutation().Remove(reinterpret_cast<void*>(address));
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: window import held stale: a range could not be removed: %s\n", error.what());
            }
        }
        VirtualFree(base, 0, MEM_RELEASE);
    }

    HostMemory(const HostMemory&) = delete;
    HostMemory& operator=(const HostMemory&) = delete;

    void Map(std::uint64_t address, std::uint64_t size) {
        GuestAllocations::Mutation().Add(reinterpret_cast<void*>(address), size, true, true, true);
        ranges.push_back(address);
    }

    std::uint64_t Address(std::uint64_t offset = 0) const { return reinterpret_cast<std::uint64_t>(base) + offset; }
    std::uint32_t Word(std::uint64_t address) const { return *reinterpret_cast<const volatile std::uint32_t*>(static_cast<std::uintptr_t>(address)); }

private:
    std::uint8_t* base = nullptr;
    std::uint64_t bytes = 0;
    std::vector<std::uint64_t> ranges;
};

std::string Build(const Graphics::Context& context) {
    std::string failure;
    StderrCapture capture;
    try {
        std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
        Graphics::GuestBufferMemory memory(context);
        memory.AcquireRegistered();
        memory.Upload();
        static_cast<void>(memory.PageTable(context));
        memory.Release();
    } catch (const std::exception& error) {
        failure = error.what();
    }
    static_cast<void>(capture.Finish());
    return failure;
}

std::uint64_t Serial(const Graphics::Context& context, std::uint64_t address, std::uint64_t bytes = RangeBytes) {
    return Graphics::HostImportSerial(context, address, static_cast<std::size_t>(bytes), false);
}

std::vector<std::uint64_t> Serials(const Graphics::Context& context, std::uint64_t window) {
    std::vector<std::uint64_t> serials(WindowRanges);
    for (std::uint32_t range = 0; range < WindowRanges; ++range) serials[range] = Serial(context, window + range * RangeBytes);
    return serials;
}

const Program& UpdateProgram() {
    static const Program program = [] {
        using namespace Isa;
        Program built;
        built << Vop2(VLshlRevB32, 1, InlineInteger(2), 0)
              << Global(GlobalLoadDword, 2, 1, 0, 2)
              << WaitAll
              << Global(GlobalLoadDword, 3, 2, 0, 0)
              << WaitAll
              << Vop2(VAddNcU32, 4, InlineInteger(1), 3)
              << Global(GlobalStoreDword, 0, 2, 4, 0, 4)
              << Global(GlobalStoreDword, 0, 1, 3, 4)
              << EndProgram;
        return built;
    }();
    return program;
}

void ExpectDispatch(AgcDriver::VulkanDevice& device, const HostMemory& memory, std::uint64_t window, const std::string& what) {
    PrivatePages scratch(1);
    std::vector<std::uint64_t> words(Threads);
    for (std::uint32_t lane = 0; lane < Threads; ++lane) {
        words[lane] = window + lane * RangeBytes + 0x100u + (lane % 64u) * 8u;
        scratch.Words()[lane] = static_cast<std::uint32_t>(words[lane] - memory.Address());
        scratch.Words()[4096u + lane] = 0xdeadbeefu;
    }
    const std::array<std::uint32_t, 6> userData{Low(memory.Address()), High(memory.Address()), Low(scratch.Address()), High(scratch.Address()), Low(scratch.Address(16384)), High(scratch.Address(16384))};
    const auto& program = UpdateProgram();
    const auto code = program.Code();
    const std::array<ShaderRecompiler::MemoryRegion, 1> regions{{{static_cast<std::uintptr_t>(program.Address()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{Threads, 1, 1}, 0, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, program.Address(), code, 0, {}},
        {32, 0, userData, compute, std::nullopt, std::nullopt, regions},
        device.Target(),
        {0, 0, 0, 128}
    };
    request.useCache = false;
    std::string failure;
    {
        StderrCapture capture;
        try {
            const auto result = ShaderRecompiler::Recompile(request);
            std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
            device.Dispatch(result, 1, 1, 1, static_cast<std::uintptr_t>(program.Address()));
            device.WaitIdle();
        } catch (const std::exception& error) {
            failure = error.what();
        }
        const auto log = capture.Finish();
        if (log.find("failed") != std::string::npos) failure += "\n" + log;
    }
    Expect(failure.empty(), what + ": the dispatch failed: " + failure);
    AgcDriver::GuestMemory::FlushGpuWrites(scratch.Address(), scratch.Bytes());
    std::size_t wrong = 0;
    std::string first;
    for (std::uint32_t lane = 0; lane < Threads; ++lane) {
        const auto expected = Pattern((words[lane] - memory.Address()) / 4u);
        const auto loaded = scratch.Words()[4096u + lane];
        const auto stored = memory.Word(words[lane] + 4u);
        if (loaded == expected && stored == expected + 1u) continue;
        if (wrong++ == 0u) first = "the lane in range " + std::to_string(lane) + " loaded " + Hex(loaded) + " and stored " + Hex(stored) + ", expected " + Hex(expected) + " and " + Hex(expected + 1u);
    }
    Expect(wrong == 0u, what + ": " + std::to_string(wrong) + " of the window's " + std::to_string(Threads) + " ranges did not load their word and store it plus one, first " + first);
}

void WindowImportHeldStale(AgcDriver::VulkanDevice& device, const Graphics::Context& context) {
    HostMemory memory(4u * WindowBytes);
    auto* churn = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, ChurnBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Require(churn != nullptr, "window import held stale: cannot allocate the churned range");
    const auto window = ((memory.Address() + WindowBytes - 1u) & ~(WindowBytes - 1u)) + WindowBytes;
    Require(window + WindowBytes <= memory.Address(4u * WindowBytes), "window import held stale: the host memory does not hold the window");
    const auto lead = window - WindowBytes;
    const auto held = window + HeldRange * RangeBytes;
    const auto changed = window + ChangedRange * RangeBytes;

    Graphics::SetHostImportWindow(WindowBytes);
    for (std::uint32_t range = 0; range < WindowRanges; ++range) memory.Map(window + range * RangeBytes, RangeBytes);
    const auto first = Build(context);
    Require(first.empty(), "window import held stale: the build of the full window failed: " + first);
    const auto whole = Serial(context, window, WindowBytes);
    const auto before = Serials(context, window);
    Require(whole != 0u && std::all_of(before.begin(), before.end(), [&](std::uint64_t serial) { return serial == whole; }), "window import held stale: the full window was not imported as one window import");

    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> stamps{0};
    std::atomic<std::uint64_t> churns{0};
    std::atomic<bool> churnFailed{false};
    std::string churnFailure;
    std::thread stamper([&] {
        while (!stop.load()) {
            if (Graphics::HostImportCovers(context, held, RangeBytes)) stamps.fetch_add(1);
        }
    });
    std::thread churner([&] {
        try {
            while (!stop.load()) {
                GuestAllocations::Mutation().Add(churn, ChurnBytes, true, true, true);
                GuestAllocations::Mutation().Remove(churn);
                churns.fetch_add(1);
            }
        } catch (const std::exception& error) {
            churnFailure = error.what();
            churnFailed.store(true);
        }
    });
    while ((stamps.load() < WarmStamps || churns.load() < WarmChurns) && !churnFailed.load()) std::this_thread::yield();
    std::string protection;
    try {
        GuestAllocations::Mutation().Protect(reinterpret_cast<void*>(changed), RangeBytes, true, true, true, [] {});
    } catch (const std::exception& error) {
        protection = error.what();
    }
    const bool found = Graphics::HostImportFor(context, held, RangeBytes) != nullptr;
    const auto renewed = Serial(context, window, WindowBytes);
    for (std::uint32_t range = 0; range < LeadRanges; ++range) memory.Map(lead + range * LeadStride, RangeBytes);
    const auto stampsBefore = stamps.load();
    const auto churnsBefore = churns.load();
    const auto rebuilt = Build(context);
    const auto stampsDuring = stamps.load() - stampsBefore;
    const auto churnsDuring = churns.load() - churnsBefore;
    stop.store(true);
    stamper.join();
    churner.join();
    VirtualFree(churn, 0, MEM_RELEASE);

    const std::string what = "a rebuild of an 8 MiB window imported whole, after range " + std::to_string(ChangedRange) + " got a new registry entry and a reconciling lookup of range " + std::to_string(HeldRange) + " imported the window again, with " + std::to_string(LeadRanges) + " new ranges below the window imported first, while another thread keeps finding the window's import for range " + std::to_string(HeldRange) + " (" + std::to_string(stampsDuring) + " finds during the build, none of them reconciling the imports) and a third maps and removes a range outside the window (" + std::to_string(churnsDuring) + " times during the build)";
    Expect(churnFailure.empty(), what + ": the mapping changes outside the window failed: " + churnFailure);
    Expect(protection.empty(), what + ": the protection that gives range " + std::to_string(ChangedRange) + " a new registry entry failed: " + protection);
    Expect(found, what + ": range " + std::to_string(HeldRange) + " had no host import after the protection");
    Expect(rebuilt.empty(), what + ": the build failed: " + rebuilt);
    if (!rebuilt.empty() || !protection.empty()) return;
    std::size_t unserved = 0;
    std::size_t moved = 0;
    for (std::uint32_t range = 0; range < WindowRanges; ++range) {
        const auto serial = Serial(context, window + range * RangeBytes);
        if (serial == 0u) ++unserved;
        else if (range != ChangedRange && renewed != 0u && serial != renewed) ++moved;
    }
    Expect(unserved == 0u, what + ": " + std::to_string(unserved) + " of the window's ranges have no host import");
    Expect(moved == 0u, what + ": " + std::to_string(moved) + " unchanged ranges are not served by the window's import that the other thread keeps finding");
    ExpectDispatch(device, memory, window, "a dispatch loading and storing through every range of the window after " + what);
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
        if (context.hostImportAlignment == 0 || !context.bufferDeviceAddress) {
            std::puts("window import held stale: host imports or buffer device addresses unavailable");
            return VulkanTestSkipped;
        }
        WindowImportHeldStale(*device, context);
    } catch (const std::exception& error) {
        Expect(false, std::string("exception: ") + error.what());
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d window import held stale checks failed\n", failures);
        return 1;
    }
    std::puts("window import held stale tests passed");
    return 0;
#else
    return VulkanTestSkipped;
#endif
}
