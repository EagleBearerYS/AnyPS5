#include "DrawSubmitFixture.hpp"
#include "MemoryBufferFixture.hpp"

namespace {

using namespace MemoryBuffers;

constexpr std::uint32_t LoadDwordX4 = 0x0eu;
constexpr std::uint32_t Stride = 32;
constexpr std::uint32_t Records = Lanes * Height;
constexpr std::uint32_t SoffsetSgpr = 2;
constexpr std::uint32_t GroupSgpr = 3;
constexpr std::uint32_t InputBlockWords = 4096;
constexpr std::uint32_t VertexCount = 24;
constexpr std::uint32_t VertexUserWords = 8;
constexpr std::uint32_t VertexSoffsetSgpr = 10;
constexpr std::array<std::uint32_t, 2> Soffsets{8, 40};

std::uint32_t* InputBlock = nullptr;

constexpr std::array<std::uint32_t, 2> IndexedLoad(std::uint32_t vdata, std::uint32_t vaddr, std::uint32_t resource, std::uint32_t soffset) {
    return {0xe0000000u | (LoadDwordX4 << 18u) | 0x2000u, (soffset << 24u) | ((resource / 4u) << 16u) | (vdata << 8u) | vaddr};
}

std::uint32_t InputWord(std::uint32_t word) {
    return 0x6d000000u ^ (word * 0x9e3779b1u);
}

std::array<std::uint32_t, 4> Structured(const void* base) {
    const auto address = AddressOf(base);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (Stride << 16u), Records, 0x01016facu};
}

std::uint64_t ExpectedEnd(std::uint32_t soffset) {
    return AddressOf(InputBlock) + std::uint64_t{Records} * Stride + soffset + 16u;
}

const Program& LoadAtSoffset(std::uint32_t soffsetSgpr) {
    static std::array<Program, 2> programs;
    static std::array<bool, 2> built{};
    const auto slot = soffsetSgpr == SoffsetSgpr ? 0u : 1u;
    if (!built[slot]) {
        using namespace Ops;
        auto& program = programs[slot];
        LoadSrt(program, {{28, OutputSlot}, {32, InputSlot}});
        program << Vop1(VMovB32, 1, GroupSgpr)
                << Vop2(VLshlRevB32, 1, InlineInteger(6), 1)
                << Vop2(VAddNcU32, 1, Vgpr(0), 1)
                << IndexedLoad(4, 1, 32, soffsetSgpr);
        StoreLane(program, GroupSgpr, 28);
        built[slot] = true;
    }
    return programs[slot];
}

std::array<std::uint32_t, 3> ComputeUserData(std::uint32_t soffset) {
    const auto srt = SrtUserData();
    Place(InputSlot, Structured(InputBlock));
    return {srt[0], srt[1], soffset};
}

void ComputeAtUserDataSoffset(AgcDriver::VulkanDevice& device) {
    for (const auto soffset : Soffsets) {
        const auto what = "compute load at a user-data soffset of " + std::to_string(soffset);
        const auto compiled = Compile(device, LoadAtSoffset(SoffsetSgpr), ComputeUserData(soffset));
        const auto& result = *compiled.result;
        Expect(!result.unboundedReads, what + ": the reads are unbounded (" + Reads(result) + ")");
        Expect(Contains(result.readRanges, AddressOf(InputBlock), ExpectedEnd(soffset)), what + ": the " + Reads(result) + " do not hold " + Span(AddressOf(InputBlock), ExpectedEnd(soffset)) + ", the V#'s records times its stride plus the soffset and 16 bytes");
        const auto outcome = Run(device, compiled, Height);
        ExpectClean(outcome, what);
        ExpectRows(outcome.words, Height, [soffset](std::uint32_t row, std::uint32_t lane, std::uint32_t component) { return InputWord(((row * Lanes + lane) * Stride + soffset) / 4u + component); }, what + ": the loaded words");
    }
}

void ComputeAtGroupSoffset(AgcDriver::VulkanDevice& device) {
    const auto what = std::string("compute load at a workgroup-id soffset");
    const auto compiled = Compile(device, LoadAtSoffset(GroupSgpr), ComputeUserData(0u));
    Expect(compiled.result->unboundedReads, what + ": the reads are bounded although the pre-pass cannot know the soffset (" + Reads(*compiled.result) + ")");
    const auto outcome = Run(device, compiled, 1);
    ExpectClean(outcome, what);
    ExpectRows(outcome.words, 1, [](std::uint32_t, std::uint32_t lane, std::uint32_t component) { return InputWord(lane * Stride / 4u + component); }, what + ": the loaded words of workgroup 0");
}

std::uint32_t* VertexTable = nullptr;
TestGuest::Array<std::uint32_t, VertexCount * 4> VertexOutput;
TestGuest::Array<std::uint32_t, 64 * 4> Pixels;
DrawSubmit::Header VertexHeader;
DrawSubmit::Header PixelHeader;

const Program& VertexAtSoffset() {
    static const Program program = [] {
        using namespace DrawSubmit::Isa;
        Program built;
        built << Smem(SLoadDwordX4, 20, 8, 0)
              << WaitAll
              << IndexedLoad(10, 5, 20, VertexSoffsetSgpr)
              << Vop2(VLshlRevB32, 21, InlineInteger(4), 5)
              << WaitAll
              << Mubuf(DeviceTable::Asm::BufferStoreDwordX4, 10, 21, 12)
              << Vop1(VMovB32, 22, InlineInteger(0))
              << Vop1(VMovB32, 23, InlineInteger(0))
              << Vop1(VMovB32, 24, InlineHalf)
              << Vop1(VMovB32, 25, InlineOne)
              << Export(PositionTarget, 22, false)
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

std::array<std::uint32_t, VertexUserWords> VertexUserData(std::uint32_t soffset) {
    const auto table = AddressOf(VertexTable);
    const auto output = Buffer(VertexOutput.data(), static_cast<std::uint32_t>(decltype(VertexOutput)::ByteSize));
    return {static_cast<std::uint32_t>(table), static_cast<std::uint32_t>(table >> 32u), soffset, 0u, output[0], output[1], output[2], output[3]};
}

void VertexRanges(const ShaderRecompiler::SpirvTarget& target) {
    const auto structured = Structured(InputBlock);
    std::copy(structured.begin(), structured.end(), VertexTable);
    for (const auto soffset : Soffsets) {
        const auto what = "vertex fetch at a user-data soffset of " + std::to_string(soffset);
        const auto userData = VertexUserData(soffset);
        const ShaderRecompiler::RecompileRequest request{
            {ShaderStage::Vertex, VertexAtSoffset().Address(), VertexAtSoffset().Code(), 0, {}},
            {64, DrawSubmit::FirstVertexUserSgpr, userData, std::nullopt, std::nullopt, ShaderRecompiler::ShaderVertexStageInfo{}, {}},
            target,
            {0, 0, 0, 64}
        };
        const auto compiled = CompileRequest(request);
        Expect(!compiled.result->unboundedReads, what + ": the vertex stage's reads are unbounded (" + Reads(*compiled.result) + ")");
        Expect(Contains(compiled.result->readRanges, AddressOf(InputBlock), ExpectedEnd(soffset)), what + ": the vertex stage's " + Reads(*compiled.result) + " do not hold " + Span(AddressOf(InputBlock), ExpectedEnd(soffset)));
    }
}

void VertexDraws() {
    DrawSubmit::Register(VertexHeader, VertexAtSoffset(), DrawSubmit::VertexShaderType);
    DrawSubmit::Register(PixelHeader, FlatPixel(), DrawSubmit::PixelShaderType);
    for (const auto soffset : Soffsets) {
        const auto what = "vertex fetch at a user-data soffset of " + std::to_string(soffset);
        VertexOutput.fill(Untouched);
        DrawSubmit::Commands commands;
        DrawSubmit::Baseline(commands, {Pixels.data(), 64u, 4u}, DrawSubmit::CenterAndPosition);
        DrawSubmit::Programs(commands, VertexAtSoffset(), VertexUserWords, FlatPixel(), 0u);
        DrawSubmit::Instances(commands, 1u);
        DrawSubmit::VertexUserData(commands, VertexUserData(soffset));
        DrawSubmit::DrawAuto(commands, VertexCount);
        commands.Submit(0);
        NewPath::WaitIdle();
        const auto words = ReadBack(VertexOutput.data(), VertexOutput.size());
        for (std::uint32_t vertex = 0; vertex < VertexCount; ++vertex) {
            for (std::uint32_t component = 0; component < 4u; ++component) {
                const auto expected = InputWord((vertex * Stride + soffset) / 4u + component);
                const auto actual = words[vertex * 4u + component];
                if (actual == expected) continue;
                Expect(false, what + ": vertex " + std::to_string(vertex) + " component " + std::to_string(component) + " fetched " + Hex(actual) + ", expected " + Hex(expected));
                vertex = VertexCount;
                break;
            }
        }
    }
}

}

int main() {
    try {
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
            InputBlock = OwnBlock();
            VertexTable = OwnBlock();
            for (std::uint32_t word = 0; word < InputBlockWords; ++word) InputBlock[word] = InputWord(word);
            Srt.fill(0u);
            ComputeAtUserDataSoffset(*device);
            ComputeAtGroupSoffset(*device);
            VertexRanges(device->Target());
        }
        VertexDraws();
    } catch (const std::exception& error) {
        Expect(false, std::string("exception: ") + error.what());
    }
    try {
        AgcDriverShutdown_nid_postfix();
    } catch (const std::exception& error) {
        Expect(false, std::string("shutdown: ") + error.what());
    }
    return Finish("register soffset ranges");
}
