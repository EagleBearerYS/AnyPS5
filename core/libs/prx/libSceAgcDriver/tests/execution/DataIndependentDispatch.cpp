#include "WrittenImageFixture.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"

namespace {

using namespace WrittenImages;

void Run() {
    WriteDescriptor(0, 0);
    const auto before = AgcDriver::DriverDetail::Driver::Get().DataIndependentDispatchHits();
    Commands commands;
    for (std::uint32_t dispatch = 0; dispatch < MaxDispatches; ++dispatch) commands.Dispatch(ChainProgram(), ReaderUserData(dispatch), Lanes, Height);
    commands.Submit(0);
    NewPath::WaitIdle();
    for (std::uint32_t dispatch = 0; dispatch < MaxDispatches; ++dispatch) ExpectSampled(dispatch, [](std::uint32_t) { return 0u; }, "data-independent dispatch: reader dispatch " + std::to_string(dispatch) + " of " + std::to_string(MaxDispatches) + ", each with its own output buffer in its user data");
    const auto hits = AgcDriver::DriverDetail::Driver::Get().DataIndependentDispatchHits() - before;
    Expect(hits == MaxDispatches - 1u, "data-independent dispatch: " + std::to_string(hits) + " of the " + std::to_string(MaxDispatches - 1u) + " dispatches after the first were served without a capture");
}

}

int main() {
    return Main("data independent dispatch", Run);
}
