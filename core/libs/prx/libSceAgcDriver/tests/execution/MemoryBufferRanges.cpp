#include "MemoryBufferFixture.hpp"

namespace {

using namespace MemoryBuffers;

Words Input;
Words Moved;
Words Stale;
Words Other;

constexpr std::uint32_t Bytes = static_cast<std::uint32_t>(Words::ByteSize);

std::uint64_t Begin(const Words& words) {
    return AddressOf(words.data());
}

std::uint64_t End(const Words& words) {
    return AddressOf(words.data()) + Words::ByteSize;
}

void StoreThroughMemory(AgcDriver::VulkanDevice& device) {
    Srt.fill(0u);
    const auto userData = SrtUserData();
    const auto compiled = Compile(device, StoreProgram(), userData);
    ExpectNothingCaptured(compiled, "memory V# store");
    const auto& result = *compiled.result;
    Expect(result.bdaWrites && !result.unboundedWrites, "memory V# store: a store through a V# the program loads from its SRT in guest memory has unbounded writes (" + Writes(result) + ")");
    Expect(Contains(result.writeRanges, Begin(Output), End(Output)), "memory V# store: the " + Writes(result) + " do not hold the V#'s range " + Span(Begin(Output), End(Output)));
    Batch batch(device);
    batch.Dispatch(compiled, Height);
    Expect(PendingWrite(Output.data(), Words::ByteSize), "memory V# store: the dispatch's stores through the V# are not pending");
    Expect(!PendingWrite(Other.data(), Words::ByteSize), "memory V# store: memory outside the V# is pending once the dispatch is recorded");
    const auto outcome = batch.Finish();
    ExpectClean(outcome, "memory V# store");
    ExpectStored(outcome.words, Height, "memory V# store");

    Place(OutputSlot, Buffer(Moved.data(), Bytes));
    const auto moved = Compile(device, StoreProgram(), userData);
    Expect(!moved.result->unboundedWrites && Contains(moved.result->writeRanges, Begin(Moved), End(Moved)), "memory V# store: after the V# in guest memory moved to another buffer, the next compile's " + Writes(*moved.result) + " do not hold the new range " + Span(Begin(Moved), End(Moved)));
    Expect(!Covers(moved.result->writeRanges, Begin(Output), End(Output)), "memory V# store: after the V# in guest memory moved to another buffer, the next compile's write ranges still hold the old buffer");
    Batch second(device);
    second.Dispatch(moved, Height);
    const auto after = second.Finish();
    ExpectClean(after, "memory V# store: the moved V#");
    ExpectUntouched(after.words, "memory V# store: the old buffer after the V# moved");
    ExpectStored(ReadBack(Moved.data(), OutputWords), Height, "memory V# store: the buffer the moved V# names");
}

void LoadThroughMemory(AgcDriver::VulkanDevice& device) {
    for (std::uint32_t word = 0; word < OutputWords; ++word) Input[word] = InputValue(word);
    Srt.fill(0u);
    const auto userData = SrtUserData();
    Place(InputSlot, Buffer(Input.data(), Bytes));
    const auto compiled = Compile(device, CopyProgram(), userData);
    ExpectNothingCaptured(compiled, "memory V# load");
    const auto& result = *compiled.result;
    Expect(!result.unboundedReads, "memory V# load: a load through a V# the program loads from its SRT in guest memory makes its reads unbounded (" + Reads(result) + ")");
    Expect(Contains(result.readRanges, Begin(Input), End(Input)), "memory V# load: the " + Reads(result) + " do not hold the V#'s range " + Span(Begin(Input), End(Input)));
    Expect(!result.unboundedWrites && Contains(result.writeRanges, Begin(Output), End(Output)), "memory V# load: the " + Writes(result) + " do not hold the stored V#'s range " + Span(Begin(Output), End(Output)));
    const auto outcome = Run(device, compiled, Height);
    ExpectClean(outcome, "memory V# load");
    ExpectRows(outcome.words, Height, [](std::uint32_t row, std::uint32_t lane, std::uint32_t component) { return InputValue((row * Lanes + lane) * LaneWords + component); }, "memory V# load: the copied words");
}

void PendingDescriptor(AgcDriver::VulkanDevice& device) {
    Stale.fill(Untouched);
    Srt.fill(0u);
    const auto userData = SrtUserData();
    Place(OutputSlot, Buffer(Stale.data(), Bytes));
    const auto slot = AddressOf(Srt.data() + OutputSlot);
    const auto control = Compile(device, StoreProgram(), userData, RecordedWrites);
    Expect(!control.result->unboundedWrites && Contains(control.result->writeRanges, Begin(Stale), End(Stale)), "pending V#: with no GPU work pending over the SRT, the " + Writes(*control.result) + " do not hold the V#'s range " + Span(Begin(Stale), End(Stale)));
    std::array<std::uint32_t, 8> writerUserData{};
    const auto srt = Buffer(Srt.data(), static_cast<std::uint32_t>(decltype(Srt)::ByteSize));
    const auto written = Buffer(Output.data(), Bytes);
    std::copy(srt.begin(), srt.end(), writerUserData.begin());
    std::copy(written.begin(), written.end(), writerUserData.begin() + 4);
    const auto writer = Compile(device, WriterProgram(), writerUserData);
    Expect(!writer.result->unboundedWrites && Contains(writer.result->writeRanges, AddressOf(Srt.data()), AddressOf(Srt.data()) + decltype(Srt)::ByteSize), "pending V#: the writer's store through its user-data V# has the " + Writes(*writer.result));
    Batch batch(device);
    batch.Dispatch(writer, 1);
    Expect(PendingWrite(Srt.data() + OutputSlot, 16u), "pending V#: the writer's store into the SRT is not pending once recorded");
    const auto pending = Compile(device, StoreProgram(), userData, RecordedWrites);
    Expect(pending.result->unboundedWrites, "pending V#: a V# whose words recorded GPU work writes was bounded from the words in memory before that work (" + Writes(*pending.result) + ")");
    Expect(PendingWrite(Srt.data() + OutputSlot, 16u), "pending V#: compiling the dispatch waited for the recorded work that writes its V# at " + NewPath::Hex(slot));
    batch.Dispatch(pending, Height);
    const auto outcome = batch.Finish();
    ExpectClean(outcome, "pending V#");
    ExpectStored(outcome.words, Height, "pending V#: a dispatch through the V# an earlier dispatch of the same batch wrote");
    ExpectUntouched(ReadBack(Stale.data(), OutputWords), "pending V#: the buffer the V# named before the earlier dispatch rewrote it");
}

void Run(AgcDriver::VulkanDevice& device) {
    Moved.fill(Untouched);
    Other.fill(Untouched);
    StoreThroughMemory(device);
    LoadThroughMemory(device);
    PendingDescriptor(device);
}

}

int main() {
    return Main("memory buffer ranges", Run);
}
