#include "NewPathFixture.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/DeviceImageTable.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include <exception>

#ifdef _WIN32

namespace {

using namespace NewPath;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Width = 64;
constexpr std::uint32_t Height = 4;
constexpr std::uint32_t Lanes = 64;
constexpr std::uint32_t Groups = 17;
constexpr std::uint32_t Format32UInt = 20;
constexpr std::uint32_t ArgumentBytes = 4096;
constexpr std::uint32_t ArgumentStride = 256;
constexpr std::uint32_t TexelStride = 1024;
constexpr std::uint32_t RingCapacity = AgcDriver::Graphics::DeviceImageTable::RingCapacity;
constexpr std::array<std::uint32_t, 4> PointClamp{0x92u, (4u * 256u) << 12u, 0u, 0u};

alignas(256) constexpr std::array<std::uint32_t, 24> RuntimeLoadCode{
    0xf4080100u, 0xfa000000u, 0xf4080700u, 0xfa000020u, 0x8f108402u, 0xf4240282u, 0x20000000u, 0xf4080b05u,
    0xfa000020u, 0xf4080805u, 0xfa000000u, 0xf42c0916u, 0xfa000020u, 0x360200bfu, 0x7e040280u, 0xf0008f08u,
    0x00090401u, 0x7e120202u, 0x34101286u, 0x4a101100u, 0x34101084u, 0xe0781000u, 0x80070408u, 0xbf810000u,
};

int failures = 0;

void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

std::uint32_t Texel(std::uint32_t group, std::uint32_t x) {
    return 0x10000u * (group + 1u) + x;
}

class Fixture {
public:
    Fixture() : srt(1), blocks(1), output(1), texels(1) {
        for (std::uint32_t group = 0; group < Groups; ++group) {
            auto* rows = reinterpret_cast<std::uint32_t*>(texels.Data() + group * TexelStride);
            for (std::uint32_t y = 0; y < Height; ++y) {
                for (std::uint32_t x = 0; x < Width; ++x) rows[y * Width + x] = Texel(group, x) + 0x100u * y;
            }
            auto* words = reinterpret_cast<std::uint32_t*>(blocks.Data() + ArgumentBytes + group * ArgumentStride);
            std::fill(words, words + ArgumentStride / 4u, 0u);
            std::copy(PointClamp.begin(), PointClamp.end(), words);
            const auto heap = BufferResource(blocks.Address(ArgumentBytes + group * ArgumentStride + 64u), 64u);
            std::copy(heap.begin(), heap.end(), words + 8);
            const auto image = Image(texels.Address(group * TexelStride));
            std::copy(image.begin(), image.end(), words + 24);
            const auto block = blocks.Address(ArgumentBytes + group * ArgumentStride);
            auto* pointers = reinterpret_cast<std::uint32_t*>(blocks.Data());
            pointers[group * 4u] = Low(block);
            pointers[group * 4u + 1u] = High(block);
        }
        auto* root = reinterpret_cast<std::uint32_t*>(srt.Data());
        std::fill(root, root + 32, 0u);
        const auto pointerTable = BufferResource(blocks.Address(), Groups * 16u);
        const auto target = BufferResource(output.Address(), Groups * Lanes * 16u);
        std::copy(pointerTable.begin(), pointerTable.end(), root);
        std::copy(target.begin(), target.end(), root + 8);
    }

    std::vector<std::uint32_t> Run(AgcDriver::VulkanDevice& device) {
        std::memset(output.Data(), 0xee, Groups * Lanes * 16u);
        const std::array<std::uint32_t, 2> userData{Low(srt.Address()), High(srt.Address())};
        const std::span<const std::uint32_t> code(RuntimeLoadCode);
        const std::array<ShaderRecompiler::MemoryRegion, 2> memory{{
            {reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(code)},
            {static_cast<std::uintptr_t>(srt.Address()), std::as_bytes(std::span(srt.Data(), 128))},
        }};
        const ShaderRecompiler::ShaderComputeStageInfo compute{{Lanes, 1, 1}, 0u, {true, false, false}, false, 1};
        ShaderRecompiler::RecompileRequest request{
            {ShaderStage::Compute, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
            {64, 0, userData, compute, std::nullopt, std::nullopt, memory},
            device.Target(),
            {0, 0, 0, 128}
        };
        const auto result = ShaderRecompiler::Recompile(request);
        device.Dispatch(result, Groups, 1, 1, reinterpret_cast<std::uintptr_t>(code.data()));
        device.WaitIdle();
        AgcDriver::GuestMemory::FlushGpuWrites(output.Address(), Groups * Lanes * 16u);
        std::vector<std::uint32_t> words(Groups * Lanes * 4u);
        std::memcpy(words.data(), output.Data(), words.size() * 4u);
        return words;
    }

private:
    std::array<std::uint32_t, 4> Image(std::uint64_t address) const {
        return {Low(address >> 8u), Low((address >> 40u) & 0xffu) | (Format32UInt << 20u) | (((Width - 1u) & 3u) << 30u), ((Width - 1u) >> 2u) | ((Height - 1u) << 14u) | (1u << 31u), 0xfacu | (9u << 28u)};
    }

    PrivatePages srt;
    PrivatePages blocks;
    PrivatePages output;
    PrivatePages texels;
};

enum class GroupRead { Zeros, Texels, Wrong };

GroupRead Classify(const std::vector<std::uint32_t>& words, std::uint32_t group) {
    bool zeros = true;
    bool texels = true;
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
        const auto red = words[(group * Lanes + lane) * 4u];
        zeros = zeros && red == 0u;
        texels = texels && red == Texel(group, lane % Width);
    }
    return texels ? GroupRead::Texels : zeros ? GroupRead::Zeros : GroupRead::Wrong;
}

void OverflowingMisses(AgcDriver::VulkanDevice& device) {
    Fixture fixture;
    const auto start = device.DeviceImageCounts();
    auto words = fixture.Run(device);
    const auto first = device.DeviceImageCounts();
    Expect(first.misses - start.misses == Groups * Lanes && first.dropped - start.dropped == Groups * Lanes - RingCapacity, "key table: " + std::to_string(Groups * Lanes) + " first-use misses in one batch counted " + std::to_string(first.misses - start.misses) + " and dropped " + std::to_string(first.dropped - start.dropped) + ", expected " + std::to_string(Groups * Lanes) + " and " + std::to_string(Groups * Lanes - RingCapacity));
    for (std::uint32_t group = 0; group < Groups; ++group) Expect(Classify(words, group) == GroupRead::Zeros, "key table: group " + std::to_string(group) + " did not read zeros at its first use");
    std::uint32_t runs = 1;
    bool complete = false;
    while (!complete && runs < 4) {
        words = fixture.Run(device);
        ++runs;
        if (runs == 2) {
            const auto inserted = device.DeviceImageCounts().inserted - start.inserted;
            Expect(inserted >= RingCapacity / Lanes && inserted <= Groups, "key table: the misses an overflowing ring kept inserted " + std::to_string(inserted) + " descriptors by the next batch");
        }
        complete = true;
        for (std::uint32_t group = 0; group < Groups; ++group) {
            const auto read = Classify(words, group);
            Expect(read != GroupRead::Wrong, "key table: group " + std::to_string(group) + " read texels of another descriptor or garbage in run " + std::to_string(runs));
            complete = complete && read == GroupRead::Texels;
        }
    }
    const auto last = device.DeviceImageCounts();
    Expect(complete, "key table: descriptors dropped from an overflowing miss ring were not inserted by the next batches");
    Expect(last.inserted - start.inserted == Groups, "key table: " + std::to_string(last.inserted - start.inserted) + " descriptors inserted for " + std::to_string(Groups) + " distinct ones");
    const auto steady = device.DeviceImageCounts();
    words = fixture.Run(device);
    const auto after = device.DeviceImageCounts();
    Expect(after.misses == steady.misses && after.dropped == steady.dropped, "key table: a steady-state run missed");
    for (std::uint32_t group = 0; group < Groups; ++group) Expect(Classify(words, group) == GroupRead::Texels, "key table: group " + std::to_string(group) + " in steady state");
}

}

#endif

int main() {
#ifdef _WIN32
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        OverflowingMisses(*device);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: exception: %s\n", error.what());
        return 1;
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d new path key table checks failed\n", failures);
        return 1;
    }
    std::printf("new path key table tests passed\n");
    return 0;
#else
    std::printf("skipped, the guest arena is Windows-only here\n");
    return VulkanTestSkipped;
#endif
}
