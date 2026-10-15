#include "WrittenImageFixture.hpp"

namespace {

using namespace WrittenImages;

void Run() {
    for (std::uint32_t texture = 0; texture < Textures; ++texture) WriteDescriptor(texture, texture);
    Commands commands;
    for (std::uint32_t dispatch = 0; dispatch < MaxDispatches; ++dispatch) commands.Dispatch(GroupProgram(), ReaderUserData(dispatch), Lanes, Height);
    commands.Submit(0);
    NewPath::WaitIdle();
    for (std::uint32_t dispatch = 1; dispatch < MaxDispatches; ++dispatch) ExpectSampled(dispatch, [](std::uint32_t row) { return row; }, "written image: reader dispatch " + std::to_string(dispatch) + " of " + std::to_string(MaxDispatches) + " in one submission, after the first missed the T#s each workgroup selects");
}

}

int main() {
    return Main("written image group selected repeated", Run);
}
