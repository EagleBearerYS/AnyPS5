#include "DeviceTableFixture.hpp"

namespace {

using namespace DeviceTable;

constexpr std::uint32_t WarmSeed = 0x1234u;
constexpr std::uint32_t ColdSeed = 0x5678u;

constexpr std::size_t OwnBlockBytes = 65536;

float* OwnBlock() {
    return reinterpret_cast<float*>(TestGuest::Allocate(OwnBlockBytes, OwnBlockBytes));
}

float* const Warm = OwnBlock();
float* const Cold = OwnBlock();

const Program& SampleProgram() {
    static const Program program = [] {
        using namespace Asm;
        Program built;
        LoadSrt(built, {{28, OutputSlot}, {32, SamplerSlot}, {36, ImageSlot}});
        TexelCenter(built, 2);
        built << Mimg(ImageSampleLz, 1u, true, 4, 1, 36, 32);
        StoreLane(built, 2, 28);
        return built;
    }();
    return program;
}

const Program& StoreProgram() {
    static const Program program = [] {
        using namespace Asm;
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

float Stored(std::uint32_t seed, std::uint32_t x, std::uint32_t y) {
    return static_cast<float>(seed + x + 256u * y);
}

Expected StoredTexels(std::uint32_t seed) {
    return {Height, 0, 1, [seed](std::uint32_t row, std::uint32_t x, std::uint32_t, std::uint32_t word) { return SameFloat(word, Stored(seed, x, row)); }};
}

std::array<std::uint32_t, 9> StoreUserData(const void* texels, std::uint32_t seed) {
    std::array<std::uint32_t, 9> userData{};
    const auto image = Image(texels, Format32Float);
    std::copy(image.begin(), image.end(), userData.begin());
    userData[8] = seed;
    return userData;
}

Outcome StoreThenSample(AgcDriver::VulkanDevice& device, const void* texels, std::uint32_t seed, std::span<const std::uint32_t> sampleUserData, const std::string& what) {
    const auto storeUserData = StoreUserData(texels, seed);
    const auto store = Compile(device, StoreProgram(), storeUserData);
    const auto sample = Compile(device, SampleProgram(), sampleUserData);
    ExpectRouted(sample, what);
    ExpectNothingCaptured(sample, what);
    Batch batch(device);
    batch.Dispatch(store, Height);
    batch.Dispatch(sample, Height);
    return batch.Finish();
}

void Run(AgcDriver::VulkanDevice& device) {
    for (std::uint32_t texel = 0; texel < Width * Height; ++texel) {
        Warm[texel] = static_cast<float>(texel) + 0.5f;
        Cold[texel] = -static_cast<float>(texel) - 0.25f;
    }
    Srt.fill(0u);
    const auto userData = SrtUserData();
    Place(SamplerSlot, PointClamp);
    Place(ImageSlot, Image(Warm, Format32Float));
    const Expected initial{Height, 0, 1, [](std::uint32_t row, std::uint32_t x, std::uint32_t, std::uint32_t word) { return SameFloat(word, static_cast<float>(row * Width + x) + 0.5f); }};

    const auto first = Compile(device, SampleProgram(), userData);
    const auto missed = Run(device, first, Height);
    const auto read = ExpectFirstUse(missed, initial, 1, "stored texels: first sample of the CPU's texels");
    const auto hit = Run(device, Compile(device, SampleProgram(), userData), Height);
    ExpectHit(hit, initial, "stored texels: second sample of the CPU's texels");
    ExpectResolved(missed, read, hit, 1, "stored texels");

    const auto warm = StoreThenSample(device, Warm, WarmSeed, userData, "stored texels: the sample after the store into the sampled image");
    ExpectHit(warm, StoredTexels(WarmSeed), "stored texels: a sample after an image store in the same submission, through a key already in the table");
    const auto warmAfter = Run(device, Compile(device, SampleProgram(), userData), Height);
    ExpectHit(warmAfter, StoredTexels(WarmSeed), "stored texels: the next submission's sample of the stored image");

    Place(ImageSlot, Image(Cold, Format32Float));
    const auto cold = StoreThenSample(device, Cold, ColdSeed, userData, "stored texels: the sample after the store into an image not sampled before");
    const auto coldRead = ExpectFirstUse(cold, StoredTexels(ColdSeed), 1, "stored texels: the first sample of an image stored in the same submission");
    const auto coldAfter = Run(device, Compile(device, SampleProgram(), userData), Height);
    ExpectHit(coldAfter, StoredTexels(ColdSeed), "stored texels: the next submission's sample of an image first sampled after its store");
    ExpectResolved(cold, coldRead, coldAfter, 1, "stored texels: the cold image");
}

}

int main() {
    return Main("device table stored texels", Run);
}
