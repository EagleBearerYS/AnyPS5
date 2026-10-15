#include "DeviceTableFixture.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestTextureResource.hpp"
#include "prx/libSceAgcDriver/Graphics/include/TextureTiling.hpp"

namespace {

using namespace DeviceTable;

constexpr std::uint32_t Side = 2;
constexpr std::uint32_t Format1616UNorm = 23;
constexpr std::uint32_t LessEqual = 3;
constexpr std::uint32_t ClampWrap = 0;
constexpr std::uint32_t ClampEdge = 2;
constexpr std::uint32_t ClampBorder = 6;
constexpr std::uint32_t BorderBlack = 0;
constexpr std::uint32_t BorderWhite = 2;
constexpr std::uint32_t FilterPoint = 0;
constexpr std::uint32_t FilterBilinear = 1;
constexpr std::uint32_t ReductionMin = 1;
constexpr std::uint32_t RecordWords = 16;
constexpr std::uint32_t SamplerWord = 8;
constexpr std::uint32_t States = 8;
constexpr std::uint32_t Refused = States;
constexpr std::uint32_t InputSlot = 20;
constexpr std::uint32_t ResultSlot = 24;
constexpr std::uint32_t RootWords = 32;
constexpr std::array<std::uint8_t, 4> Red{0, 64, 128, 255};

struct Sampler {
    std::uint32_t clamp;
    std::uint32_t filter;
    std::uint32_t border = BorderBlack;
    std::uint32_t reduction = 0;
};

constexpr std::array<Sampler, States + 1> Samplers{{
    {ClampEdge, FilterPoint},
    {ClampWrap, FilterPoint},
    {ClampEdge, FilterBilinear},
    {ClampWrap, FilterBilinear},
    {ClampBorder, FilterPoint, BorderWhite},
    {ClampBorder, FilterPoint, BorderBlack},
    {ClampBorder, FilterBilinear, BorderWhite},
    {ClampBorder, FilterBilinear, BorderBlack},
    {ClampEdge, FilterBilinear, BorderBlack, ReductionMin},
}};

constexpr std::array<const char*, States> Names{
    "point, clamp to edge", "point, wrap", "bilinear, clamp to edge", "bilinear, wrap",
    "point, white border", "point, black border", "bilinear, white border", "bilinear, black border",
};

TestGuest::Array<float, Lanes * 3> Input;
TestGuest::Array<std::uint8_t, 4096> Texels;
TestGuest::Array<std::uint32_t, (States + 1) * RecordWords> Records;
TestGuest::Array<std::uint32_t, RootWords> Root;

namespace Ops {

using namespace DeviceTable::Asm;

constexpr std::uint32_t SLoadDwordX2 = 0x01u;
constexpr std::uint32_t SLoadDwordX8 = 0x03u;
constexpr std::uint32_t VMulU32U24 = 0x0bu;
constexpr std::uint32_t BufferLoadDwordX3 = 0x0fu;
constexpr std::uint32_t ImageSampleCLz = 0x2fu;
constexpr std::uint32_t RecordOffset = 0x8f108302u;

constexpr std::array<std::uint32_t, 2> SmemRegisterOffset(std::uint32_t opcode, std::uint32_t sdata, std::uint32_t sbase, std::uint32_t soffset, std::uint32_t offset) {
    return {0xf4000000u | (opcode << 18u) | (sdata << 6u) | (sbase / 2u), (soffset << 25u) | (offset & 0x1fffffu)};
}

}

const Program& CompareProgram() {
    static const Program program = [] {
        using namespace Ops;
        Program built;
        built << RecordOffset
              << SmemRegisterOffset(SLoadDwordX2, 20, 0, 16, 0)
              << Smem(SLoadDwordX4, 24, 0, InputSlot * 4u)
              << Smem(SLoadDwordX4, 28, 0, ResultSlot * 4u)
              << WaitAll
              << Smem(SLoadDwordX8, 36, 20, 0)
              << Smem(SLoadDwordX4, 32, 20, SamplerWord * 4u)
              << Vop2(VMulU32U24, 10, InlineInteger(12), 0)
              << Mubuf(BufferLoadDwordX3, 1, 10, 24)
              << WaitAll
              << Mimg(ImageSampleCLz, 1u, false, 4, 1, 36, 32)
              << Vop1(VMovB32, 9, 2)
              << Vop2(VLshlRevB32, 9, InlineInteger(6), 9)
              << Vop2(VAddNcU32, 9, Vgpr(0), 9)
              << Vop2(VLshlRevB32, 11, InlineInteger(2), 9)
              << WaitAll
              << Mubuf(BufferStoreDword, 4, 11, 28)
              << EndProgram;
        return built;
    }();
    return program;
}

std::array<std::uint32_t, 8> TextureDescriptor() {
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(Texels.data()));
    return {
        static_cast<std::uint32_t>(address >> 8u),
        static_cast<std::uint32_t>((address >> 40u) & 0xffu) | (Format1616UNorm << 20u) | (((Side - 1u) & 3u) << 30u),
        ((Side - 1u) >> 2u) | ((Side - 1u) << 14u),
        0xfacu | (Type2D << 28u),
        0u, 0u, 0u, 0u,
    };
}

std::array<std::uint32_t, 4> SamplerDescriptor(const Sampler& sampler) {
    return {
        sampler.clamp | (sampler.clamp << 3u) | (ClampEdge << 6u) | (LessEqual << 12u) | (sampler.reduction << 29u),
        0u,
        (sampler.filter << 20u) | (sampler.filter << 22u),
        sampler.border << 30u,
    };
}

void FillTexels() {
    Texels.fill(0);
    const auto descriptor = TextureDescriptor();
    const auto surface = AgcDriver::Graphics::DescribeSurface(AgcDriver::Graphics::DecodeTextureResource(descriptor));
    const auto& mip = surface.mips.at(0);
    for (std::uint32_t y = 0; y < Side; ++y) {
        for (std::uint32_t x = 0; x < Side; ++x) {
            auto* texel = Texels.data() + mip.tiledOffset + y * mip.pitchBytes + x * 4u;
            texel[0] = Red[y * Side + x];
            texel[1] = Red[y * Side + x];
            texel[2] = 0x22;
            texel[3] = 0xff;
        }
    }
}

void FillInput() {
    for (std::uint32_t tid = 0; tid < Lanes; ++tid) {
        Input[tid * 3u + 0u] = -0.6f + 0.035f * static_cast<float>(tid);
        Input[tid * 3u + 1u] = 1.55f - 0.0325f * static_cast<float>(tid);
        Input[tid * 3u + 2u] = -0.27f + 0.0237f * static_cast<float>((tid * 7u) % Lanes);
    }
    Input[0] = 0.0f; Input[1] = 0.0f; Input[2] = 0.0f;
    Input[3] = 0.999f; Input[4] = 0.999f; Input[5] = 1.2f;
    Input[6] = 0.6f; Input[7] = -0.1f; Input[8] = 0.5f;
    Input[9] = 0.3f; Input[10] = 0.5f; Input[11] = 1.1f;
    Input[12] = 0.9f; Input[13] = 1.05f; Input[14] = -0.05f;
}

void FillRecords() {
    Records.fill(0u);
    Root.fill(0u);
    const auto texture = TextureDescriptor();
    for (std::uint32_t record = 0; record < Samplers.size(); ++record) {
        auto* words = Records.data() + record * RecordWords;
        const auto sampler = SamplerDescriptor(Samplers[record]);
        std::copy(texture.begin(), texture.end(), words);
        std::copy(sampler.begin(), sampler.end(), words + SamplerWord);
        const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(words));
        Root[record * 2u] = static_cast<std::uint32_t>(address);
        Root[record * 2u + 1u] = static_cast<std::uint32_t>(address >> 32u);
    }
    const auto input = Buffer(Input.data(), static_cast<std::uint32_t>(decltype(Input)::ByteSize));
    const auto output = Buffer(Output.data(), static_cast<std::uint32_t>(decltype(Output)::ByteSize));
    std::copy(input.begin(), input.end(), Root.begin() + InputSlot);
    std::copy(output.begin(), output.end(), Root.begin() + ResultSlot);
}

float ReferenceTexel(int x, int y, float reference, const Sampler& sampler) {
    const int size = static_cast<int>(Side);
    if (sampler.clamp == ClampBorder && (x < 0 || x >= size || y < 0 || y >= size)) {
        const float border = sampler.border == BorderWhite ? 1.0f : 0.0f;
        return reference <= border ? 1.0f : 0.0f;
    }
    const auto address = [&](int value) {
        if (sampler.clamp == ClampEdge || sampler.clamp == ClampBorder) return std::clamp(value, 0, size - 1);
        return ((value % size) + size) % size;
    };
    const float red = static_cast<float>(Red[address(y) * Side + address(x)]) / 255.0f;
    return reference <= red ? 1.0f : 0.0f;
}

float Expected(std::uint32_t tid, const Sampler& sampler) {
    const float u = Input[tid * 3u + 1u] * static_cast<float>(Side);
    const float v = Input[tid * 3u + 2u] * static_cast<float>(Side);
    const float reference = std::clamp(Input[tid * 3u + 0u], 0.0f, 1.0f);
    if (sampler.filter == FilterPoint) return ReferenceTexel(static_cast<int>(std::floor(u)), static_cast<int>(std::floor(v)), reference, sampler);
    const float cu = u - 0.5f;
    const float cv = v - 0.5f;
    const int x = static_cast<int>(std::floor(cu));
    const int y = static_cast<int>(std::floor(cv));
    const float a = cu - std::floor(cu);
    const float b = cv - std::floor(cv);
    const float top = ReferenceTexel(x, y, reference, sampler) * (1.0f - a) + ReferenceTexel(x + 1, y, reference, sampler) * a;
    const float bottom = ReferenceTexel(x, y + 1, reference, sampler) * (1.0f - a) + ReferenceTexel(x + 1, y + 1, reference, sampler) * a;
    return top * (1.0f - b) + bottom * b;
}

float Result(const Outcome& outcome, std::uint32_t group, std::uint32_t tid) {
    return std::bit_cast<float>(outcome.words[group * Lanes + tid]);
}

void ExpectCompared(const Outcome& outcome, std::uint32_t groups, const std::string& what) {
    for (std::uint32_t group = 0; group < groups; ++group) {
        const auto& sampler = Samplers[group];
        const float tolerance = sampler.filter == FilterPoint ? 0.0f : 1e-4f;
        std::size_t wrong = 0;
        std::string first;
        for (std::uint32_t tid = 0; tid < Lanes; ++tid) {
            const float expected = Expected(tid, sampler);
            const float actual = Result(outcome, group, tid);
            if (std::fabs(actual - expected) <= tolerance) continue;
            if (wrong++ == 0u) first = "thread " + std::to_string(tid) + " compared to " + std::to_string(actual) + ", expected " + std::to_string(expected);
        }
        Expect(wrong == 0u, what + ", " + Names[group] + ": " + std::to_string(wrong) + " threads differ, first " + first);
    }
}

bool ZeroGroup(const Outcome& outcome, std::uint32_t group, bool untouched) {
    for (std::uint32_t tid = 0; tid < Lanes; ++tid) {
        const auto word = outcome.words[group * Lanes + tid];
        if (word != 0u && (!untouched || word != Untouched)) return false;
    }
    return true;
}

Outcome Dispatch(AgcDriver::VulkanDevice& device, const Compiled& compiled, std::uint32_t groups) {
    AgcDriver::GuestMemory::BumpCollectEpoch();
    return DeviceTable::Run(device, compiled, groups);
}

void Run(AgcDriver::VulkanDevice& device) {
    FillTexels();
    FillInput();
    FillRecords();
    const auto root = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(Root.data()));
    const std::array<std::uint32_t, 2> userData{static_cast<std::uint32_t>(root), static_cast<std::uint32_t>(root >> 32u)};
    const auto compiled = Compile(device, CompareProgram(), userData);
    ExpectRouted(compiled, "run-time color compare");
    ExpectNothingCaptured(compiled, "run-time color compare");

    const auto missed = Dispatch(device, compiled, States);
    ExpectClean(missed, "run-time color compare: first use");
    bool zeros = true;
    for (std::uint32_t group = 0; group < States; ++group) zeros = zeros && ZeroGroup(missed, group, false);
    Expect(zeros && missed.Misses() >= States && missed.Misses() <= States * Lanes, "run-time color compare: the first use of " + std::to_string(States) + " T# and S# pairs that the CPU pre-pass cannot evaluate did not read zeros and miss (" + std::to_string(missed.Misses()) + " misses)");
    const auto compared = Dispatch(device, compiled, States);
    ExpectClean(compared, "run-time color compare: second use");
    Expect(compared.Misses() == 0u && compared.after.inserted - missed.before.inserted == States && compared.after.distinct - missed.before.distinct == States, "run-time color compare: the drain did not insert one entry per S# state, or the second use missed again (" + std::to_string(compared.Misses()) + " misses, " + std::to_string(compared.after.inserted - missed.before.inserted) + " inserted)");
    ExpectCompared(compared, States, "run-time color compare: second use");

    const auto refusedFirst = Dispatch(device, compiled, States + 1u);
    ExpectClean(refusedFirst, "run-time color compare: first use of a min reduction S#");
    Expect(ZeroGroup(refusedFirst, Refused, false) && refusedFirst.Misses() >= 1u && refusedFirst.Misses() <= Lanes, "run-time color compare: the first use of a min reduction S# did not read zeros and miss (" + std::to_string(refusedFirst.Misses()) + " misses)");
    ExpectCompared(refusedFirst, States, "run-time color compare: beside the first use of a min reduction S#");
    const auto refused = Dispatch(device, compiled, States + 1u);
    const auto logs = refusedFirst.log + refused.log;
    Expect(refused.after.rejected - refusedFirst.before.rejected == 1u && logs.find("[device-images]") != std::string::npos && logs.find("color texture under comparison") != std::string::npos && logs.find("min or max reduction") != std::string::npos, "run-time color compare: the drain did not reject the min reduction S# once as a color compare with its reason:\n" + logs);
    Expect(refused.log.find("selected a descriptor the device image table rejected") != std::string::npos && refused.log.find("color texture under comparison") != std::string::npos, "run-time color compare: a use of the rejected min reduction S# did not fault with the color compare reason:\n" + refused.log);
    Expect(ZeroGroup(refused, Refused, true), "run-time color compare: a use of the rejected min reduction S# read texels");

    const auto again = Dispatch(device, compiled, States);
    ExpectClean(again, "run-time color compare: after the rejection");
    Expect(again.Misses() == 0u, "run-time color compare: the accepted states missed after the rejection");
    ExpectCompared(again, States, "run-time color compare: after the rejection");
}

}

int main() {
    return Main("run-time color compare", Run);
}
