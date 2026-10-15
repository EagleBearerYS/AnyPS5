#include "NewPathFixture.hpp"
#include "prx/libSceAgcDriver/Graphics/include/CommittedImports.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestBufferMemory.hpp"
#include "VulkanTestDevice.hpp"
#include <exception>
#include <new>
#include <stdexcept>

#ifdef _WIN32

namespace {

using namespace NewPath;

constexpr std::uint32_t Lanes = 16;
constexpr std::uint32_t SliceWords = Lanes;
constexpr std::uint32_t Untouched = 0xdeadbeefu;
constexpr std::uint32_t Sources = 64;
constexpr std::uint32_t EvictionRounds = 6;
constexpr std::uint32_t ImportBlockBytes = static_cast<std::uint32_t>(PageBytes);
constexpr std::uint32_t BudgetImports = 6;

int failures = 0;

void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

const Program& SumProgram() {
    static const Program program = [] {
        using namespace Isa;
        Program built;
        built << Vop2(VLshlRevB32, 1, InlineInteger(2), 0)
              << Mubuf(BufferLoadDword, 2, 1, 0)
              << Mubuf(BufferLoadDword, 3, 1, 4)
              << WaitAll
              << Vop2(VAddNcU32, 4, Vgpr(2), 3)
              << Mubuf(BufferStoreDword, 4, 1, 8)
              << EndProgram;
        return built;
    }();
    return program;
}

std::array<std::uint32_t, 12> Resources(const std::array<std::pair<std::uint64_t, std::uint32_t>, 3>& buffers) {
    std::array<std::uint32_t, 12> userData{};
    for (std::size_t index = 0; index < buffers.size(); ++index) {
        const auto buffer = BufferResource(buffers[index].first, buffers[index].second);
        std::copy(buffer.begin(), buffer.end(), userData.begin() + 4 * index);
    }
    return userData;
}

std::string Submitted(Commands& commands) {
    std::string failure;
    StderrCapture capture;
    try {
        commands.Submit(0);
        WaitIdle();
    } catch (const std::exception& error) {
        failure = error.what();
    }
    const auto log = capture.Finish();
    if (log.find("BDA access failed") != std::string::npos) failure += "\n" + log;
    return failure;
}

std::uint32_t* HostBlock() {
    auto* block = static_cast<std::uint32_t*>(::operator new(ImportBlockBytes, std::align_val_t{ImportBlockBytes}));
    GuestAllocations::Mutation().Add(block, ImportBlockBytes, true, true, true);
    return block;
}

std::uint32_t SharedWord(std::uint32_t lane) {
    return 0x30000000u + lane * 0x101u;
}

std::uint32_t SourceWord(std::uint32_t source, std::uint32_t lane) {
    return (source << 16u) | (lane << 4u) | 1u;
}

void ImportEviction() {
    const auto address = [](const void* pointer) { return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(pointer)); };
    auto* warmup = HostBlock();
    std::fill(warmup, warmup + ImportBlockBytes / 4u, 0u);
    Commands warm;
    warm.Dispatch(SumProgram(), Resources({{{address(warmup), SliceWords * 4u}, {address(warmup + SliceWords), SliceWords * 4u}, {address(warmup + 2u * SliceWords), SliceWords * 4u}}}), Lanes);
    auto failure = Submitted(warm);
    auto* shared = HostBlock();
    auto* output = HostBlock();
    std::array<std::uint32_t*, Sources> sources{};
    for (auto& source : sources) source = HostBlock();
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) shared[lane] = SharedWord(lane);
    for (std::uint32_t source = 0; source < Sources; ++source) {
        for (std::uint32_t lane = 0; lane < Lanes; ++lane) sources[source][lane] = SourceWord(source, lane);
    }
    std::fill(output, output + ImportBlockBytes / 4u, Untouched);
    const auto budget = AgcDriver::Graphics::HostImportLiveBytes() + AgcDriver::Graphics::CommittedImportPinnedBytes() + std::uint64_t{BudgetImports} * ImportBlockBytes;
    AgcDriver::Graphics::SetHostImportBudget(budget);
    for (std::uint32_t round = 0; round < EvictionRounds && failure.empty(); ++round) {
        Commands commands;
        for (std::uint32_t source = 0; source < Sources; ++source) {
            const auto slot = round * Sources + source;
            commands.Dispatch(SumProgram(), Resources({{{address(shared), SliceWords * 4u}, {address(sources[source]), SliceWords * 4u}, {address(output + slot * SliceWords), SliceWords * 4u}}}), Lanes);
        }
        failure = Submitted(commands);
    }
    const auto live = AgcDriver::Graphics::HostImportLiveBytes() + AgcDriver::Graphics::CommittedImportPinnedBytes();
    AgcDriver::Graphics::SetHostImportBudget(0);
    Expect(failure.empty(), "import eviction: a submission reported a failure: " + failure);
    Expect(live <= budget, "import eviction: the live imports (" + std::to_string(live) + " bytes) ended over the budget of " + std::to_string(budget));
    AgcDriver::GuestMemory::FlushGpuWrites(address(output), ImportBlockBytes);
    std::size_t wrong = 0;
    std::string first;
    for (std::uint32_t slot = 0; slot < EvictionRounds * Sources; ++slot) {
        for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
            const auto expected = SharedWord(lane) + SourceWord(slot % Sources, lane);
            const auto actual = output[slot * SliceWords + lane];
            if (actual == expected) continue;
            if (wrong++ == 0u) first = "dispatch " + std::to_string(slot) + " lane " + std::to_string(lane) + " is " + Hex(actual) + ", expected " + Hex(expected);
        }
    }
    Expect(wrong == 0u, "import eviction: " + std::to_string(EvictionRounds * Sources) + " dispatches over " + std::to_string(Sources) + " sources under a budget of " + std::to_string(BudgetImports) + " more imports, with one table and one output every dispatch uses: " + std::to_string(wrong) + " words differ, first " + first);
}

}

#endif

int main() {
#ifdef _WIN32
    {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
    }
    try {
        ImportEviction();
    } catch (const std::exception& error) {
        Expect(false, std::string("import eviction: exception: ") + error.what());
    }
    try {
        AgcDriverShutdown_nid_postfix();
    } catch (const std::exception& error) {
        Expect(false, std::string("shutdown: ") + error.what());
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d BDA import eviction checks failed\n", failures);
        return 1;
    }
    std::printf("BDA import eviction tests passed\n");
    return 0;
#else
    std::printf("skipped, commit imports are Windows-only\n");
    return VulkanTestSkipped;
#endif
}
