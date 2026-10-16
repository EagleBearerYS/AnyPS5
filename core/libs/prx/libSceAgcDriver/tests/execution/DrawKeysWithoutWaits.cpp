#include "DrawSubmitFixture.hpp"
#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32

namespace {

using DeviceTable::Expect;
using NewPath::Commands;
using NewPath::Hex;
using NewPath::High;
using NewPath::Low;
using NewPath::PageBytes;
using NewPath::PrivatePages;
using NewPath::Program;
using Clock = std::chrono::steady_clock;

constexpr std::uint32_t Side = 128;
constexpr std::uint32_t Texels = Side * Side;
constexpr std::uint32_t KeyBytes = Texels * 4u / 256u;
constexpr std::uint32_t KeyLanes = KeyBytes / 4u;
constexpr std::uint32_t DccEnable = 0x10000000u;
constexpr std::uint32_t RenderTargetAttrib3 = (0x1bu << 14u) | (1u << 24u) | (1u << 27u);
constexpr std::uint32_t Format8888Unorm = 56;
constexpr std::uint32_t TileRenderTarget64KB = 0x1b;
constexpr std::uint32_t VertexUserWords = 4;
constexpr std::uint32_t Garbage = 0x40404040u;
constexpr std::uint32_t Red = 0xff0000ffu;
constexpr std::uint32_t Green = 0xff00ff00u;
constexpr std::uint32_t White = 0xffffffffu;
constexpr std::uint32_t Black = 0x00000000u;
constexpr std::uint32_t KeysClear0000 = 0x00000000u;
constexpr std::uint32_t KeysClear1111 = 0xc0c0c0c0u;
constexpr std::uint32_t KeysUncompressed = 0xffffffffu;
constexpr std::uint32_t SpinLanes = 64;
constexpr std::uint32_t CalibrationRounds = 1u << 20u;
constexpr std::uint64_t MaxRounds = std::uint64_t{1} << 31u;
constexpr auto Busy = std::chrono::milliseconds(600);
constexpr auto CalibrationTime = std::chrono::microseconds(50000);
constexpr std::uint32_t SpinLabelWord = 1024;
constexpr std::uint32_t DrawLabelWord = 2048;
constexpr std::uint32_t OneOverSide = 0x3c000000u;
constexpr std::array<std::uint32_t, 4> PointSampler{0x00000092u, 0x00fff000u, 0u, 0u};

struct Rect {
    std::uint32_t left;
    std::uint32_t top;
    std::uint32_t right;
    std::uint32_t bottom;
};

constexpr Rect Drawn{16, 24, 80, 72};
constexpr Rect Whole{0, 0, Side, Side};

using Corners = std::array<std::array<float, 4>, 3>;
constexpr Corners FullScreen{{{-1.0f, -1.0f, 0.5f, 1.0f}, {3.0f, -1.0f, 0.5f, 1.0f}, {-1.0f, 3.0f, 0.5f, 1.0f}}};

DrawSubmit::Header VertexHeader;
DrawSubmit::Header RedHeader;
DrawSubmit::Header GreenHeader;
TestGuest::Array<std::uint32_t, DeviceTable::SrtWords> SampleSrt;

const Program& BoundedVertex() {
    static const Program program = [] {
        using namespace DrawSubmit::Isa;
        Program built;
        built << Indexed(BufferLoadDwordX4, 10, 5, 8)
              << WaitAll
              << Export(PositionTarget, 10, false)
              << EndProgram;
        return built;
    }();
    return program;
}

Program FlatPixel(std::uint32_t red, std::uint32_t green) {
    using namespace DrawSubmit::Isa;
    Program built;
    built << Vop1(VMovB32, 0, red)
          << Vop1(VMovB32, 1, green)
          << Vop1(VMovB32, 2, InlineInteger(0))
          << Vop1(VMovB32, 3, InlineOne)
          << Export(ColorTarget, 0, true)
          << EndProgram;
    return built;
}

const Program& RedPixel() {
    static const Program program = FlatPixel(DrawSubmit::Isa::InlineOne, DrawSubmit::Isa::InlineInteger(0));
    return program;
}

const Program& GreenPixel() {
    static const Program program = FlatPixel(DrawSubmit::Isa::InlineInteger(0), DrawSubmit::Isa::InlineOne);
    return program;
}

const Program& SpinProgram() {
    static const Program program = [] {
        using namespace NewPath::Isa;
        constexpr std::uint32_t VLshrRevB32 = 0x16u;
        constexpr std::uint32_t VXorB32 = 0x1du;
        constexpr std::uint32_t MoveS4ToS9 = 0xbe890304u;
        constexpr std::uint32_t DecrementS9 = 0x80898109u;
        constexpr std::uint32_t CompareS9NotZero = 0xbf078009u;
        constexpr std::uint32_t BranchBackWhileNotZero = 0xbf85fff7u;
        Program built;
        built << Vop2(VAddNcU32, 1, InlineInteger(1), 0) << MoveS4ToS9
              << Vop2(VLshlRevB32, 2, InlineInteger(13), 1) << Vop2(VXorB32, 1, Vgpr(1), 2)
              << Vop2(VLshrRevB32, 2, InlineInteger(17), 1) << Vop2(VXorB32, 1, Vgpr(1), 2)
              << Vop2(VLshlRevB32, 2, InlineInteger(5), 1) << Vop2(VXorB32, 1, Vgpr(1), 2)
              << DecrementS9 << CompareS9NotZero << BranchBackWhileNotZero
              << Vop2(VLshlRevB32, 3, InlineInteger(2), 0) << Mubuf(BufferStoreDword, 1, 3, 0) << EndProgram;
        return built;
    }();
    return program;
}

const Program& FillKeysProgram() {
    static const Program program = [] {
        using namespace NewPath::Isa;
        Program built;
        built << Vop2(VLshlRevB32, 1, InlineInteger(2), 0)
              << Vop1(VMovB32, 2, Literal) << KeysClear1111
              << Mubuf(BufferStoreDword, 2, 1, 0)
              << EndProgram;
        return built;
    }();
    return program;
}

const Program& CopyKeysProgram() {
    static const Program program = [] {
        using namespace NewPath::Isa;
        Program built;
        built << Vop2(VLshlRevB32, 1, InlineInteger(2), 0)
              << Mubuf(BufferLoadDword, 2, 1, 0)
              << WaitAll
              << Mubuf(BufferStoreDword, 2, 1, 4)
              << EndProgram;
        return built;
    }();
    return program;
}

const Program& SampleTargetProgram() {
    static const Program program = [] {
        using namespace DeviceTable::Asm;
        constexpr std::uint32_t VLshrRevB32 = 0x16u;
        constexpr std::uint32_t VOrB32 = 0x1cu;
        constexpr std::uint32_t SLoadDwordX8 = 0x03u;
        Program built;
        built << Smem(SLoadDwordX4, 28, 0, DeviceTable::OutputSlot * 4u)
              << Smem(SLoadDwordX4, 32, 0, DeviceTable::SamplerSlot * 4u)
              << Smem(SLoadDwordX8, 36, 0, DeviceTable::ImageSlot * 4u)
              << WaitAll
              << Vop1(VMovB32, 3, 2)
              << Vop2(VAndB32, 4, InlineInteger(1), 3)
              << Vop2(VLshlRevB32, 4, InlineInteger(6), 4)
              << Vop2(VOrB32, 1, Vgpr(4), 0)
              << Vop2(VLshrRevB32, 2, InlineInteger(1), 3)
              << Vop2(VLshlRevB32, 5, InlineInteger(7), 2)
              << Vop2(VOrB32, 5, Vgpr(1), 5)
              << Vop2(VLshlRevB32, 5, InlineInteger(4), 5)
              << Vop1(VCvtF32U32, 1, Vgpr(1))
              << Vop2(VAddF32, 1, Half, 1)
              << Vop2(VMulF32, 1, Literal, 1) << OneOverSide
              << Vop1(VCvtF32U32, 2, Vgpr(2))
              << Vop2(VAddF32, 2, Half, 2)
              << Vop2(VMulF32, 2, Literal, 2) << OneOverSide
              << Mimg(ImageSampleLz, 0xfu, false, 6, 1, 36, 32)
              << WaitAll
              << Mubuf(BufferStoreDwordX4, 6, 5, 28)
              << EndProgram;
        return built;
    }();
    return program;
}

struct Memory {
    PrivatePages target{1};
    PrivatePages keys{1};
    PrivatePages vertices{1};
    PrivatePages scratch{1};
    PrivatePages sampled{static_cast<std::uint32_t>(Texels * 16u / PageBytes)};
    PrivatePages sourceKeys{1};

    Memory() { std::memcpy(vertices.Data(), FullScreen.data(), sizeof(FullScreen)); }

    void Reset() {
        for (std::uint32_t word = 0; word < Texels; ++word) target.Words()[word] = Garbage;
        for (std::uint32_t word = 0; word < KeyLanes; ++word) keys.Words()[word] = KeysUncompressed;
        for (std::uint32_t word = 0; word < KeyLanes; ++word) sourceKeys.Words()[word] = KeysClear0000;
        scratch.Words()[SpinLabelWord] = 0u;
        scratch.Words()[DrawLabelWord] = 0u;
        AgcDriver::GuestMemory::CollectWritesUncached(target.Address(), target.Bytes());
        AgcDriver::GuestMemory::CollectWritesUncached(keys.Address(), keys.Bytes());
        AgcDriver::GuestMemory::BumpCollectEpoch();
    }
};

void Draw(Commands& commands, const Memory& memory, const Program& pixel, const Rect& rect) {
    DrawSubmit::Baseline(commands, {memory.target.Data(), Side, Side}, DrawSubmit::CenterAndPosition);
    DrawSubmit::Context(commands, 0x31c, 0x28028u | DccEnable);
    DrawSubmit::Context(commands, 0x3b8, RenderTargetAttrib3);
    DrawSubmit::Context(commands, 0x31b, 0u);
    DrawSubmit::Context(commands, 0x325, Low(memory.keys.Address() >> 8u));
    DrawSubmit::Context(commands, 0x3a8, Low(memory.keys.Address() >> 40u));
    DrawSubmit::Context(commands, 0x90, 0x80000000u | (rect.top << 16u) | rect.left);
    DrawSubmit::Context(commands, 0x91, (rect.bottom << 16u) | rect.right);
    DrawSubmit::Programs(commands, BoundedVertex(), VertexUserWords, pixel, 0u);
    DrawSubmit::Instances(commands, 1u);
    const auto corners = memory.vertices.Address();
    const std::array<std::uint32_t, VertexUserWords> vertexUserData{Low(corners), (High(corners) & 0xffffu) | (16u << 16u), static_cast<std::uint32_t>(FullScreen.size()), 0x01016facu};
    DrawSubmit::VertexUserData(commands, vertexUserData);
    DrawSubmit::DrawAuto(commands, static_cast<std::uint32_t>(FullScreen.size()));
}

void Spin(Commands& commands, const Memory& memory, std::uint32_t rounds) {
    const auto buffer = NewPath::BufferResource(memory.scratch.Address(), SpinLanes * 4u);
    const std::array<std::uint32_t, 5> userData{buffer[0], buffer[1], buffer[2], buffer[3], rounds};
    commands.Dispatch(SpinProgram(), userData, SpinLanes);
}

std::array<std::uint32_t, 8> TargetTexture(const Memory& memory) {
    const auto address = memory.target.Address();
    const auto meta = memory.keys.Address() >> 8u;
    return {
        Low(address >> 8u),
        (Low(address >> 40u) & 0xffu) | (Format8888Unorm << 20u) | (((Side - 1u) & 3u) << 30u),
        ((Side - 1u) >> 2u) | ((Side - 1u) << 14u),
        0xfacu | (TileRenderTarget64KB << 20u) | (DeviceTable::Type2D << 28u),
        0u,
        0u,
        (1u << 20u) | (1u << 21u) | (static_cast<std::uint32_t>(meta & 0xffu) << 24u),
        static_cast<std::uint32_t>(meta >> 8u),
    };
}

std::string Submitted(Commands& commands) {
    std::string failure;
    NewPath::StderrCapture capture;
    try {
        commands.Submit(0);
        NewPath::WaitIdle();
    } catch (const std::exception& error) {
        failure = error.what();
    }
    const auto log = capture.Finish();
    if (log.find("failed") != std::string::npos) failure += "\n" + log;
    return failure;
}

std::vector<std::uint32_t> Sample(Memory& memory, const std::string& what) {
    std::fill(SampleSrt.begin(), SampleSrt.end(), 0u);
    const auto output = DeviceTable::Buffer(memory.sampled.Data(), Texels * 16u);
    std::copy(output.begin(), output.end(), SampleSrt.begin() + DeviceTable::OutputSlot);
    std::copy(PointSampler.begin(), PointSampler.end(), SampleSrt.begin() + DeviceTable::SamplerSlot);
    const auto image = TargetTexture(memory);
    std::copy(image.begin(), image.end(), SampleSrt.begin() + DeviceTable::ImageSlot);
    std::memset(memory.sampled.Data(), 0xee, Texels * 16u);
    const auto srt = DrawSubmit::AddressOf(SampleSrt.data());
    const std::array<std::uint32_t, 2> userData{Low(srt), High(srt)};
    Commands commands;
    commands.Dispatch(SampleTargetProgram(), userData, 64u, Texels / 64u);
    const auto failure = Submitted(commands);
    Expect(failure.empty(), what + ": the sampling dispatch failed: " + failure);
    AgcDriver::GuestMemory::FlushGpuWrites(memory.sampled.Address(), Texels * 16u);
    std::vector<std::uint32_t> words(Texels * 4u);
    std::memcpy(words.data(), memory.sampled.Data(), words.size() * 4u);
    return words;
}

std::uint32_t Calibrate(Memory& memory) {
    for (std::uint64_t measured = CalibrationRounds;; measured *= 4u) {
        Commands commands;
        Spin(commands, memory, static_cast<std::uint32_t>(measured));
        const auto start = Clock::now();
        commands.Submit(0);
        NewPath::WaitIdle();
        NewPath::Settle();
        const auto took = std::max<long long>(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count(), 1);
        if (took < CalibrationTime.count() && measured < MaxRounds / 4u) continue;
        const auto rounds = measured * static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(Busy).count()) / static_cast<std::uint64_t>(took);
        std::printf("the GPU spins %llu rounds in %lld us\n", static_cast<unsigned long long>(measured), took);
        return static_cast<std::uint32_t>(std::clamp<std::uint64_t>(rounds, CalibrationRounds, MaxRounds));
    }
}

bool Inside(const Rect& rect, std::uint32_t x, std::uint32_t y) {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

std::array<std::uint32_t, 4> Channels(std::uint32_t color) {
    std::array<std::uint32_t, 4> channels{};
    for (std::uint32_t channel = 0; channel < 4u; ++channel) channels[channel] = std::bit_cast<std::uint32_t>(static_cast<float>((color >> (8u * channel)) & 0xffu) / 255.0f);
    return channels;
}

void ExpectSampled(const std::vector<std::uint32_t>& words, std::uint32_t inside, std::uint32_t outside, const std::string& what) {
    const auto in = Channels(inside);
    const auto out = Channels(outside);
    std::size_t wrong = 0;
    std::string first;
    for (std::uint32_t y = 0; y < Side; ++y) {
        for (std::uint32_t x = 0; x < Side; ++x) {
            const auto& expected = Inside(Drawn, x, y) ? in : out;
            const auto* texel = words.data() + (y * Side + x) * 4u;
            if (std::equal(expected.begin(), expected.end(), texel)) continue;
            if (wrong++ == 0u) first = "texel (" + std::to_string(x) + ", " + std::to_string(y) + ") sampled " + Hex(texel[0]) + " " + Hex(texel[1]) + " " + Hex(texel[2]) + " " + Hex(texel[3]);
        }
    }
    Expect(wrong == 0u, what + ": " + std::to_string(wrong) + " sampled texels are not " + Hex(inside) + " inside the drawn rectangle and " + Hex(outside) + " outside it, first " + first);
}

void ExpectStored(Memory& memory, std::uint32_t inside, std::uint32_t outside, const std::string& what) {
    std::vector<std::uint32_t> texels(Texels);
    AgcDriver::GuestMemory::Read(memory.target.Address(), std::as_writable_bytes(std::span(texels)));
    std::vector<std::uint32_t> keys(KeyLanes);
    AgcDriver::GuestMemory::Read(memory.keys.Address(), std::as_writable_bytes(std::span(keys)));
    const auto drawn = static_cast<std::size_t>(std::count(texels.begin(), texels.end(), inside));
    const auto kept = static_cast<std::size_t>(std::count(texels.begin(), texels.end(), outside));
    const auto area = std::size_t{Drawn.right - Drawn.left} * (Drawn.bottom - Drawn.top);
    Expect(drawn == area && kept == Texels - area, what + ": the target's guest memory holds " + std::to_string(drawn) + " texels " + Hex(inside) + " and " + std::to_string(kept) + " texels " + Hex(outside) + ", expected " + std::to_string(area) + " and " + std::to_string(Texels - area));
    Expect(std::ranges::all_of(keys, [](std::uint32_t word) { return word == KeysUncompressed; }), what + ": the target's DCC keys are not uncompressed after the draw");
}

enum class Writer { DmaFill, Draw, FillKernel, UnknownKernel };

struct KeyCase {
    const char* name;
    Writer writer;
    std::uint32_t outside;
    bool mustNotWait;
};

void WriteKeys(Commands& commands, const Memory& memory, Writer writer) {
    switch (writer) {
        case Writer::DmaFill:
            commands.Append(0x50u, {0x40000000u, KeysClear0000, 0u, Low(memory.keys.Address()), High(memory.keys.Address()), KeyBytes});
            break;
        case Writer::Draw:
            Draw(commands, memory, GreenPixel(), Whole);
            break;
        case Writer::FillKernel:
            commands.Dispatch(FillKeysProgram(), NewPath::BufferResource(memory.keys.Address(), KeyBytes), KeyLanes);
            break;
        case Writer::UnknownKernel: {
            const auto from = NewPath::BufferResource(memory.sourceKeys.Address(), KeyBytes);
            const auto to = NewPath::BufferResource(memory.keys.Address(), KeyBytes);
            const std::array<std::uint32_t, 8> userData{from[0], from[1], from[2], from[3], to[0], to[1], to[2], to[3]};
            commands.Dispatch(CopyKeysProgram(), userData, KeyLanes);
            break;
        }
    }
}

bool LabelPending(std::uint64_t address) {
    const auto* recorder = AgcDriver::Graphics::Recorder::Active();
    return recorder != nullptr && recorder->LabelPendingAt(address);
}

void Run(Memory& memory, std::uint32_t rounds, const KeyCase& keyCase) {
    NewPath::Settle();
    memory.Reset();
    const auto spinLabel = memory.scratch.Address(SpinLabelWord * 4u);
    const auto drawLabel = memory.scratch.Address(DrawLabelWord * 4u);
    Commands commands;
    Spin(commands, memory, rounds);
    commands.WriteData(spinLabel, {1u});
    WriteKeys(commands, memory, keyCase.writer);
    Draw(commands, memory, RedPixel(), Drawn);
    commands.WriteData(drawLabel, {1u});
    std::string failure;
    bool seen = false;
    bool spunFirst = false;
    {
        NewPath::StderrCapture capture;
        try {
            commands.Submit(0);
            for (const auto deadline = Clock::now() + std::chrono::seconds(10); !seen && Clock::now() < deadline;) {
                if (LabelPending(drawLabel) || memory.scratch.Words()[DrawLabelWord] == 1u) {
                    seen = true;
                    spunFirst = memory.scratch.Words()[SpinLabelWord] == 1u;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            NewPath::WaitIdle();
        } catch (const std::exception& error) {
            failure = error.what();
        }
        const auto log = capture.Finish();
        if (log.find("failed") != std::string::npos) failure += "\n" + log;
    }
    const std::string what = std::string("a draw into a sub-rectangle of a DCC color target after ") + keyCase.name;
    Expect(failure.empty(), what + ": the submission failed: " + failure);
    Expect(seen, what + ": the draw's label was never recorded");
    if (keyCase.mustNotWait) Expect(!spunFirst, what + ": the draw was recorded only after the GPU had finished the work submitted before the key writer, so its recording waited for the GPU");
    else std::printf("%s: the draw was recorded %s the GPU finished the work before the key writer\n", what.c_str(), spunFirst ? "after" : "before");
    ExpectStored(memory, Red, keyCase.outside, what + ", read by the CPU");
    ExpectSampled(Sample(memory, what), Red, keyCase.outside, what + ", sampled");
}

void DrawsAfterKeyWriters() {
    DrawSubmit::Register(VertexHeader, BoundedVertex(), DrawSubmit::VertexShaderType);
    DrawSubmit::Register(RedHeader, RedPixel(), DrawSubmit::PixelShaderType);
    DrawSubmit::Register(GreenHeader, GreenPixel(), DrawSubmit::PixelShaderType);
    Memory memory;
    memory.Reset();
    {
        Commands warm;
        Draw(warm, memory, GreenPixel(), Whole);
        Draw(warm, memory, RedPixel(), Drawn);
        Expect(Submitted(warm).empty(), "DCC draws: the warm-up draws failed");
        static_cast<void>(Sample(memory, "DCC draws: the warm-up sample"));
        for (const auto writer : {Writer::DmaFill, Writer::Draw, Writer::FillKernel, Writer::UnknownKernel}) {
            memory.Reset();
            Spin(warm, memory, 1u);
            WriteKeys(warm, memory, writer);
            Expect(Submitted(warm).empty(), "DCC draws: a warm-up key writer failed");
        }
    }
    const auto rounds = Calibrate(memory);
    constexpr std::array<KeyCase, 4> cases{{
        {"a GPU fast clear of its keys to 0000 (a DMA fill)", Writer::DmaFill, Black, true},
        {"a full draw into it", Writer::Draw, Green, true},
        {"a kernel filling its keys with 1111", Writer::FillKernel, White, true},
        {"an unknown kernel copying 0000 keys into its keys", Writer::UnknownKernel, Black, false},
    }};
    for (const auto& keyCase : cases) Run(memory, rounds, keyCase);
}

}

#endif

int main() {
#ifdef _WIN32
    return DrawSubmit::Main("DCC draws without GPU waits", DrawsAfterKeyWriters);
#else
    std::printf("skipped, commit imports are Windows-only\n");
    return VulkanTestSkipped;
#endif
}
