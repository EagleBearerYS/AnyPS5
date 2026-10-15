#include "DeviceTableFixture.hpp"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <thread>

namespace {

using namespace DeviceTable;
using NewPath::Commands;
using NewPath::High;
using NewPath::Low;
using AgcDriver::Graphics::Require;

enum Texture : std::uint32_t { First, Second, Stored, Textures };

constexpr std::uint32_t TextureWords = Width * Height;
constexpr std::uint32_t MaxDispatches = 32;
constexpr std::uint32_t IndexSlot = 20;
constexpr std::uint32_t TableSlot = 24;
constexpr std::uint32_t FirstSeed = 0x1100u;
constexpr std::uint32_t SecondSeed = 0x2200u;
constexpr std::size_t BlockBytes = 65536;
constexpr std::uint64_t ScopedRefreshes = 2;

namespace Ops {

using namespace DeviceTable::Asm;

constexpr std::uint32_t SLoadDwordX2 = 0x01u;

constexpr std::array<std::uint32_t, 2> SmemRegisterOffset(std::uint32_t opcode, std::uint32_t sdata, std::uint32_t sbase, std::uint32_t soffset, std::uint32_t offset) {
    return {0xf4000000u | (opcode << 18u) | (sdata << 6u) | (sbase / 2u), (soffset << 25u) | (offset & 0x1fffffu)};
}

}

template <typename T>
T* Block() {
    return reinterpret_cast<T*>(TestGuest::Allocate(BlockBytes, BlockBytes));
}

const std::array<float*, Textures> Texels{Block<float>(), Block<float>(), Block<float>()};
std::uint32_t* const Indices = Block<std::uint32_t>();
std::uint32_t* const Table = Block<std::uint32_t>();
TestGuest::Array<std::uint32_t, MaxDispatches * SrtWords> Srts;
TestGuest::Array<std::uint32_t, MaxDispatches * OutputWords> Results;
std::array<std::array<float, TextureWords>, Textures> Model;
std::uint32_t next = 0;

const char* const Names[Textures] = {"T1", "T2", "T3"};

const Program& SampleProgram() {
    static const Program program = [] {
        using namespace Ops;
        Program built;
        LoadSrt(built, {{28, OutputSlot}, {32, SamplerSlot}, {36, ImageSlot}});
        TexelCenter(built, 2);
        built << Mimg(ImageSampleLz, 1u, true, 4, 1, 36, 32);
        StoreLane(built, 2, 28);
        return built;
    }();
    return program;
}

const Program& SelectedProgram() {
    static const Program program = [] {
        using namespace Ops;
        Program built;
        built << Smem(SLoadDwordX4, 28, 0, OutputSlot * 4u)
              << Smem(SLoadDwordX4, 32, 0, SamplerSlot * 4u)
              << Smem(SLoadDwordX4, 40, 0, IndexSlot * 4u)
              << Smem(SLoadDwordX2, 44, 0, TableSlot * 4u)
              << WaitAll
              << Vop2(VLshlRevB32, 10, InlineInteger(2), 0)
              << Mubuf(BufferLoadDword, 11, 10, 40)
              << WaitAll
              << Vop1(VReadFirstLaneB32, 46, Vgpr(11))
              << SmemRegisterOffset(SLoadDwordX4, 36, 44, 46, 0)
              << WaitAll;
        TexelCenter(built, 2);
        built << Mimg(ImageSampleLz, 1u, true, 4, 1, 36, 32);
        StoreLane(built, 2, 28);
        return built;
    }();
    return program;
}

const Program& StoreProgram() {
    static const Program program = [] {
        using namespace Ops;
        Program built;
        built << Vop2(VAndB32, 1, InlineInteger(63), 0)
              << Vop1(VMovB32, 2, 9)
              << Vop2(VLshlRevB32, 3, InlineInteger(8), 2)
              << Vop2(VAddNcU32, 3, Vgpr(1), 3)
              << Vop2(VAddNcU32, 3, 8, 3)
              << Vop1(VCvtF32U32, 4, Vgpr(3))
              << Mimg(ImageStore, 1u, false, 4, 1, 0, 0, true)
              << EndProgram;
        return built;
    }();
    return program;
}

float Value(std::uint32_t texture, std::uint32_t generation, std::uint32_t texel) {
    return static_cast<float>((texture + 1u) * 10000u + generation * 1000u + texel) + 0.25f;
}

void Rewrite(std::uint32_t texture, std::uint32_t generation) {
    for (std::uint32_t texel = 0; texel < TextureWords; ++texel) {
        Texels[texture][texel] = Value(texture, generation, texel);
        Model[texture][texel] = Value(texture, generation, texel);
    }
}

void Fill() {
    for (std::uint32_t texture = 0; texture < Textures; ++texture) Rewrite(texture, 0);
    std::fill(Indices, Indices + 64, 0u);
    std::fill(Table, Table + 64, 0u);
    const auto first = Image(Texels[First], Format32Float);
    std::copy(first.begin(), first.end(), Table);
    Srts.fill(0u);
    Results.fill(Untouched);
}

std::array<std::uint32_t, 2> PrepareSrt(std::uint32_t dispatch, std::uint32_t texture) {
    auto* srt = Srts.data() + dispatch * SrtWords;
    std::fill(srt, srt + SrtWords, 0u);
    const auto output = Buffer(Results.data() + dispatch * OutputWords, OutputWords * 4u);
    std::copy(output.begin(), output.end(), srt + OutputSlot);
    std::copy(PointClamp.begin(), PointClamp.end(), srt + SamplerSlot);
    const auto image = Image(Texels[texture], Format32Float);
    std::copy(image.begin(), image.end(), srt + ImageSlot);
    const auto indices = Buffer(Indices, 256u);
    std::copy(indices.begin(), indices.end(), srt + IndexSlot);
    const auto table = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(Table));
    srt[TableSlot] = Low(table);
    srt[TableSlot + 1u] = High(table);
    std::fill(Results.begin() + dispatch * OutputWords, Results.begin() + (dispatch + 1u) * OutputWords, Untouched);
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(srt));
    return {Low(address), High(address)};
}

std::uint32_t Sample(const Program& program, std::uint32_t texture) {
    Require(next < MaxDispatches, "device image scoped refresh: more dispatches than result slots");
    const auto dispatch = next++;
    const auto userData = PrepareSrt(dispatch, texture);
    Commands commands;
    commands.Dispatch(program, userData, Lanes, Height);
    commands.Submit(0);
    NewPath::WaitIdle();
    return dispatch;
}

void Store(std::uint32_t texture, std::uint32_t seed) {
    std::array<std::uint32_t, 9> userData{};
    const auto image = Image(Texels[texture], Format32Float);
    std::copy(image.begin(), image.end(), userData.begin());
    userData[8] = seed;
    Commands commands;
    commands.Dispatch(StoreProgram(), userData, Lanes, Height);
    commands.Submit(0);
    NewPath::WaitIdle();
    for (std::uint32_t y = 0; y < Height; ++y) {
        for (std::uint32_t x = 0; x < Width; ++x) Model[texture][y * Width + x] = static_cast<float>(seed + x + 256u * y);
    }
}

Read Classify(std::uint32_t dispatch, std::uint32_t texture, std::string& mismatch) {
    const auto* words = Results.data() + dispatch * OutputWords;
    AgcDriver::GuestMemory::FlushGpuWrites(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(words)), OutputWords * 4u);
    bool all = true;
    bool zeros = true;
    for (std::uint32_t row = 0; row < Height; ++row) {
        for (std::uint32_t x = 0; x < Lanes; ++x) {
            const auto word = words[(row * Lanes + x) * LaneWords];
            zeros = zeros && word == 0u;
            if (SameFloat(word, Model[texture][row * Width + x])) continue;
            if (all) mismatch = "row " + std::to_string(row) + " lane " + std::to_string(x) + " is " + Hex(word) + ", expected " + Hex(std::bit_cast<std::uint32_t>(Model[texture][row * Width + x]));
            all = false;
        }
    }
    return all ? Read::Expected : zeros ? Read::Zeros : Read::Other;
}

void ExpectTexels(std::uint32_t dispatch, std::uint32_t texture, const std::string& what) {
    std::string mismatch;
    const auto read = Classify(dispatch, texture, mismatch);
    Expect(read == Read::Expected, what + ": " + Names[texture] + " " + (read == Read::Zeros ? "read zeros" : mismatch));
}

void Premises() {
    const auto device = OpenVulkanTestDevice();
    if (!device) return;
    const auto named = Compile(*device, SampleProgram(), PrepareSrt(MaxDispatches - 1u, First));
    ExpectRouted(named, "device image scoped refresh: the sampling program");
    Expect(named.result->deviceImagesComplete && named.result->deviceImages.size() == 1u, "device image scoped refresh: the pre-pass of the sampling program did not name its one image (" + std::to_string(named.result->deviceImages.size()) + " named, complete " + std::to_string(named.result->deviceImagesComplete) + ")");
    const auto selected = Compile(*device, SelectedProgram(), PrepareSrt(MaxDispatches - 1u, First));
    ExpectRouted(selected, "device image scoped refresh: the program selecting its T# from memory");
    Expect(!selected.result->deviceImagesComplete, "device image scoped refresh: the pre-pass named every image of a program whose T# is selected through a value loaded per lane");
}

std::optional<std::uint64_t> ReportedRefreshes(const std::string& log) {
    const auto report = log.find("[device-images] (10 s):");
    if (report == std::string::npos) return std::nullopt;
    const auto end = log.find(" refreshed (", report);
    if (end == std::string::npos) return std::nullopt;
    auto begin = end;
    while (begin > report && log[begin - 1u] >= '0' && log[begin - 1u] <= '9') --begin;
    if (begin == end) return std::nullopt;
    return std::strtoull(log.substr(begin, end - begin).c_str(), nullptr, 10);
}

void Run() {
    Expect(std::getenv("APS5_PROFILE_DRAW") != nullptr, "device image scoped refresh: the test must run with APS5_PROFILE_DRAW set, which reports the device image table's refreshes");
    const auto warm = Sample(SampleProgram(), First);
    const auto window = std::chrono::steady_clock::now();
    ExpectTexels(warm, First, "the first complete dispatch naming T1");
    ExpectTexels(Sample(SampleProgram(), Second), Second, "the first complete dispatch naming T2");
    ExpectTexels(Sample(SampleProgram(), Stored), Stored, "the first complete dispatch naming T3");

    Rewrite(First, 1);
    ExpectTexels(Sample(SampleProgram(), Second), Second, "a complete dispatch naming only T2 after the CPU rewrote T1");
    Rewrite(First, 2);
    ExpectTexels(Sample(SampleProgram(), First), First, "the next complete dispatch naming T1, after the CPU rewrote it twice");

    Rewrite(Second, 1);
    ExpectTexels(Sample(SampleProgram(), First), First, "a complete dispatch naming only T1 after the CPU rewrote T2");
    Rewrite(Second, 2);
    ExpectTexels(Sample(SampleProgram(), Second), Second, "the next complete dispatch naming T2, after the CPU rewrote it twice");

    std::this_thread::sleep_until(window + std::chrono::milliseconds(10500));
    NewPath::StderrCapture capture;
    const auto reporting = Sample(SampleProgram(), Second);
    const auto log = capture.Finish();
    ExpectTexels(reporting, Second, "the complete dispatch that reports the device image table's counts");
    const auto refreshed = ReportedRefreshes(log);
    Expect(refreshed.has_value(), "device image scoped refresh: no [device-images] report with a refresh count after 10 s:\n" + log);
    if (refreshed) Expect(*refreshed == ScopedRefreshes, "device image scoped refresh: the table refreshed " + std::to_string(*refreshed) + " entries, expected " + std::to_string(ScopedRefreshes) + ": each texture the CPU rewrote twice while a dispatch named only the other is refreshed once, by the next dispatch that names it");

    Rewrite(First, 3);
    ExpectTexels(Sample(SelectedProgram(), First), First, "a dispatch with an unnamed T#, selected through a value loaded per lane, after the CPU rewrote T1");

    Store(Stored, FirstSeed);
    ExpectTexels(Sample(SampleProgram(), Second), Second, "a complete dispatch naming only T2 after a dispatch stored into T3");
    ExpectTexels(Sample(SampleProgram(), Stored), Stored, "the next complete dispatch naming T3, whose entry views the image a dispatch stored into");
    Store(Stored, SecondSeed);
    ExpectTexels(Sample(SampleProgram(), Stored), Stored, "a complete dispatch naming T3 after a second dispatch stored into it");
}

}

int main() {
    Fill();
    {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
    }
    try {
        Premises();
        Run();
    } catch (const std::exception& error) {
        Expect(false, std::string("exception: ") + error.what());
    }
    try {
        AgcDriverShutdown_nid_postfix();
    } catch (const std::exception& error) {
        Expect(false, std::string("shutdown: ") + error.what());
    }
    return Finish("device image scoped refresh");
}
