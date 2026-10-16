#include "NewPathFixture.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <string>
#include <thread>

#ifdef _WIN32

namespace {

using namespace NewPath;
using Clock = std::chrono::steady_clock;

constexpr std::uint32_t SpinLanes = 64;
constexpr std::uint32_t CalibrationRounds = 1u << 20u;
constexpr std::uint32_t LabelWord = 4096;
constexpr std::uint32_t LabelValue = 0x1abe1001u;
constexpr auto Busy = std::chrono::milliseconds(500);
constexpr auto Idle = std::chrono::milliseconds(300);
constexpr auto CpuLimit = std::chrono::milliseconds(50);
constexpr auto WakeLimit = std::chrono::milliseconds(20);
constexpr auto Slack = std::chrono::milliseconds(2);
constexpr auto IdleLimit = std::chrono::milliseconds(100);

int failures = 0;

void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

std::chrono::microseconds ThreadCpu() {
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    Require(GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user) != 0, "unmap wait: GetThreadTimes failed");
    const auto ticks = [](const FILETIME& time) { return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32u) | time.dwLowDateTime; };
    return std::chrono::microseconds((ticks(kernel) + ticks(user)) / 10u);
}

long long Milliseconds(Clock::duration duration) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

const Program& SpinProgram() {
    static const Program program = [] {
        using namespace Isa;
        constexpr std::uint32_t VLshrRevB32 = 0x16u;
        constexpr std::uint32_t VXorB32 = 0x1du;
        constexpr std::uint32_t MoveS4ToS9 = 0xbe890304u;
        constexpr std::uint32_t DecrementS9 = 0x80898109u;
        constexpr std::uint32_t CompareS9NotZero = 0xbf078009u;
        constexpr std::uint32_t BranchBackWhileNotZero = 0xbf85fff7u;
        Program built;
        built << Vop2(VAddNcU32, 1, InlineInteger(1), 0) << MoveS4ToS9
              << Vop2(VLshlRevB32, 2, InlineInteger(13), 1) << Vop2(VXorB32, 1, Vgpr(1), 2)
              << Vop2(VLshrRevB32, 2, InlineInteger(17), 1) << Vop2(VXorB32, 1, Vgpr(1), 2)
              << Vop2(VLshlRevB32, 2, InlineInteger(5), 1) << Vop2(VXorB32, 1, Vgpr(1), 2)
              << DecrementS9 << CompareS9NotZero << BranchBackWhileNotZero
              << Vop2(VLshlRevB32, 3, InlineInteger(2), 0) << Mubuf(BufferStoreDword, 1, 3, 0) << EndProgram;
        return built;
    }();
    return program;
}

class GuestBlock {
public:
    GuestBlock() {
        base = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(PageBytes, PageBytes));
        Require(base != nullptr, "unmap wait: cannot reserve guest pages");
        GuestArena::GuestArenaCommit_nid_postfix(base, PageBytes, PAGE_READWRITE, 16384);
        std::memset(base, 0, PageBytes);
        GuestAllocations::Mutation().Add(base, PageBytes, true, true, true);
    }

    ~GuestBlock() {
        if (registered) {
            try {
                GuestAllocations::Mutation().Remove(base);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "FAIL: unmap wait: a range could not be removed at the end: %s\n", error.what());
            }
        }
        GuestArena::GuestArenaReset_nid_postfix(base, PageBytes);
        GuestArena::GuestArenaRelease_nid_postfix(base, PageBytes);
    }

    GuestBlock(const GuestBlock&) = delete;
    GuestBlock& operator=(const GuestBlock&) = delete;

    void* Base() const { return base; }
    std::uint64_t Address(std::uint64_t offset = 0) const { return reinterpret_cast<std::uint64_t>(base) + offset; }
    volatile std::uint32_t* Words() const { return reinterpret_cast<volatile std::uint32_t*>(base); }
    void Unregistered() { registered = false; }

private:
    std::uint8_t* base = nullptr;
    bool registered = true;
};

struct Removal {
    std::string failure;
    Clock::time_point start;
    Clock::time_point end;
    std::chrono::microseconds cpu{0};
};

Removal Remove(GuestBlock& block) {
    Removal removal;
    const auto cpu = ThreadCpu();
    removal.start = Clock::now();
    try {
        GuestAllocations::Mutation().Remove(block.Base());
        block.Unregistered();
    } catch (const std::exception& error) {
        removal.failure = error.what();
    }
    removal.end = Clock::now();
    removal.cpu = ThreadCpu() - cpu;
    return removal;
}

void Spin(Commands& commands, const GuestBlock& output, std::uint32_t rounds) {
    const auto buffer = BufferResource(output.Address(), SpinLanes * 4u);
    const std::array<std::uint32_t, 5> userData{buffer[0], buffer[1], buffer[2], buffer[3], rounds};
    commands.Dispatch(SpinProgram(), userData, SpinLanes);
}

std::uint32_t Calibrate(const GuestBlock& output) {
    Commands commands;
    Spin(commands, output, CalibrationRounds);
    const auto start = Clock::now();
    commands.Submit(0);
    WaitIdle();
    const auto took = std::max<long long>(Milliseconds(Clock::now() - start), 1);
    const auto rounds = static_cast<std::uint64_t>(CalibrationRounds) * static_cast<std::uint64_t>(Busy.count()) / static_cast<std::uint64_t>(took);
    return static_cast<std::uint32_t>(std::clamp<std::uint64_t>(rounds, CalibrationRounds, std::uint64_t{1} << 30u));
}

bool LabelPending(std::uint64_t address) {
    const auto* recorder = AgcDriver::Graphics::Recorder::Active();
    return recorder != nullptr && recorder->LabelPendingAt(address);
}

void BatchInFlight() {
    GuestBlock output;
    GuestBlock target;
    const auto rounds = Calibrate(output);
    output.Words()[LabelWord] = 0u;
    const auto label = output.Address(LabelWord * 4u);
    Commands commands;
    Spin(commands, output, rounds);
    commands.WriteData(label, {LabelValue});
    std::atomic<bool> stop{false};
    std::atomic<Clock::rep> completedAt{0};
    std::thread watcher([&] {
        while (!stop.load()) {
            if (output.Words()[LabelWord] == LabelValue) {
                completedAt.store(Clock::now().time_since_epoch().count());
                return;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });
    commands.Submit(0);
    bool recorded = false;
    for (const auto deadline = Clock::now() + std::chrono::seconds(5); !recorded && Clock::now() < deadline;) {
        recorded = LabelPending(label);
        if (!recorded) std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto removal = Remove(target);
    WaitIdle();
    const auto waitEnd = Clock::now() + std::chrono::seconds(5);
    while (completedAt.load() == 0 && Clock::now() < waitEnd) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    stop.store(true);
    watcher.join();
    const std::string what = "a guest removal of a range that a recorded batch running about " + std::to_string(Busy.count()) + " ms and the cached address space pin" + (recorded ? "" : " (the batch's label was never seen pending)");
    Expect(removal.failure.empty(), what + ": the removal failed: " + removal.failure);
    Expect(completedAt.load() != 0, what + ": the batch's label never landed");
    if (completedAt.load() != 0) {
        const Clock::time_point completed{Clock::duration(completedAt.load())};
        Expect(removal.end + Slack >= completed, what + ": the removal returned " + std::to_string(Milliseconds(completed - removal.end)) + " ms before the batch that pins the range completed");
        Expect(removal.end < completed + WakeLimit, what + ": the removal returned " + std::to_string(Milliseconds(removal.end - completed)) + " ms after the batch completed, expected within " + std::to_string(WakeLimit.count()) + " ms");
    }
    Expect(removal.cpu < CpuLimit, what + ": the removing thread used " + std::to_string(removal.cpu.count() / 1000) + " ms of CPU time over a wait of " + std::to_string(Milliseconds(removal.end - removal.start)) + " ms, expected under " + std::to_string(CpuLimit.count()) + " ms");
}

void IdleDriver() {
    GuestBlock output;
    GuestBlock target;
    Commands commands;
    Spin(commands, output, 1u);
    commands.Submit(0);
    WaitIdle();
    Settle();
    std::this_thread::sleep_for(Idle);
    const auto removal = Remove(target);
    const std::string what = "a guest removal after the driver did no work for " + std::to_string(Idle.count()) + " ms";
    Expect(removal.failure.empty(), what + ": the removal failed: " + removal.failure);
    Expect(removal.end - removal.start < IdleLimit, what + ": the removal took " + std::to_string(Milliseconds(removal.end - removal.start)) + " ms, expected under " + std::to_string(IdleLimit.count()) + " ms");
    Expect(removal.cpu < CpuLimit, what + ": the removing thread used " + std::to_string(removal.cpu.count() / 1000) + " ms of CPU time");
}

}

#endif

int main() {
#ifdef _WIN32
    _putenv_s("APS5_PIN_WAIT_MS", "5000");
    {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
    }
    const std::array<std::pair<const char*, void (*)()>, 2> cases{{{"batch in flight", BatchInFlight}, {"idle driver", IdleDriver}}};
    for (const auto& [name, run] : cases) {
        try {
            run();
        } catch (const std::exception& error) {
            Expect(false, std::string(name) + ": exception: " + error.what());
        }
    }
    try {
        AgcDriverShutdown_nid_postfix();
    } catch (const std::exception& error) {
        Expect(false, std::string("shutdown: ") + error.what());
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d unmap wait batch checks failed\n", failures);
        return 1;
    }
    std::printf("unmap wait batch tests passed\n");
    return 0;
#else
    std::printf("skipped, the guest arena is Windows-only here\n");
    return VulkanTestSkipped;
#endif
}
