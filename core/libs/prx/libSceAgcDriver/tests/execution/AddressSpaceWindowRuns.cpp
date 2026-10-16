#include "NewPathFixture.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/BdaPageTable.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
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
using Clock = std::chrono::steady_clock;
using ShaderRecompiler::ShaderStage;

constexpr std::uint64_t RangeBytes = 16384;
constexpr std::uint64_t WindowBytes = std::uint64_t{64} << 20u;
constexpr std::uint32_t WindowRanges = static_cast<std::uint32_t>(WindowBytes / RangeBytes);
constexpr std::uint32_t FullWindows = 2;
constexpr std::uint32_t Isolated = 3;
constexpr std::uint32_t Lanes = 64;
constexpr auto HoldLease = std::chrono::milliseconds(100);
constexpr auto RebuildLimit = std::chrono::milliseconds(100);

int failures = 0;

void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

std::uint32_t Pattern(std::uint64_t word) {
    return 0x60000000u | static_cast<std::uint32_t>(word & 0x0fffffffu);
}

class HostMemory {
public:
    explicit HostMemory(std::uint64_t size) : bytes(size) {
        base = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        Require(base != nullptr, "address space window runs: cannot allocate host memory");
        auto* words = reinterpret_cast<std::uint32_t*>(base);
        for (std::uint64_t word = 0; word < bytes / 4u; ++word) words[word] = Pattern(word);
    }

    ~HostMemory() {
        for (const auto address : ranges) {
            try {
                GuestAllocations::Mutation().Remove(reinterpret_cast<void*>(address));
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: address space window runs: a range could not be removed: %s\n", error.what());
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

    void Unmap(std::uint64_t address) {
        GuestAllocations::Mutation().Remove(reinterpret_cast<void*>(address));
        ranges.erase(std::remove(ranges.begin(), ranges.end(), address), ranges.end());
    }

    std::uint64_t Address(std::uint64_t offset = 0) const { return reinterpret_cast<std::uint64_t>(base) + offset; }
    std::uint32_t Word(std::uint64_t address) const { return *reinterpret_cast<const volatile std::uint32_t*>(static_cast<std::uintptr_t>(address)); }
    std::uint64_t Bytes() const { return bytes; }

private:
    std::uint8_t* base = nullptr;
    std::uint64_t bytes = 0;
    std::vector<std::uint64_t> ranges;
};

struct Built {
    std::shared_ptr<const Graphics::BdaPageTable> table;
    Clock::duration took{};
};

Built Build(const Graphics::Context& context) {
    Built built;
    std::unique_lock gpu(AgcDriver::GuestMemory::GpuMutex());
    const auto start = Clock::now();
    Graphics::GuestBufferMemory memory(context);
    memory.AcquireRegistered();
    built.took = Clock::now() - start;
    memory.Upload();
    built.table = memory.PageTable(context);
    memory.Release();
    return built;
}

long long Milliseconds(Clock::duration duration) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

struct Layout {
    std::uint64_t first = 0;
    std::uint64_t before = 0;
    std::uint64_t after = 0;
    std::vector<std::uint64_t> isolated;

    std::uint64_t Window(std::uint32_t window) const { return first + window * WindowBytes; }
    std::uint64_t Range(std::uint32_t window, std::uint32_t range) const { return Window(window) + range * RangeBytes; }
};

Layout MapRanges(HostMemory& memory) {
    Layout layout;
    layout.first = (memory.Address() + WindowBytes + WindowBytes - 1u) & ~(WindowBytes - 1u);
    layout.before = layout.first - RangeBytes;
    layout.after = layout.Window(FullWindows);
    Require(layout.after + WindowBytes <= memory.Address() + memory.Bytes(), "address space window runs: the host memory does not hold the windows");
    memory.Map(layout.before, RangeBytes);
    for (std::uint32_t window = 0; window < FullWindows; ++window) {
        for (std::uint32_t range = 0; range < WindowRanges; ++range) memory.Map(layout.Range(window, range), RangeBytes);
    }
    memory.Map(layout.after, RangeBytes);
    for (std::uint32_t index = 0; index < Isolated; ++index) {
        const auto address = layout.after + (index + 2u) * 4u * RangeBytes;
        memory.Map(address, RangeBytes);
        layout.isolated.push_back(address);
    }
    return layout;
}

std::uint64_t Serial(const Graphics::Context& context, std::uint64_t address, std::uint64_t bytes = RangeBytes) {
    return Graphics::HostImportSerial(context, address, static_cast<std::size_t>(bytes), false);
}

struct Serials {
    std::vector<std::uint64_t> windows;
    std::vector<std::uint64_t> outside;
};

Serials ExpectImports(const Graphics::Context& context, const Built& built, const Layout& layout, const std::string& what) {
    Serials serials;
    Require(built.table != nullptr, what + ": the build made no page table");
    for (std::uint32_t window = 0; window < FullWindows; ++window) {
        const auto start = layout.Window(window);
        const auto whole = Serial(context, start, WindowBytes);
        serials.windows.push_back(whole);
        Expect(whole != 0u, what + ": no import covers the whole window at " + Hex(start));
        const auto translated = built.table->Find(start);
        Expect(translated.has_value() && translated->begin == start && translated->end == start + WindowBytes, what + ": the page table does not translate the window at " + Hex(start) + " as one range");
        std::size_t foreign = 0;
        std::size_t moved = 0;
        std::string first;
        for (std::uint32_t range = 0; range < WindowRanges; ++range) {
            const auto address = layout.Range(window, range);
            const auto serial = Serial(context, address);
            const auto found = built.table->Find(address + RangeBytes - 4u);
            if (serial != whole && foreign++ == 0u && first.empty()) first = "range " + std::to_string(range) + " has import serial " + Hex(serial) + ", the window's is " + Hex(whole);
            if (translated.has_value() && (!found.has_value() || found->begin != translated->begin || found->deviceAddress != translated->deviceAddress) && moved++ == 0u && first.empty()) first = "range " + std::to_string(range) + " translates through another device range";
        }
        Expect(foreign == 0u && moved == 0u, what + ": of the " + std::to_string(WindowRanges) + " ranges of the window at " + Hex(start) + ", " + std::to_string(foreign) + " are not served by the window's import and " + std::to_string(moved) + " translate through another device range; first " + first);
    }
    std::vector<std::uint64_t> outside{layout.before, layout.after};
    outside.insert(outside.end(), layout.isolated.begin(), layout.isolated.end());
    for (const auto address : outside) {
        const auto serial = Serial(context, address);
        serials.outside.push_back(serial);
        Expect(serial != 0u && std::find(serials.windows.begin(), serials.windows.end(), serial) == serials.windows.end(), what + ": the range at " + Hex(address) + " outside the full windows has import serial " + Hex(serial) + ", expected an import of its own");
    }
    auto distinct = serials.outside;
    std::sort(distinct.begin(), distinct.end());
    Expect(std::adjacent_find(distinct.begin(), distinct.end()) == distinct.end(), what + ": two separate ranges outside the full windows share an import");
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

void Dispatch(AgcDriver::VulkanDevice& device, std::span<const std::uint32_t> userData) {
    const auto& program = UpdateProgram();
    const auto code = program.Code();
    const std::array<ShaderRecompiler::MemoryRegion, 1> regions{{{static_cast<std::uintptr_t>(program.Address()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{Lanes, 1, 1}, 0, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, program.Address(), code, 0, {}},
        {32, 0, userData, compute, std::nullopt, std::nullopt, regions},
        device.Target(),
        {0, 0, 0, 128}
    };
    request.useCache = false;
    const auto result = ShaderRecompiler::Recompile(request);
    std::lock_guard gpu(AgcDriver::GuestMemory::GpuMutex());
    device.Dispatch(result, 1, 1, 1, static_cast<std::uintptr_t>(program.Address()));
    device.WaitIdle();
}

void ExpectDispatch(AgcDriver::VulkanDevice& device, const HostMemory& memory, const Layout& layout, const std::string& what) {
    std::vector<std::uint64_t> addresses;
    for (std::uint32_t window = 0; window < FullWindows; ++window) {
        for (std::uint32_t pick = 0; pick < 26u; ++pick) addresses.push_back(layout.Range(window, pick == 25u ? WindowRanges - 1u : pick * (WindowRanges / 25u)));
    }
    addresses.push_back(layout.before);
    addresses.push_back(layout.after);
    for (const auto address : layout.isolated) addresses.push_back(address);
    while (addresses.size() < Lanes) addresses.push_back(layout.Range(static_cast<std::uint32_t>(addresses.size() % FullWindows), static_cast<std::uint32_t>(addresses.size() * 7u % WindowRanges)));
    PrivatePages scratch(1);
    std::vector<std::uint64_t> words(Lanes);
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
        words[lane] = addresses[lane] + 0x100u + lane * 8u;
        scratch.Words()[lane] = static_cast<std::uint32_t>(words[lane] - memory.Address());
        scratch.Words()[1024u + lane] = 0xdeadbeefu;
    }
    const std::array<std::uint32_t, 6> userData{Low(memory.Address()), High(memory.Address()), Low(scratch.Address()), High(scratch.Address()), Low(scratch.Address(4096)), High(scratch.Address(4096))};
    std::string failure;
    {
        StderrCapture capture;
        try {
            Dispatch(device, userData);
        } catch (const std::exception& error) {
            failure = error.what();
        }
        const auto log = capture.Finish();
        if (log.find("BDA access failed") != std::string::npos || log.find("failed") != std::string::npos) failure += "\n" + log;
    }
    Expect(failure.empty(), what + ": the dispatch failed: " + failure);
    AgcDriver::GuestMemory::FlushGpuWrites(scratch.Address(), scratch.Bytes());
    std::size_t wrong = 0;
    std::string first;
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
        const auto expected = Pattern((words[lane] - memory.Address()) / 4u);
        const auto loaded = scratch.Words()[1024u + lane];
        const auto stored = memory.Word(words[lane] + 4u);
        if (loaded == expected && stored == expected + 1u) continue;
        if (wrong++ == 0u) first = "lane " + std::to_string(lane) + " at " + Hex(words[lane]) + " loaded " + Hex(loaded) + " and stored " + Hex(stored) + ", expected " + Hex(expected) + " and " + Hex(expected + 1u);
    }
    Expect(wrong == 0u, what + ": " + std::to_string(wrong) + " of " + std::to_string(Lanes) + " lanes did not load their word and store it plus one through the imports, first " + first);
}

void DropCachedSpace(const Graphics::Context& context, HostMemory& unrelated) {
    std::atomic<bool> held{false};
    std::string buildFailure;
    std::thread builder([&] {
        try {
            Graphics::GuestBufferMemory memory(context);
            memory.AcquireRegistered();
            held.store(true);
            std::this_thread::sleep_for(HoldLease);
        } catch (const std::exception& error) {
            buildFailure = error.what();
            held.store(true);
        }
    });
    while (!held.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::string removal;
    try {
        unrelated.Unmap(unrelated.Address());
    } catch (const std::exception& error) {
        removal = error.what();
    }
    builder.join();
    Expect(buildFailure.empty(), "address space window runs: the build holding the lease failed: " + buildFailure);
    Expect(removal.empty(), "address space window runs: the removal of a leased range failed: " + removal);
}

void WindowRuns(AgcDriver::VulkanDevice& device, const Graphics::Context& context) {
    Graphics::SetHostImportWindow(WindowBytes);
    HostMemory memory((FullWindows + 3u) * WindowBytes);
    HostMemory unrelated(PageBytes);
    unrelated.Map(unrelated.Address(), PageBytes);
    const auto layout = MapRanges(memory);
    const std::string mapped = std::to_string(FullWindows) + " fully mapped " + std::to_string(WindowBytes >> 20u) + " MiB import windows of " + std::to_string(WindowRanges) + " ranges of 16 KiB each";
    const auto first = Build(context);
    const auto before = ExpectImports(context, first, layout, "the first build of " + mapped);
    ExpectDispatch(device, memory, layout, "a dispatch after the first build of " + mapped);

    const auto counters = Graphics::AddressSpaceCounters();
    DropCachedSpace(context, unrelated);
    const auto rebuilt = Build(context);
    const auto after = Graphics::AddressSpaceCounters();
    const std::string what = "a rebuild of " + mapped + " after a pin wait dropped the cached address space";
    Expect(after.rebuiltWaiterDrop > counters.rebuiltWaiterDrop, what + ": the build did not rebuild the space after the waiter's drop, so it may have reused the earlier regions");
    const auto again = ExpectImports(context, rebuilt, layout, what);
    Expect(again.windows == before.windows, what + ": a window's import changed although its ranges did not");
    Expect(again.outside == before.outside, what + ": a range outside the full windows lost its import");
    std::printf("address space window runs: the first build took %lld ms, the rebuild without reuse %lld ms\n", Milliseconds(first.took), Milliseconds(rebuilt.took));
    Expect(rebuilt.took < RebuildLimit, what + ": the rebuild took " + std::to_string(Milliseconds(rebuilt.took)) + " ms, expected under " + std::to_string(RebuildLimit.count()) + " ms");
    ExpectDispatch(device, memory, layout, "a dispatch after " + what);
}

}

#endif

int main() {
#ifdef _WIN32
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        const auto context = AgcDriver::VulkanDeviceTestAccess::ContextOf(*device);
        if (context.hostImportAlignment == 0 || !context.bufferDeviceAddress) {
            std::puts("address space window runs: host imports or buffer device addresses unavailable");
            return VulkanTestSkipped;
        }
        WindowRuns(*device, context);
    } catch (const std::exception& error) {
        Expect(false, std::string("exception: ") + error.what());
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d address space window run checks failed\n", failures);
        return 1;
    }
    std::puts("address space window run tests passed");
    return 0;
#else
    return VulkanTestSkipped;
#endif
}
