#include "MemoryBufferFixture.hpp"
#include <chrono>
#include <thread>

namespace {

using namespace MemoryBuffers;
using NewPath::Commands;

constexpr std::uint32_t FirstSeed = 0x1100u;
constexpr std::uint32_t SecondSeed = 0x2200u;
constexpr std::uint32_t GateQueue = 0x30u;
constexpr std::uint32_t WaitEqual = 3u;
constexpr std::uint32_t Bytes = static_cast<std::uint32_t>(Words::ByteSize);

Words First;
Words Written;
std::uint32_t* const Moved = OwnBlock();
std::uint32_t* const Unrelated = OwnBlock();
TestGuest::Array<std::uint32_t, 1> Gate;
TestGuest::Array<std::uint32_t, 1> Done;

void Clear(std::uint32_t* block) {
    std::fill(block, block + BlockBytes / sizeof(std::uint32_t), Untouched);
}

bool Await(const TestGuest::Array<std::uint32_t, 1>& flag) {
    const auto* value = reinterpret_cast<const volatile std::uint32_t*>(flag.data());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (*value != 1u) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return true;
}

void Release() {
    *reinterpret_cast<volatile std::uint32_t*>(Gate.data()) = 1u;
}

void MovedDescriptor(std::span<const std::uint32_t> userData) {
    Clear(Moved);
    Clear(Unrelated);
    First.fill(Untouched);
    Place(OutputSlot, Buffer(Moved, Bytes));
    Gate[0] = 0u;
    Done[0] = 0u;
    Commands gate;
    gate.Wait(AddressOf(Gate.data()), WaitEqual, 1u, 0xffffffffu);
    gate.Submit(GateQueue);
    Commands commands;
    commands.Dispatch(ImageStoreProgram(), ImageStoreUserData(Moved, FirstSeed), Lanes, Height);
    commands.Dispatch(ImageStoreProgram(), ImageStoreUserData(Unrelated, SecondSeed), Lanes, Height);
    commands.Dispatch(StoreProgram(), userData, Lanes, Height);
    commands.WriteData(AddressOf(Done.data()), {1u});
    commands.Submit(0);
    const bool done = Await(Done);
    Expect(done, "refresh: the submission with the moved V# did not finish");
    if (done) {
        Expect(!ImagePending(Moved), "refresh: the storage image over the buffer the moved V# names was still pending once the dispatch storing into that buffer was recorded (the dispatch kept the ranges of the V# it was first compiled with)");
        Expect(ImagePending(Unrelated), "refresh: the pending storage image over memory no access of the dispatch reaches was written back before the queue went idle (the dispatch's ranges are unbounded)");
    }
    Release();
    NewPath::WaitIdle();
    ExpectStored(ReadBack(Moved, OutputWords), Height, "refresh: a CPU read of the buffer the moved V# names");
    ExpectUntouched(ReadBack(First.data(), OutputWords), "refresh: the buffer the V# named at the first dispatch");
    ExpectImage(Unrelated, SecondSeed, "refresh: the storage image over unrelated memory read back");
}

void WrittenDescriptor(std::span<const std::uint32_t> userData) {
    Clear(Moved);
    Written.fill(Untouched);
    std::array<std::uint32_t, 8> writerUserData{};
    const auto srt = Buffer(Srt.data(), static_cast<std::uint32_t>(decltype(Srt)::ByteSize));
    const auto written = Buffer(Written.data(), Bytes);
    std::copy(srt.begin(), srt.end(), writerUserData.begin());
    std::copy(written.begin(), written.end(), writerUserData.begin() + 4);
    Commands commands;
    commands.Dispatch(WriterProgram(), writerUserData, Lanes, 1);
    commands.Dispatch(StoreProgram(), userData, Lanes, Height);
    commands.Submit(0);
    NewPath::WaitIdle();
    ExpectStored(ReadBack(Written.data(), OutputWords), Height, "refresh: a dispatch through the V# an earlier dispatch of the same submission wrote");
    ExpectUntouched(ReadBack(Moved, OutputWords), "refresh: the buffer the V# named before an earlier dispatch of the same submission rewrote it");
}

void Run() {
    Srt.fill(0u);
    const auto userData = SrtUserData();
    Place(OutputSlot, Buffer(First.data(), Bytes));
    First.fill(Untouched);
    Commands commands;
    commands.Dispatch(StoreProgram(), userData, Lanes, Height);
    commands.Submit(0);
    NewPath::WaitIdle();
    ExpectStored(ReadBack(First.data(), OutputWords), Height, "refresh: the first dispatch");
    MovedDescriptor(userData);
    WrittenDescriptor(userData);
}

}

int main() {
    {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
    }
    try {
        Run();
    } catch (const std::exception& error) {
        Expect(false, std::string("exception: ") + error.what());
        Release();
    }
    try {
        AgcDriverShutdown_nid_postfix();
    } catch (const std::exception& error) {
        Expect(false, std::string("shutdown: ") + error.what());
    }
    return Finish("memory buffer refresh");
}
