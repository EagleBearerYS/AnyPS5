#include "WrittenImageFixture.hpp"

namespace {

using namespace WrittenImages;

void Run() {
    Commands commands;
    commands.Dispatch(WriterProgram(), WriterUserData(0), Lanes, 1);
    for (std::uint32_t dispatch = 0; dispatch < MaxDispatches; ++dispatch) commands.Dispatch(ChainProgram(), ReaderUserData(dispatch), Lanes, Height);
    commands.Submit(0);
    NewPath::WaitIdle();
    ExpectDescriptor(0, 0, "written image: the T# the writer dispatch stored");
    for (std::uint32_t dispatch = 0; dispatch < MaxDispatches; ++dispatch) ExpectSampled(dispatch, [](std::uint32_t) { return 0u; }, "written image: reader dispatch " + std::to_string(dispatch) + " of " + std::to_string(MaxDispatches) + " after the dispatch that wrote the T#, in the same submission");
}

}

int main() {
    return Main("written image repeated reads", Run);
}
