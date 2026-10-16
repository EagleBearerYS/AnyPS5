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
constexpr std::uint32_t HeldRange = 509;
constexpr std::uint64_t WarmStamps = 1000;

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
        Require(base != nullptr, "window import held: cannot allocate host memory");
        auto* words = reinterpret_cast<std::uint32_t*>(base);
        for (std::uint64_t word = 0; word < bytes / 4u; ++word) words[word] = Pattern(word);
    }

    ~HostMemory() {
        for (const auto address : ranges) {
            try {
                GuestAllocations::Mutation().Remove(reinterpret_cast<void*>(address));
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: window import held: a range could not be removed: %s\n", error.what());
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

void WindowImportHeld(AgcDriver::VulkanDevice& device, const Graphics::Context& context) {
    HostMemory memory(3u * WindowBytes);
    const auto window = (memory.Address() + WindowBytes - 1u) & ~(WindowBytes - 1u);
    Require(window + WindowBytes <= memory.Address(3u * WindowBytes), "window import held: the host memory does not hold the window");
    const auto last = window + (WindowRanges - 1u) * RangeBytes;
    const auto held = window + HeldRange * RangeBytes;

    Graphics::SetHostImportWindow(WindowBytes);
    for (std::uint32_t range = 0; range + 1u < WindowRanges; ++range) memory.Map(window + range * RangeBytes, RangeBytes);
    const auto first = Build(context);
    Require(first.empty(), "window import held: the build of the incomplete window failed: " + first);
    auto small = Serials(context, window);
    small.pop_back();
    const std::set<std::uint64_t> distinct(small.begin(), small.end());
    Require(!distinct.contains(0u) && distinct.size() == WindowRanges - 1u, "window import held: the " + std::to_string(WindowRanges - 1u) + " ranges of the incomplete window were not imported one by one (" + std::to_string(distinct.size()) + " distinct import serials)");
    const auto heldSerial = Serial(context, held);

    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> stamps{0};
    std::thread stamper([&] {
        while (!stop.load()) {
            if (Graphics::HostImportFor(context, held, RangeBytes) != nullptr) stamps.fetch_add(1);
        }
    });
    while (stamps.load() < WarmStamps) std::this_thread::yield();
    memory.Map(last, RangeBytes);
    const auto stampsBefore = stamps.load();
    const auto rebuilt = Build(context);
    const auto stampsDuring = stamps.load() - stampsBefore;
    stop.store(true);
    stamper.join();

    const std::string what = "a rebuild of an 8 MiB window of " + std::to_string(WindowRanges) + " ranges of 16 KiB imported one by one, which its last range completes, while another thread keeps using the import of range " + std::to_string(HeldRange) + " (" + std::to_string(stampsDuring) + " uses during the build)";
    Expect(rebuilt.empty(), what + ": the build failed: " + rebuilt);
    if (!rebuilt.empty()) return;
    std::size_t unserved = 0;
    for (std::uint32_t range = 0; range < WindowRanges; ++range) {
        if (Serial(context, window + range * RangeBytes) == 0u) ++unserved;
    }
    Expect(unserved == 0u, what + ": " + std::to_string(unserved) + " of the window's ranges have no host import");
    const auto heldAfter = Serial(context, held);
    const auto whole = Serial(context, window, WindowBytes);
    Expect(heldAfter == heldSerial || (whole != 0u && heldAfter == whole), what + ": the import of the range in use changed from " + Hex(heldSerial) + " to " + Hex(heldAfter) + " without the window's import serving it");
    ExpectDispatch(device, memory, window, "a dispatch loading and storing through every range of the window after " + what);
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
            std::puts("window import held: host imports or buffer device addresses unavailable");
            return VulkanTestSkipped;
        }
        WindowImportHeld(*device, context);
    } catch (const std::exception& error) {
        Expect(false, std::string("exception: ") + error.what());
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d window import held checks failed\n", failures);
        return 1;
    }
    std::puts("window import held tests passed");
    return 0;
#else
    return VulkanTestSkipped;
#endif
}
