#include "StorageMipFixture.hpp"

namespace {

using namespace StorageMips;

void Run(AgcDriver::VulkanDevice& device) {
    const auto full = NewTexture(1);
    const auto based = NewTexture(2);
    const auto partial = NewTexture(3);
    Surface(&full);
    const View fullView{&full, 0, Levels - 1u};
    const View basedView{&based, 1, Levels - 1u};
    Model fullModel;
    Model basedModel;
    Model partialModel;

    for (const std::uint32_t seed : {0x10000u, 0x20000u}) {
        const Dispatch dispatch(fullView, basedView, true, seed);
        const auto what = "storage mips: every level of a four-level T# with seed " + Hex(seed);
        const auto outcome = RunOne(device, CompileFor(device, dispatch, what));
        ExpectClean(outcome, what);
        if (seed != 0x10000u) Expect(outcome.Misses() == 0u, what + ": the second use missed " + std::to_string(outcome.Misses()) + " times");
        dispatch.Apply(fullModel);
        ExpectTexture(full, fullModel, what);
    }
    ExpectTexture(based, basedModel, "storage mips: the other candidate T#, never selected");

    for (const std::uint32_t round : {0x30000u, 0x50000u}) {
        const Dispatch first(fullView, basedView, true, round);
        const Dispatch second(fullView, basedView, false, round + 0x10000u);
        const auto what = "storage mips: two textures alternating across dispatches, round " + Hex(round);
        const auto firstCompiled = CompileFor(device, first, what);
        const auto secondCompiled = CompileFor(device, second, what);
        AgcDriver::GuestMemory::BumpCollectEpoch();
        Batch batch(device);
        batch.Dispatch(firstCompiled, Groups);
        batch.Dispatch(secondCompiled, Groups);
        batch.Dispatch(firstCompiled, Groups);
        const auto outcome = batch.Finish();
        ExpectClean(outcome, what);
        if (round != 0x30000u) Expect(outcome.Misses() == 0u, what + ": the second round missed " + std::to_string(outcome.Misses()) + " times");
        first.Apply(fullModel);
        second.Apply(basedModel);
        ExpectTexture(full, fullModel, what + ", the four-level T#");
        ExpectTexture(based, basedModel, what + ", the T# with base level 1 (mip 0 lands on level 1, level 0 stays untouched)");
    }

    {
        const Dispatch dispatch({&partial, 0, 1}, fullView, true, 0x70000u);
        const std::string what = "storage mips: a T# naming levels 0 to 1 of four";
        const auto compiled = CompileFor(device, dispatch, what);
        for (const std::string use : {"first use", "second use"}) {
            const auto outcome = RunOne(device, compiled);
            ExpectClean(outcome, what + ", " + use);
            if (use == "second use") Expect(outcome.Misses() == 0u, what + ": the second use missed");
        }
        dispatch.Apply(partialModel);
        ExpectTexture(partial, partialModel, what + " (writes past the last level are dropped and levels 2 and 3 stay untouched)");
    }
}

}

int main() {
    return Main("device table storage mips", Run);
}
