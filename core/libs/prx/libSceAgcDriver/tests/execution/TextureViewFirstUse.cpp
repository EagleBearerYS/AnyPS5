#include "DrawSubmitFixture.hpp"
#include <functional>
#include <optional>

namespace {

using namespace DrawSubmit;

constexpr std::uint32_t TargetWidth = 64;
constexpr std::uint32_t TargetHeight = 32;
constexpr std::uint32_t Texels = TargetWidth * TargetHeight;
constexpr std::uint32_t Lanes = 64;
constexpr std::uint32_t LaneWords = 4;
constexpr std::uint32_t OutputWords = Texels * LaneWords;
constexpr std::uint32_t Draws = 16;
constexpr std::uint32_t BandRows = TargetHeight / Draws;
constexpr std::uint32_t BandVertices = 6;
constexpr std::uint32_t DispatchTextures = 6;
constexpr std::uint32_t DrawTextures = Draws / 2;
constexpr std::uint32_t MixedTextures = 2;
constexpr std::uint32_t TextureCount = DispatchTextures + DrawTextures + MixedTextures;
constexpr std::uint32_t DispatchViews = 2 * DispatchTextures;
constexpr std::uint32_t MixedDispatches = 2 * MixedTextures;
constexpr std::uint32_t Outputs = DispatchViews + MixedDispatches;
constexpr std::uint32_t BlockWords = 16;
constexpr std::uint32_t TextureWord = 0;
constexpr std::uint32_t SamplerWord = 8;
constexpr std::uint32_t OutputWord = 12;
constexpr std::uint32_t VertexUserWords = 4;
constexpr std::uint32_t PixelUserWords = 2;
constexpr std::uint32_t OneOverTargetWidth = 0x3c800000u;
constexpr std::uint32_t OneOverTargetHeight = 0x3d000000u;
constexpr std::uint32_t IdentitySwizzle = 0xfacu;
constexpr std::uint32_t SwappedSwizzle = 0xf2eu;

enum class View : std::uint32_t { Identity, Swapped };

TestGuest::Array<std::uint32_t, TextureCount * Texels> TextureMemory;
TestGuest::Array<std::uint32_t, (Outputs + Draws) * BlockWords> Blocks;
TestGuest::Array<std::uint32_t, Outputs * OutputWords> OutputMemory;
TestGuest::Array<std::array<float, 4>, Draws * BandVertices> Vertices;
TestGuest::Array<std::uint32_t, Texels> Pixels;

Header VertexHeader;
Header PixelHeader;

std::uint32_t Channel(std::uint32_t texture, std::uint32_t x, std::uint32_t y, std::uint32_t component) {
    return (0x11u + 29u * texture + 7u * x + 41u * y + 83u * component) & 0xffu;
}

std::uint32_t Sampled(std::uint32_t texture, View view, std::uint32_t x, std::uint32_t y, std::uint32_t component) {
    if (view == View::Swapped && component != 1u && component != 3u) component = 2u - component;
    return Channel(texture, x, y, component);
}

std::uint32_t* TextureData(std::uint32_t texture) {
    return TextureMemory.data() + texture * Texels;
}

void FillTexture(std::uint32_t texture) {
    for (std::uint32_t y = 0; y < TargetHeight; ++y) {
        for (std::uint32_t x = 0; x < TargetWidth; ++x) TextureData(texture)[y * TargetWidth + x] = Channel(texture, x, y, 0) | (Channel(texture, x, y, 1) << 8u) | (Channel(texture, x, y, 2) << 16u) | (Channel(texture, x, y, 3) << 24u);
    }
}

std::array<std::uint32_t, 8> TextureDescriptor(std::uint32_t texture, View view) {
    const auto address = AddressOf(TextureData(texture));
    return {
        Low(address >> 8u),
        static_cast<std::uint32_t>((address >> 40u) & 0xffu) | (DeviceTable::Format8_8_8_8UNorm << 20u) | (((TargetWidth - 1u) & 3u) << 30u),
        ((TargetWidth - 1u) >> 2u) | ((TargetHeight - 1u) << 14u) | (1u << 31u),
        (view == View::Identity ? IdentitySwizzle : SwappedSwizzle) | (DeviceTable::Type2D << 28u),
        0u,
        0u,
        0u,
        0u,
    };
}

std::uint32_t* Block(std::uint32_t index) {
    return Blocks.data() + index * BlockWords;
}

std::uint32_t* Output(std::uint32_t index) {
    return OutputMemory.data() + index * OutputWords;
}

std::array<std::uint32_t, 2> BlockUserData(std::uint32_t index, std::uint32_t texture, View view, std::optional<std::uint32_t> output) {
    auto* words = Block(index);
    std::fill(words, words + BlockWords, 0u);
    const auto descriptor = TextureDescriptor(texture, view);
    std::copy(descriptor.begin(), descriptor.end(), words + TextureWord);
    std::copy(DeviceTable::PointClamp.begin(), DeviceTable::PointClamp.end(), words + SamplerWord);
    if (output) {
        const auto buffer = DeviceTable::Buffer(Output(*output), OutputWords * 4u);
        std::copy(buffer.begin(), buffer.end(), words + OutputWord);
        std::fill(Output(*output), Output(*output) + OutputWords, DeviceTable::Untouched);
    }
    const auto address = AddressOf(words);
    return {Low(address), High(address)};
}

const Program& SampleDispatch() {
    static const Program program = [] {
        using namespace DrawSubmit::Isa;
        Program built;
        built << Smem(SLoadDwordX8, 36, 0, TextureWord * 4u)
              << Smem(SLoadDwordX4, 32, 0, SamplerWord * 4u)
              << Smem(SLoadDwordX4, 28, 0, OutputWord * 4u)
              << Vop1(VCvtF32U32, 1, Vgpr(0))
              << Vop2(VAddF32, 1, Half, 1)
              << Vop2(VMulF32, 1, Literal, 1) << OneOverTargetWidth
              << Vop1(VCvtF32U32, 2, 2)
              << Vop2(VAddF32, 2, Half, 2)
              << Vop2(VMulF32, 2, Literal, 2) << OneOverTargetHeight
              << WaitAll
              << Mimg(ImageSampleLz, 0xfu, false, 4, 1, 36, 32);
        StoreLane(built, 2, 28);
        return built;
    }();
    return program;
}

const Program& BandVertex() {
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

const Program& SamplePixel() {
    static const Program program = [] {
        using namespace DrawSubmit::Isa;
        Program built;
        built << Smem(SLoadDwordX8, 36, 0, TextureWord * 4u)
              << Smem(SLoadDwordX4, 32, 0, SamplerWord * 4u)
              << Vop2(VMulF32, 4, Literal, 2) << OneOverTargetWidth
              << Vop2(VMulF32, 5, Literal, 3) << OneOverTargetHeight
              << WaitAll
              << Mimg(ImageSampleLz, 0xfu, false, 6, 4, 36, 32)
              << WaitAll
              << Export(ColorTarget, 6, true)
              << EndProgram;
        return built;
    }();
    return program;
}

void FillBand(std::uint32_t band) {
    const auto top = 1.0f - 2.0f * static_cast<float>(band * BandRows) / static_cast<float>(TargetHeight);
    const auto bottom = 1.0f - 2.0f * static_cast<float>((band + 1u) * BandRows) / static_cast<float>(TargetHeight);
    const std::array<std::array<float, 2>, BandVertices> corners{{{-1.0f, top}, {1.0f, top}, {-1.0f, bottom}, {1.0f, top}, {1.0f, bottom}, {-1.0f, bottom}}};
    for (std::uint32_t vertex = 0; vertex < BandVertices; ++vertex) Vertices[band * BandVertices + vertex] = {corners[vertex][0], corners[vertex][1], 0.5f, 1.0f};
}

std::array<std::uint32_t, VertexUserWords> BandUserData(std::uint32_t band) {
    const auto address = AddressOf(Vertices.data() + band * BandVertices);
    return {Low(address), (High(address) & 0xffffu) | (16u << 16u), BandVertices, 0x01016facu};
}

struct Sample {
    std::uint32_t texture;
    View view;
};

void RecordDispatch(Commands& commands, std::uint32_t output, const Sample& sample) {
    const auto userData = BlockUserData(output, sample.texture, sample.view, output);
    commands.Dispatch(SampleDispatch(), userData, Lanes, TargetHeight);
}

void RecordDraws(Commands& commands, const std::function<std::optional<Sample>(std::uint32_t band)>& bands) {
    Baseline(commands, {Pixels.data(), TargetWidth, TargetHeight}, CenterAndPosition);
    Programs(commands, BandVertex(), VertexUserWords, SamplePixel(), PixelUserWords);
    Instances(commands, 1u);
    for (std::uint32_t band = 0; band < Draws; ++band) {
        const auto sample = bands(band);
        if (!sample) continue;
        VertexUserData(commands, BandUserData(band));
        PixelUserData(commands, BlockUserData(Outputs + band, sample->texture, sample->view, std::nullopt));
        DrawAuto(commands, BandVertices);
    }
}

std::string Submit(Commands& commands) {
    std::string failure;
    NewPath::StderrCapture capture;
    try {
        commands.Submit(0);
        NewPath::WaitIdle();
    } catch (const std::exception& error) {
        failure = error.what();
    }
    const auto log = capture.Finish();
    if (log.find("failed") != std::string::npos || log.find("image table:") != std::string::npos || log.find("is rejected") != std::string::npos) failure += "\n" + log;
    return failure;
}

std::string ViewName(const Sample& sample) {
    return "texture " + std::to_string(sample.texture) + (sample.view == View::Identity ? " through the identity view" : " through the view swapping red and blue");
}

void ExpectDispatch(std::uint32_t output, const Sample& sample, const std::string& what) {
    const auto words = ReadBack(Output(output), OutputWords);
    std::size_t wrong = 0;
    std::string first;
    for (std::uint32_t y = 0; y < TargetHeight; ++y) {
        for (std::uint32_t x = 0; x < TargetWidth; ++x) {
            for (std::uint32_t component = 0; component < 4u; ++component) {
                const auto word = words[(y * Lanes + x) * LaneWords + component];
                const auto byte = Sampled(sample.texture, sample.view, x, y, component);
                if (DeviceTable::SameUnorm(word, byte) || wrong++ != 0u) continue;
                first = "texel (" + std::to_string(x) + ", " + std::to_string(y) + ") component " + std::to_string(component) + " read " + Hex(word) + ", expected " + std::to_string(byte) + "/255";
            }
        }
    }
    Expect(wrong == 0u, what + ": the dispatch sampling " + ViewName(sample) + " read " + std::to_string(wrong) + " of " + std::to_string(Texels * 4u) + " components wrong, first " + first);
}

void ExpectBands(const std::function<std::optional<Sample>(std::uint32_t band)>& bands, const std::string& what) {
    const auto pixels = ReadBack(Pixels.data(), Pixels.size());
    for (std::uint32_t band = 0; band < Draws; ++band) {
        const auto sample = bands(band);
        if (!sample) continue;
        std::size_t wrong = 0;
        std::string first;
        for (std::uint32_t y = band * BandRows; y < (band + 1u) * BandRows; ++y) {
            for (std::uint32_t x = 0; x < TargetWidth; ++x) {
                std::uint32_t expected = 0;
                for (std::uint32_t component = 0; component < 4u; ++component) expected |= Sampled(sample->texture, sample->view, x, y, component) << (8u * component);
                const auto pixel = pixels[y * TargetWidth + x];
                if (pixel == expected || wrong++ != 0u) continue;
                first = "pixel (" + std::to_string(x) + ", " + std::to_string(y) + ") is " + Hex(pixel) + ", expected " + Hex(expected);
            }
        }
        Expect(wrong == 0u, what + ": draw " + std::to_string(band) + " sampling " + ViewName(*sample) + ": " + std::to_string(wrong) + " of " + std::to_string(BandRows * TargetWidth) + " pixels differ, first " + first);
    }
}

Sample DispatchSample(std::uint32_t output) {
    return {output / 2u, output % 2u == 0u ? View::Identity : View::Swapped};
}

void Dispatches() {
    for (std::uint32_t round = 0; round < 2u; ++round) {
        const std::string what = round == 0u ? "dispatches, first use: " + std::to_string(DispatchViews) + " dispatches in one submission, each sampling a view never used before, two views of each of " + std::to_string(DispatchTextures) + " textures" : "dispatches, reuse: the same dispatches in a later submission";
        Commands commands;
        for (std::uint32_t output = 0; output < DispatchViews; ++output) RecordDispatch(commands, output, DispatchSample(output));
        const auto failure = Submit(commands);
        Expect(failure.empty(), what + ": the submission failed: " + failure);
        for (std::uint32_t output = 0; output < DispatchViews; ++output) ExpectDispatch(output, DispatchSample(output), what);
    }
}

std::optional<Sample> DrawSample(std::uint32_t band) {
    return Sample{DispatchTextures + band / 2u, band % 2u == 0u ? View::Identity : View::Swapped};
}

void DrawsFirstUse() {
    for (std::uint32_t round = 0; round < 2u; ++round) {
        const std::string what = round == 0u ? "draws, first use: " + std::to_string(Draws) + " draws in one submission, each sampling a view never used before, two views of each of " + std::to_string(DrawTextures) + " textures" : "draws, reuse: the same draws in a later submission";
        Pixels.fill(DeviceTable::Untouched);
        Commands commands;
        RecordDraws(commands, DrawSample);
        const auto failure = Submit(commands);
        Expect(failure.empty(), what + ": the submission failed: " + failure);
        ExpectBands(DrawSample, what);
    }
}

void Mixed() {
    const std::uint32_t texture = DispatchTextures + DrawTextures;
    const std::string what = "dispatches and draws: in one submission a dispatch samples a new texture through a new view, draws sample it and another new texture through further new views, and dispatches after them sample those views";
    const auto bands = [&](std::uint32_t band) -> std::optional<Sample> {
        if (band >= 4u) return std::nullopt;
        return Sample{texture + band / 2u, band % 2u == 0u ? View::Swapped : View::Identity};
    };
    const std::array<Sample, MixedDispatches> samples{{{texture, View::Identity}, {texture, View::Swapped}, {texture + 1u, View::Identity}, {texture + 1u, View::Swapped}}};
    Pixels.fill(DeviceTable::Untouched);
    Commands commands;
    RecordDispatch(commands, DispatchViews, samples[0]);
    RecordDraws(commands, bands);
    for (std::uint32_t index = 1; index < MixedDispatches; ++index) RecordDispatch(commands, DispatchViews + index, samples[index]);
    const auto failure = Submit(commands);
    Expect(failure.empty(), what + ": the submission failed: " + failure);
    for (std::uint32_t index = 0; index < MixedDispatches; ++index) ExpectDispatch(DispatchViews + index, samples[index], what);
    ExpectBands(bands, what);
}

void Run() {
    for (std::uint32_t texture = 0; texture < TextureCount; ++texture) FillTexture(texture);
    for (std::uint32_t band = 0; band < Draws; ++band) FillBand(band);
    Register(VertexHeader, BandVertex(), VertexShaderType);
    Register(PixelHeader, SamplePixel(), PixelShaderType);
    Cases({
        {"dispatches", Dispatches},
        {"draws", DrawsFirstUse},
        {"dispatches and draws", Mixed},
    });
}

}

int main() {
    return DrawSubmit::Main("texture view first use", Run);
}
