#include "WrittenImageFixture.hpp"

namespace {

using namespace WrittenImages;

void Run() {
    WriteDescriptor(0, 0);
    Commands commands;
    commands.Dispatch(ChainProgram(), ReaderUserData(0), Lanes, Height);
    commands.Submit(0);
    NewPath::WaitIdle();
    ExpectSampled(0, [](std::uint32_t) { return 0u; }, "written image: the first use of a T# the CPU wrote, loaded through a scalar chain");
}

}

int main() {
    return Main("written image cpu written", Run);
}
