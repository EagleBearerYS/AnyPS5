#include "NewPathFixture.hpp"
#include "VulkanTestDevice.hpp"
#include <chrono>
#include <exception>
#include <thread>

#ifdef _WIN32

namespace {

using namespace NewPath;

constexpr std::uint32_t WriterQueue = 0x20;
constexpr std::uint32_t WaiterQueue = 0x28;
constexpr std::uint32_t CopyPages = 512;
constexpr std::uint32_t Copies = 4;
constexpr std::uint32_t Equal = 3;
constexpr std::uint32_t Later = 0x6c000001u;

int failures = 0;

void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

bool Recorded(std::uint64_t address) {
    const auto* recorder = AgcDriver::Graphics::Recorder::Active();
    return recorder != nullptr && recorder->LabelPendingAt(address);
}

struct Observation {
    bool recorded = false;
    bool beforeLanding = false;
};

Observation WatchRecord(std::uint64_t target, volatile std::uint32_t* label, std::uint32_t initial) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (Recorded(target)) return {true, *label == initial};
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    return {};
}

class Memory {
public:
    Memory() : sources(Copies * CopyPages), destinations(Copies * CopyPages), slots(1) {
        std::memset(sources.Data(), 0x3c, sources.Bytes());
        for (std::uint32_t copy = 0; copy < Copies; ++copy) {
            auto* tail = sources.Data() + (std::uint64_t{copy + 1u} * CopyPages) * PageBytes - 16u;
            for (std::uint32_t byte = 0; byte < 16u; ++byte) tail[byte] = static_cast<std::uint8_t>(0x80u + copy * 16u + byte);
        }
        std::memset(slots.Data(), 0, slots.Bytes());
    }

    void Reset() {
        std::memset(destinations.Data(), 0xc3, destinations.Bytes());
        std::memset(slots.Data(), 0, slots.Bytes());
    }

    void SlowWrites(Commands& commands) const {
        for (std::uint32_t copy = 0; copy < Copies; ++copy) {
            const auto offset = std::uint64_t{copy} * CopyPages * PageBytes;
            commands.DmaCopy(destinations.Address(offset), sources.Address(offset), static_cast<std::uint32_t>(CopyPages * PageBytes));
        }
    }

    std::uint64_t LastTail() const { return destinations.Address(std::uint64_t{Copies} * CopyPages * PageBytes - 16u); }
    const std::uint8_t* SourceTail() const { return sources.Data() + std::uint64_t{Copies} * CopyPages * PageBytes - 16u; }

    std::uint64_t Slot(std::uint32_t index) const { return slots.Address(std::uint64_t{index} * 64u); }
    volatile std::uint32_t* SlotWords(std::uint32_t index) const { return slots.Words() + index * 16u; }

private:
    PrivatePages sources;
    PrivatePages destinations;
    PrivatePages slots;
};

void GpuSideWait(Memory& memory) {
    memory.Reset();
    const auto label = memory.Slot(0);
    const auto target = memory.Slot(1);
    const auto check = memory.Slot(2);
    Commands waiter;
    waiter.Wait(label, Equal, 1u, 0xffffffffu);
    waiter.DmaCopy(check, memory.LastTail(), 16u);
    waiter.WriteData(target, {Later});
    waiter.Submit(WaiterQueue);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    Expect(!Recorded(target), "GPU-side wait: the work after a wait was recorded before any writer of its label existed");
    Commands writer;
    memory.SlowWrites(writer);
    writer.WriteData(label, {1u});
    writer.Submit(WriterQueue);
    const auto seen = WatchRecord(target, memory.SlotWords(0), 0u);
    Expect(seen.recorded, "GPU-side wait: the waiter never recorded the work after its wait");
    Expect(seen.beforeLanding, "GPU-side wait: the waiter recorded the work after a wait on another queue's recorded label only once the label was in memory (a CPU wait)");
    WaitIdle();
    AgcDriver::GuestMemory::FlushGpuWrites(memory.Slot(0), 3 * 64u);
    Expect(memory.SlotWords(1)[0] == Later, "GPU-side wait: the store after the wait did not land");
    const auto* copied = reinterpret_cast<const volatile std::uint8_t*>(memory.SlotWords(2));
    for (std::uint32_t byte = 0; byte < 16u; ++byte) {
        if (copied[byte] == memory.SourceTail()[byte]) continue;
        Expect(false, "GPU-side wait: the copy after the wait read byte " + std::to_string(byte) + " of the writer's last copy as " + Hex(copied[byte]) + ", before the writer's copy landed (" + Hex(memory.SourceTail()[byte]) + ")");
        break;
    }
}

void CpuWrittenLabel(Memory& memory) {
    memory.Reset();
    const auto label = memory.Slot(4);
    const auto target = memory.Slot(5);
    Commands waiter;
    waiter.Wait(label, Equal, 7u, 0xffffffffu);
    waiter.WriteData(target, {Later});
    waiter.Submit(WaiterQueue);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    Expect(!Recorded(target), "CPU-written label: the work after a wait on a value no recorded work writes was recorded before the CPU stored it");
    Expect(memory.SlotWords(5)[0] == 0u, "CPU-written label: the work after the wait ran before the CPU stored the value");
    memory.SlotWords(4)[0] = 7u;
    WaitIdle();
    AgcDriver::GuestMemory::FlushGpuWrites(target, 4u);
    Expect(memory.SlotWords(5)[0] == Later, "CPU-written label: the work after the wait did not run once the CPU stored the value");
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
        Memory memory;
        {
            Commands open;
            open.WriteData(memory.Slot(8), {1u});
            open.Submit(0);
            WaitIdle();
        }
        const auto* recorder = AgcDriver::Graphics::Recorder::Active();
        Require(recorder != nullptr, "stream waits: the driver opened no device");
        if (recorder->Streams() < 2) {
            std::printf("skipped, the device gives the recorder one stream\n");
            AgcDriverShutdown_nid_postfix();
            return VulkanTestSkipped;
        }
        GpuSideWait(memory);
        CpuWrittenLabel(memory);
        AgcDriverShutdown_nid_postfix();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: exception: %s\n", error.what());
        AgcDriverShutdown_nid_postfix();
        return 1;
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d new path stream wait checks failed\n", failures);
        return 1;
    }
    std::printf("new path stream wait tests passed\n");
    return 0;
#else
    std::printf("skipped, the guest arena is Windows-only here\n");
    return VulkanTestSkipped;
#endif
}
