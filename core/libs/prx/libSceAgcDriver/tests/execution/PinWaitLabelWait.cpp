#include "NewPathFixture.hpp"
#include "VulkanTestDevice.hpp"
#include <chrono>
#include <exception>
#include <string>
#include <thread>

#ifdef _WIN32

namespace {

using namespace NewPath;
using Clock = std::chrono::steady_clock;

constexpr std::uint32_t QueueA = 0x20;
constexpr std::uint32_t QueueB = 0x28;
constexpr std::uint32_t Lanes = 64;
constexpr std::uint32_t RangePages = 4;
constexpr std::uint32_t Equal = 3;
constexpr std::uint32_t Released = 1;
constexpr std::uint32_t After = 0x6d000001u;
constexpr std::uint32_t Pattern = 0x4e000000u;
constexpr const char* PinWaitMs = "3000";
constexpr auto Settled = std::chrono::milliseconds(300);
constexpr auto Prompt = std::chrono::milliseconds(2500);

int failures = 0;

void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

const Program& ReadProgram() {
    static const Program program = [] {
        using namespace Isa;
        Program built;
        built << Vop2(VLshlRevB32, 1, InlineInteger(12), 0)
              << Mubuf(BufferLoadDword, 2, 1, 0)
              << WaitAll
              << Vop2(VLshlRevB32, 3, InlineInteger(2), 0)
              << Mubuf(BufferStoreDword, 2, 3, 4)
              << EndProgram;
        return built;
    }();
    return program;
}

class Region {
public:
    Region() : bytes(std::uint64_t{RangePages} * PageBytes) {
        base = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(bytes, PageBytes));
        Require(base != nullptr, "pin wait label wait: cannot reserve the range");
        GuestArena::GuestArenaCommit_nid_postfix(base, bytes, PAGE_READWRITE, 16384);
        auto* words = reinterpret_cast<volatile std::uint32_t*>(base);
        for (std::uint64_t word = 0; word < bytes / 4u; ++word) words[word] = Pattern | static_cast<std::uint32_t>(word);
        GuestAllocations::Mutation().Add(base, bytes, true, true, true);
        registered = true;
    }

    ~Region() {
        try {
            if (registered) GuestAllocations::Mutation().Remove(base);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "FAIL: pin wait label wait: the range could not be removed at the end: %s\n", error.what());
            ++failures;
        }
        GuestArena::GuestArenaReset_nid_postfix(base, bytes);
        GuestArena::GuestArenaRelease_nid_postfix(base, bytes);
    }

    Region(const Region&) = delete;
    Region& operator=(const Region&) = delete;

    std::string Change(bool protect) {
        try {
            if (protect) {
                GuestAllocations::Mutation().Protect(base, bytes, true, true, true, [] {});
            } else {
                GuestAllocations::Mutation().Remove(base);
                registered = false;
            }
        } catch (const std::exception& error) {
            return error.what();
        }
        return {};
    }

    std::uint64_t Address(std::uint64_t offset = 0) const { return reinterpret_cast<std::uint64_t>(base) + offset; }
    std::uint64_t Bytes() const { return bytes; }

private:
    std::uint8_t* base = nullptr;
    std::uint64_t bytes = 0;
    bool registered = false;
};

struct Variant {
    const char* name;
    bool warm;
    bool otherQueue;
    bool protect;
    bool otherWaits;
};

void RecordRead(Commands& commands, const Region& region, const PrivatePages& output) {
    const auto source = BufferResource(region.Address(), static_cast<std::uint32_t>(region.Bytes()));
    const auto target = BufferResource(output.Address(), Lanes * 4u);
    const std::array<std::uint32_t, 8> userData{source[0], source[1], source[2], source[3], target[0], target[1], target[2], target[3]};
    commands.Dispatch(ReadProgram(), userData, Lanes);
}

void Run(const Variant& variant) {
    Region region;
    PrivatePages output(1);
    PrivatePages labels(1);
    PrivatePages other(1);
    for (std::uint32_t word = 0; word < 16u; ++word) labels.Words()[word] = 0u;
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) output.Words()[lane] = 0xdeadbeefu;
    const std::string what = std::string(variant.name) + ": a dispatch on one queue reads a range through the page table and the queue then waits for a label the CPU writes only after it " + (variant.protect ? "protects" : "unmaps") + " the range";
    if (variant.warm) {
        Commands warm;
        RecordRead(warm, region, other);
        warm.Submit(QueueA);
        WaitIdle();
        Settle();
    }
    Region elsewhere;
    if (variant.otherQueue) {
        Commands open;
        RecordRead(open, elsewhere, other);
        if (variant.otherWaits) {
            open.Wait(labels.Address(), Equal, Released, 0xffffffffu);
            open.WriteData(labels.Address(128), {After});
        }
        open.Submit(QueueB);
        std::this_thread::sleep_for(Settled);
    }
    Commands waiter;
    RecordRead(waiter, region, output);
    if (!variant.otherWaits) {
        waiter.Wait(labels.Address(), Equal, Released, 0xffffffffu);
        waiter.WriteData(labels.Address(64), {After});
    }
    waiter.Submit(QueueA);
    std::this_thread::sleep_for(Settled);
    const auto start = Clock::now();
    const auto failure = region.Change(variant.protect);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);
    labels.Words()[0] = Released;
    Expect(failure.empty(), what + ": the change failed after " + std::to_string(elapsed.count()) + " ms: " + failure);
    Expect(elapsed < Prompt, what + ": the change took " + std::to_string(elapsed.count()) + " ms");
    WaitIdle();
    AgcDriver::GuestMemory::FlushGpuWrites(labels.Address(), 128u);
    AgcDriver::GuestMemory::FlushGpuWrites(output.Address(), Lanes * 4u);
    const auto marker = variant.otherWaits ? labels.Words()[32] : labels.Words()[16];
    Expect(marker == After, what + ": the store after the wait did not land (" + Hex(marker) + ")");
    std::size_t wrong = 0;
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
        const auto expected = lane < RangePages * PageBytes / 4096u ? Pattern | (lane * 1024u) : 0u;
        if (output.Words()[lane] != expected) ++wrong;
    }
    Expect(wrong == 0u, what + ": " + std::to_string(wrong) + " lanes of the dispatch before the wait did not read the range's words (first " + Hex(output.Words()[0]) + ")");
}

}

#endif

int main() {
#ifdef _WIN32
    _putenv_s("APS5_PIN_WAIT_MS", PinWaitMs);
    {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
    }
    try {
        {
            MirrorReader reader;
            PrivatePages warm(1);
            reader.Read(warm.Address(), 1);
        }
        for (const auto& variant : {Variant{"unmap", false, false, false, false}, Variant{"unmap after a warm build", true, false, false, false}, Variant{"unmap with the other queue's batch open", true, true, false, false}, Variant{"protect", false, false, true, false}, Variant{"unmap while the other queue waits", true, true, false, true}, Variant{"unmap while the other queue waits, no warm build", false, true, false, true}, Variant{"protect while the other queue waits", true, true, true, true}}) {
            try {
                Run(variant);
            } catch (const std::exception& error) {
                Expect(false, std::string(variant.name) + ": exception: " + error.what());
            }
        }
    } catch (const std::exception& error) {
        Expect(false, std::string("exception: ") + error.what());
    }
    try {
        AgcDriverShutdown_nid_postfix();
    } catch (const std::exception& error) {
        Expect(false, std::string("shutdown: ") + error.what());
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d pin wait label wait checks failed\n", failures);
        return 1;
    }
    std::printf("pin wait label wait tests passed\n");
    return 0;
#else
    std::printf("skipped, the guest arena is Windows-only here\n");
    return VulkanTestSkipped;
#endif
}
