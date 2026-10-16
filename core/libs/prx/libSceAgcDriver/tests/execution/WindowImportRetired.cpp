#include "NewPathFixture.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <exception>
#include <set>
#include <string>
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
constexpr std::uint64_t WiderWindowBytes = 2u * WindowBytes;
constexpr std::uint32_t WindowRanges = static_cast<std::uint32_t>(WindowBytes / RangeBytes);
constexpr std::uint32_t Threads = WindowRanges;
constexpr std::uint32_t HitsToAge = 300;
constexpr std::uint64_t LargeBudget = std::uint64_t{1} << 40u;

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
        Require(base != nullptr, "window import retired: cannot allocate host memory");
        auto* words = reinterpret_cast<std::uint32_t*>(base);
        for (std::uint64_t word = 0; word < bytes / 4u; ++word) words[word] = Pattern(word);
    }

    ~HostMemory() {
        for (const auto address : ranges) {
            try {
                GuestAllocations::Mutation().Remove(reinterpret_cast<void*>(address));
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: window import retired: a range could not be removed: %s\n", error.what());
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

void WindowImportRetired(AgcDriver::VulkanDevice& device, const Graphics::Context& context) {
    HostMemory memory(5u * WindowBytes);
    HostMemory unrelated(2u * RangeBytes);
    const auto window = (memory.Address() + WiderWindowBytes - 1u) & ~(WiderWindowBytes - 1u);
    Require(window + WindowBytes <= memory.Address(5u * WindowBytes), "window import retired: the host memory does not hold the window");

    Graphics::SetHostImportWindow(WiderWindowBytes);
    for (std::uint32_t range = 0; range < WindowRanges; ++range) memory.Map(window + range * RangeBytes, RangeBytes);
    const auto first = Build(context);
    Require(first.empty(), "window import retired: the build importing the ranges one by one failed: " + first);
    const auto small = Serials(context, window);
    std::set<std::uint64_t> distinct(small.begin(), small.end());
    Require(!distinct.contains(0u) && distinct.size() == WindowRanges, "window import retired: the " + std::to_string(WindowRanges) + " ranges were not imported one by one (" + std::to_string(distinct.size()) + " distinct import serials)");

    Graphics::SetHostImportWindow(WindowBytes);
    for (std::uint32_t build = 0; build < 2u; ++build) {
        const auto cached = Build(context);
        Expect(cached.empty(), "window import retired: a build over the fully mapped window and its small imports failed: " + cached);
    }

    unrelated.Map(unrelated.Address(), RangeBytes);
    Require(Graphics::HostImportFor(context, unrelated.Address(), RangeBytes) != nullptr, "window import retired: an unrelated range was not imported");
    for (std::uint32_t hit = 0; hit < HitsToAge; ++hit) static_cast<void>(Graphics::HostImportFor(context, unrelated.Address(), RangeBytes));
    unrelated.Map(unrelated.Address(RangeBytes), RangeBytes);
    Graphics::SetHostImportBudget(Graphics::HostImportLiveBytes());
    const auto* evicting = Graphics::HostImportFor(context, unrelated.Address(RangeBytes), RangeBytes);
    Graphics::SetHostImportBudget(LargeBudget);
    Require(evicting != nullptr, "window import retired: the second unrelated range was not imported under the budget");
    const auto after = Serials(context, window);
    const auto retired = static_cast<std::size_t>(std::count(after.begin(), after.end(), 0u));
    Require(retired != 0u && retired < WindowRanges, "window import retired: the budget retired " + std::to_string(retired) + " of the window's small imports, expected some but not all");

    const std::string what = "a rebuild over a fully mapped 8 MiB window of " + std::to_string(WindowRanges) + " ranges of 16 KiB imported one by one, after " + std::to_string(retired) + " of their imports were retired";
    const auto rebuilt = Build(context);
    Expect(rebuilt.empty(), what + ": the build failed: " + rebuilt);
    if (!rebuilt.empty()) return;
    const auto whole = Serial(context, window, WindowBytes);
    std::size_t unserved = 0;
    std::size_t mixed = 0;
    for (std::uint32_t range = 0; range < WindowRanges; ++range) {
        const auto serial = Serial(context, window + range * RangeBytes);
        if (serial == 0u) ++unserved;
        else if (whole != 0u && serial != whole) ++mixed;
    }
    Expect(unserved == 0u, what + ": " + std::to_string(unserved) + " of the window's ranges have no host import");
    Expect(mixed == 0u, what + ": " + std::to_string(mixed) + " ranges keep small imports beside the window's import");
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
            std::puts("window import retired: host imports or buffer device addresses unavailable");
            return VulkanTestSkipped;
        }
        WindowImportRetired(*device, context);
    } catch (const std::exception& error) {
        Expect(false, std::string("exception: ") + error.what());
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d window import retired checks failed\n", failures);
        return 1;
    }
    std::puts("window import retired tests passed");
    return 0;
#else
    return VulkanTestSkipped;
#endif
}
