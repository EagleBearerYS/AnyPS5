#include "MemoryBufferFixture.hpp"

namespace {

using namespace MemoryBuffers;

constexpr std::uint32_t Seed = 0x2468u;

Words Input;
std::uint32_t* const Texels = OwnBlock();

void Run(AgcDriver::VulkanDevice& device) {
    for (std::uint32_t word = 0; word < OutputWords; ++word) Input[word] = InputValue(word);
    std::fill(Texels, Texels + BlockBytes / sizeof(std::uint32_t), Untouched);
    Srt.fill(0u);
    const auto userData = SrtUserData();
    Place(InputSlot, Buffer(Input.data(), static_cast<std::uint32_t>(Words::ByteSize)));
    const auto store = Compile(device, ImageStoreProgram(), ImageStoreUserData(Texels, Seed));
    const auto copy = Compile(device, CopyProgram(), userData);
    Expect(!copy.result->unboundedReads && !copy.result->unboundedWrites, "pending images: the copy through memory V#s has the " + Reads(*copy.result) + " and the " + Writes(*copy.result));
    Batch batch(device);
    batch.Dispatch(store, Height);
    Expect(ImagePending(Texels), "pending images: the storage image the first dispatch stored is not pending");
    batch.Dispatch(copy, Height);
    Expect(ImagePending(Texels), "pending images: a dispatch whose buffer accesses all go through bounded memory V#s of other memory wrote back the pending storage image (its page-table build stored every pending image)");
    const auto outcome = batch.Finish();
    ExpectClean(outcome, "pending images");
    ExpectRows(outcome.words, Height, [](std::uint32_t row, std::uint32_t lane, std::uint32_t component) { return InputValue((row * Lanes + lane) * LaneWords + component); }, "pending images: the words the copy loaded through its memory V#");
    ExpectImage(Texels, Seed, "pending images: the storage image read back after both dispatches");
}

}

int main() {
    return Main("memory buffer pending images", Run);
}
