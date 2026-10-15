#include "WrittenImageFixture.hpp"

namespace {

using namespace WrittenImages;

void Run() {
    Commands commands;
    commands.Dispatch(WriterProgram(), WriterUserData(0), Lanes, 1);
    commands.Dispatch(ChainProgram(), ReaderUserData(0), Lanes, Height);
    commands.Submit(0);
    NewPath::WaitIdle();
    ExpectDescriptor(0, 0, "written image: the T# the writer dispatch stored");
    ExpectSampled(0, [](std::uint32_t) { return 0u; }, "written image: the first use of a T# an earlier dispatch of the same submission wrote, loaded through a scalar chain");
    Commands again;
    again.Dispatch(ChainProgram(), ReaderUserData(1), Lanes, Height);
    again.Submit(0);
    NewPath::WaitIdle();
    ExpectSampled(1, [](std::uint32_t) { return 0u; }, "written image: a later submission's use of the T# the writer dispatch stored");
}

}

int main() {
    return Main("written image same submission", Run);
}
