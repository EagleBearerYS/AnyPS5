#include "DrawSubmitFixture.hpp"
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32

namespace {

using namespace DrawSubmit;
using NewPath::PageBytes;
using NewPath::PrivatePages;
using Clock = std::chrono::steady_clock;

constexpr std::uint32_t Width = 64;
constexpr std::uint32_t Height = 64;
constexpr std::uint32_t Indices = 96;
constexpr std::uint32_t Marks = 4096;
constexpr std::uint32_t Marked = 0x40000000u;
constexpr std::uint32_t VertexUserWords = 4;
constexpr std::uint32_t Lanes = 64;
constexpr std::uint32_t Equal = 3;
constexpr std::uint32_t ComputeQueue = 0x20;
constexpr auto LabelLimit = std::chrono::seconds(10);

Header VertexHeader;
Header PixelHeader;

const Program& MarkVertex() {
    static const Program program = [] {
        using namespace DrawSubmit::Isa;
        constexpr std::uint32_t VOrB32 = 0x1cu;
        Program built;
        built << Vop2(VLshlRevB32, 4, InlineInteger(2), 5)
              << Vop2(VOrB32, 6, Literal, 5) << Marked
              << Mubuf(BufferStoreDword, 6, 4, 8)
              << Vop1(VMovB32, 16, InlineInteger(0))
              << Vop1(VMovB32, 17, InlineInteger(0))
              << Vop1(VMovB32, 18, InlineInteger(0))
              << Vop1(VMovB32, 19, InlineOne)
              << WaitAll
              << Export(PositionTarget, 16, false)
              << EndProgram;
        return built;
    }();
    return program;
}

const Program& FlatPixel() {
    static const Program program = [] {
        using namespace DrawSubmit::Isa;
        Program built;
        built << Vop1(VMovB32, 0, InlineOne)
              << Vop1(VMovB32, 1, InlineInteger(0))
              << Vop1(VMovB32, 2, InlineInteger(0))
              << Vop1(VMovB32, 3, InlineOne)
              << Export(ColorTarget, 0, true)
              << EndProgram;
        return built;
    }();
    return program;
}

const Program& IndexWriter() {
    static const Program program = [] {
        using namespace NewPath::Isa;
        constexpr std::uint32_t VLshlAddU32 = 0x346u;
        Program built;
        built << std::array<std::uint32_t, 2>{0xd4000000u | (VLshlAddU32 << 16u) | 3u, 5u | (InlineInteger(6) << 9u) | (Vgpr(0) << 18u)}
              << Vop2(VLshlRevB32, 1, InlineInteger(2), 3)
              << Vop2(VAddNcU32, 2, 4, 3)
              << Mubuf(BufferStoreDword, 2, 1, 0)
              << EndProgram;
        return built;
    }();
    return program;
}

class Scene {
public:
    Scene() : pixels(1), marks(1), indices(2), labels(1) {}

    void Reset(std::uint64_t indexOffset, std::uint32_t first) {
        offset = indexOffset;
        for (std::uint32_t word = 0; word < Width * Height; ++word) pixels.Words()[word] = 0u;
        for (std::uint32_t word = 0; word < Marks; ++word) marks.Words()[word] = 0u;
        for (std::uint64_t word = 0; word < indices.Bytes() / 4u; ++word) indices.Words()[word] = 0xffffu;
        WriteIndices(first);
        for (std::uint32_t word = 0; word < 64u; ++word) labels.Words()[word] = 0u;
        AgcDriver::GuestMemory::BumpCollectEpoch();
    }

    void WriteIndices(std::uint32_t first) {
        auto* bytes = indices.Data() + offset;
        for (std::uint32_t index = 0; index < Indices; ++index) {
            const auto value = first + index;
            std::memcpy(bytes + std::size_t{index} * 4u, &value, 4u);
        }
    }

    std::uint64_t IndexAddress() const { return indices.Address(offset); }

    void WriterDispatch(Commands& commands, std::uint32_t first) const {
        const auto buffer = NewPath::BufferResource(IndexAddress(), Indices * 4u);
        const std::array<std::uint32_t, 5> userData{buffer[0], buffer[1], buffer[2], buffer[3], first};
        commands.Dispatch(IndexWriter(), userData, Lanes, (Indices + Lanes - 1u) / Lanes);
    }

    void Draw(Commands& commands) const {
        Baseline(commands, {pixels.Data(), Width, Height}, CenterAndPosition);
        Programs(commands, MarkVertex(), VertexUserWords, FlatPixel(), 0u);
        const auto buffer = NewPath::BufferResource(marks.Address(), Marks * 4u);
        VertexUserData(commands, buffer);
        DrawSubmit::Instances(commands, 1u);
        IndexType(commands, Index32);
        DrawIndex(commands, IndexAddress(), Indices, Indices);
    }

    std::uint64_t Label(std::uint32_t index) const { return labels.Address(index * 64u); }
    volatile std::uint32_t* LabelWord(std::uint32_t index) const { return labels.Words() + index * 16u; }

    bool AwaitLabel(std::uint32_t index) const {
        for (const auto deadline = Clock::now() + LabelLimit; Clock::now() < deadline;) {
            if (LabelWord(index)[0] == 1u) return true;
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        return false;
    }

    void Check(std::uint32_t first, const std::string& what) const {
        AgcDriver::GuestMemory::FlushGpuWrites(marks.Address(), marks.Bytes());
        std::size_t missing = 0;
        std::size_t foreign = 0;
        std::string firstWrong;
        for (std::uint32_t mark = 0; mark < Marks; ++mark) {
            const bool drawn = mark >= first && mark < first + Indices;
            const auto actual = marks.Words()[mark];
            const auto expected = drawn ? (Marked | mark) : 0u;
            if (actual == expected) continue;
            if (drawn) ++missing;
            else ++foreign;
            if (firstWrong.empty()) firstWrong = "vertex " + std::to_string(mark) + " marked " + Hex(actual) + ", expected " + Hex(expected);
        }
        Expect(missing == 0u && foreign == 0u, what + ": " + std::to_string(missing) + " of the " + std::to_string(Indices) + " indexed vertices did not run and " + std::to_string(foreign) + " other vertices ran; first " + firstWrong);
    }

private:
    PrivatePages pixels;
    PrivatePages marks;
    PrivatePages indices;
    PrivatePages labels;
    std::uint64_t offset = 0;
};

std::string Settled() {
    std::string failure;
    try {
        NewPath::WaitIdle();
        NewPath::Settle();
    } catch (const std::exception& error) {
        failure = error.what();
    }
    return failure;
}

constexpr std::uint32_t Stale = 100;
constexpr std::uint32_t Fresh = 2000;

void WrittenBySameSubmission(Scene& scene) {
    scene.Reset(0x100u, Stale);
    const std::string what = "an indexed draw whose 32-bit indices a dispatch earlier in its submission wrote";
    Commands commands;
    scene.WriterDispatch(commands, Fresh);
    scene.Draw(commands);
    commands.WriteData(scene.Label(0), {1u});
    commands.Submit(0);
    Expect(scene.AwaitLabel(0), what + ": the label never landed");
    const auto failure = Settled();
    Expect(failure.empty(), what + ": the submission failed: " + failure);
    scene.Check(Fresh, what);
}

void WrittenByCpuBeforeExecution(Scene& scene) {
    scene.Reset(0x100u, Stale);
    const std::string what = "an indexed draw waiting on a CPU-written label, whose indices the CPU rewrote after the submission and before writing the label";
    Commands commands;
    commands.Wait(scene.Label(1), Equal, 1u, 0xffffffffu);
    scene.Draw(commands);
    commands.WriteData(scene.Label(0), {1u});
    commands.Submit(0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    scene.WriteIndices(Fresh);
    scene.LabelWord(1)[0] = 1u;
    Expect(scene.AwaitLabel(0), what + ": the label never landed");
    const auto failure = Settled();
    Expect(failure.empty(), what + ": the submission failed: " + failure);
    scene.Check(Fresh, what);
}

void AcrossPageBoundary(Scene& scene) {
    const auto offset = PageBytes - Indices * 2u - 4u;
    scene.Reset(offset, Fresh);
    const std::string what = "an indexed draw whose indices cross a 64 KiB page boundary of the index buffer";
    Commands commands;
    scene.Draw(commands);
    commands.WriteData(scene.Label(0), {1u});
    commands.Submit(0);
    Expect(scene.AwaitLabel(0), what + ": the label never landed");
    const auto failure = Settled();
    Expect(failure.empty(), what + ": the submission failed: " + failure);
    scene.Check(Fresh, what);
}

void WrittenByOtherStream(Scene& scene) {
    scene.Reset(0x100u, Stale);
    const std::string what = "an indexed draw waiting on the label of a dispatch on another queue that wrote its indices";
    Commands draw;
    draw.Wait(scene.Label(1), Equal, 1u, 0xffffffffu);
    scene.Draw(draw);
    draw.WriteData(scene.Label(0), {1u});
    draw.Submit(0);
    Commands writer;
    scene.WriterDispatch(writer, Fresh);
    writer.WriteData(scene.Label(1), {1u});
    writer.Submit(ComputeQueue);
    Expect(scene.AwaitLabel(0), what + ": the draw's label never landed");
    const auto failure = Settled();
    Expect(failure.empty(), what + ": the submissions failed: " + failure);
    scene.Check(Fresh, what);
}

void Run() {
    Register(VertexHeader, MarkVertex(), VertexShaderType);
    Register(PixelHeader, FlatPixel(), PixelShaderType);
    Scene scene;
    WrittenBySameSubmission(scene);
    WrittenByCpuBeforeExecution(scene);
    AcrossPageBoundary(scene);
    WrittenByOtherStream(scene);
}

}

#endif

int main() {
#ifdef _WIN32
    return DrawSubmit::Main("index input ordering", Run);
#else
    std::printf("skipped, the guest arena is Windows-only here\n");
    return VulkanTestSkipped;
#endif
}
