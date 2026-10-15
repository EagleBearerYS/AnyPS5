#include "StorageMipFixture.hpp"

namespace {

using namespace StorageMips;

void Run(AgcDriver::VulkanDevice& device) {
    const auto full = NewTexture(1);
    const auto inverted = NewTexture(4);
    Surface(&full);
    const View fullView{&full, 0, Levels - 1u};
    Model fullModel;

    {
        const Dispatch dispatch({&inverted, 2, 1}, fullView, false, 0x80000u);
        const std::string what = "storage mips: a dispatch selecting a valid T#, whose other candidate T# has its last level below its base level";
        try {
            const auto outcome = RunOne(device, CompileFor(device, dispatch, what));
            ExpectClean(outcome, what);
            dispatch.Apply(fullModel);
        } catch (const std::exception& error) {
            Expect(false, what + ": the program was refused before it ran: " + FirstLine(error.what()));
        }
        ExpectTexture(full, fullModel, what + ", the selected T#");
        ExpectTexture(inverted, {}, what + ": the unselected texture was written");
    }

    {
        const Dispatch dispatch({&inverted, 2, 1}, fullView, true, 0x90000u);
        const std::string what = "storage mips: a T# whose last level is below its base level";
        try {
            const auto compiled = CompileFor(device, dispatch, what);
            const auto missed = RunOne(device, compiled);
            const auto refused = RunOne(device, compiled);
            const auto logs = missed.log + refused.log;
            Expect(missed.log.find("selected a descriptor the device image table rejected") != std::string::npos && missed.log.find("invalid mip range") != std::string::npos, what + ": its first use did not fault on the device image table's rejection of its mip range:\n" + missed.log);
            Expect(refused.log.find("selected a descriptor the device image table rejected") != std::string::npos, what + ": a use after the rejection did not fault:\n" + refused.log);
            Expect(logs.find("is rejected") == std::string::npos, what + ": the rejection of a candidate T# was reported as a sampled descriptor's:\n" + logs);
        } catch (const std::exception& error) {
            Expect(false, what + ": the program was refused before it ran instead of the device image table rejecting the T# at use: " + FirstLine(error.what()));
        }
        ExpectTexture(inverted, {}, what + ": the texture was written");
        ExpectTexture(full, fullModel, what + ": the other candidate T# was written");
    }
}

}

int main() {
    return Main("device table storage mip range", Run);
}
