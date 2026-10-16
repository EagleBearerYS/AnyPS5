#include "DrawSubmitFixture.hpp"
#include "prx/libSceAgcDriver/Graphics/include/ColorTargetLayout.hpp"
#include "prx/libSceAgcDriver/Graphics/include/TextureTiling.hpp"
#include <algorithm>
#include <span>
#include <string>
#include <vector>

#ifdef _WIN32

namespace {

using DeviceTable::Expect;
using NewPath::Commands;
using NewPath::Hex;
using NewPath::PageBytes;
using NewPath::PrivatePages;
using NewPath::Program;
using NewPath::Require;
namespace Graphics = AgcDriver::Graphics;

constexpr std::uint32_t Red = 0xff0000ffu;
constexpr std::uint32_t ElementBytes = 4;
constexpr std::uint32_t VertexUserWords = 4;
constexpr std::uint32_t CopyLanes = 64;

struct Rect {
    std::uint32_t left;
    std::uint32_t top;
    std::uint32_t right;
    std::uint32_t bottom;
};

struct Target {
    const char* name;
    Graphics::ColorTileMode mode;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t mips;
    std::uint32_t layers;
    std::uint64_t misalign;
};

struct DrawCase {
    std::uint32_t layer;
    std::uint32_t mip;
    Rect rect;
    std::uint32_t seed;
};

struct Layout {
    std::vector<Graphics::TileMipLayout> mips;
    std::uint64_t layerBytes = 0;
    std::uint64_t bytes = 0;
};

using Corners = std::array<std::array<float, 4>, 3>;
constexpr Corners FullScreen{{{-1.0f, -1.0f, 0.5f, 1.0f}, {3.0f, -1.0f, 0.5f, 1.0f}, {-1.0f, 3.0f, 0.5f, 1.0f}}};

DrawSubmit::Header VertexHeader;
DrawSubmit::Header PixelHeader;

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

const Program& RedPixel() {
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

const Program& CopyProgram() {
    static const Program program = [] {
        using namespace NewPath::Isa;
        Program built;
        built << Vop1(VMovB32, 1, 8)
              << Vop2(VLshlRevB32, 1, InlineInteger(6), 1)
              << Vop2(VAddNcU32, 1, Vgpr(0), 1)
              << Vop2(VLshlRevB32, 1, InlineInteger(2), 1)
              << Mubuf(BufferLoadDword, 2, 1, 0)
              << WaitAll
              << Mubuf(BufferStoreDword, 2, 1, 4)
              << EndProgram;
        return built;
    }();
    return program;
}

struct VertexBuffer {
    PrivatePages pages{1};

    VertexBuffer() { std::memcpy(pages.Data(), FullScreen.data(), sizeof(FullScreen)); }

    std::array<std::uint32_t, VertexUserWords> Descriptor() const {
        const auto address = pages.Address();
        return {NewPath::Low(address), (NewPath::High(address) & 0xffffu) | (16u << 16u), static_cast<std::uint32_t>(FullScreen.size()), 0x01016facu};
    }
};

Layout LayoutOf(const Target& target) {
    Layout layout;
    layout.mips = Graphics::ComputeElementMipLayout(Graphics::ColorTextureTileMode(target.mode), ElementBytes, target.width, target.height, target.mips);
    layout.layerBytes = Graphics::ComputeSurfaceSize(layout.mips, 1);
    layout.bytes = layout.layerBytes * target.layers;
    return layout;
}

std::uint32_t Background(std::uint64_t word, std::uint32_t seed) {
    return (seed << 28u) | static_cast<std::uint32_t>(word & 0x0fffffffu);
}

std::string Describe(const Rect& rect) {
    return "(" + std::to_string(rect.left) + ", " + std::to_string(rect.top) + ")-(" + std::to_string(rect.right) + ", " + std::to_string(rect.bottom) + ")";
}

std::uint64_t Overlap(const Rect& rect, std::uint32_t left, std::uint32_t top, std::uint32_t right, std::uint32_t bottom) {
    const auto x0 = std::max(rect.left, left);
    const auto x1 = std::min(rect.right, right);
    const auto y0 = std::max(rect.top, top);
    const auto y1 = std::min(rect.bottom, bottom);
    return x0 < x1 && y0 < y1 ? std::uint64_t{x1 - x0} * (y1 - y0) : 0u;
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

void Draw(const Target& target, const Layout& layout, std::uint64_t base, const DrawCase& draw, const VertexBuffer& vertices) {
    const auto& mip = layout.mips.at(draw.mip);
    Commands commands;
    DrawSubmit::Baseline(commands, {reinterpret_cast<const void*>(static_cast<std::uintptr_t>(base)), mip.width, mip.height}, DrawSubmit::CenterAndPosition);
    DrawSubmit::Context(commands, 0x3b0, ((target.mips - 1u) << 28u) | ((target.width - 1u) << 14u) | (target.height - 1u));
    DrawSubmit::Context(commands, 0x3b8, (target.layers - 1u) | (static_cast<std::uint32_t>(target.mode) << 14u) | (1u << 24u) | (1u << 27u));
    DrawSubmit::Context(commands, 0x31b, draw.layer | (draw.layer << 13u) | (draw.mip << 26u));
    DrawSubmit::Context(commands, 0x90, 0x80000000u | (draw.rect.top << 16u) | draw.rect.left);
    DrawSubmit::Context(commands, 0x91, (draw.rect.bottom << 16u) | draw.rect.right);
    DrawSubmit::Programs(commands, BoundedVertex(), VertexUserWords, RedPixel(), 0u);
    DrawSubmit::Instances(commands, 1u);
    DrawSubmit::VertexUserData(commands, vertices.Descriptor());
    DrawSubmit::DrawAuto(commands, static_cast<std::uint32_t>(FullScreen.size()));
    const auto failure = Submitted(commands);
    Expect(failure.empty(), std::string(target.name) + ": the draw failed: " + failure);
}

std::vector<std::uint32_t> GpuRead(std::uint64_t base, std::uint64_t bytes, const PrivatePages& output, const std::string& what) {
    const auto words = static_cast<std::uint32_t>(bytes / 4u);
    const auto image = NewPath::BufferResource(base, static_cast<std::uint32_t>(bytes));
    const auto copy = NewPath::BufferResource(output.Address(), static_cast<std::uint32_t>(bytes));
    const std::array<std::uint32_t, 8> userData{image[0], image[1], image[2], image[3], copy[0], copy[1], copy[2], copy[3]};
    Commands commands;
    commands.Dispatch(CopyProgram(), userData, CopyLanes, (words + CopyLanes - 1u) / CopyLanes);
    const auto failure = Submitted(commands);
    Expect(failure.empty(), what + ": the dispatch reading the image's memory failed: " + failure);
    AgcDriver::GuestMemory::FlushGpuWrites(output.Address(), bytes);
    std::vector<std::uint32_t> read(words);
    std::memcpy(read.data(), output.Data(), bytes);
    return read;
}

std::vector<std::uint32_t> CpuRead(std::uint64_t base, std::uint64_t bytes) {
    std::vector<std::uint32_t> read(static_cast<std::size_t>(bytes / 4u));
    AgcDriver::GuestMemory::Read(base, std::as_writable_bytes(std::span(read)));
    return read;
}

void Verify(std::span<const std::uint32_t> words, const Target& target, const Layout& layout, const DrawCase& draw, const std::string& what) {
    const auto& mip = layout.mips.at(draw.mip);
    const auto begin = (draw.layer * layout.layerBytes + mip.tiledOffset) / 4u;
    const auto end = begin + mip.tiledSize / 4u;
    std::size_t outside = 0;
    std::size_t foreign = 0;
    std::size_t red = 0;
    std::string firstOutside;
    std::string firstForeign;
    for (std::size_t word = 0; word < words.size(); ++word) {
        const auto value = words[word];
        const auto kept = Background(word, draw.seed);
        if (word < begin || word >= end) {
            if (value != kept && outside++ == 0u) firstOutside = "byte " + Hex(word * 4u) + " holds " + Hex(value) + ", expected " + Hex(kept);
            continue;
        }
        if (value == Red) ++red;
        else if (value != kept && foreign++ == 0u) firstForeign = "byte " + Hex(word * 4u) + " holds " + Hex(value) + ", expected " + Hex(kept) + " or the drawn " + Hex(Red);
    }
    const auto area = std::uint64_t{draw.rect.right - draw.rect.left} * (draw.rect.bottom - draw.rect.top);
    const auto subject = what + ": layer " + std::to_string(draw.layer) + " mip " + std::to_string(draw.mip) + ", rectangle " + Describe(draw.rect);
    Expect(outside == 0u, subject + ": " + std::to_string(outside) + " words of the other layers and mips changed, first " + firstOutside);
    Expect(foreign == 0u, subject + ": " + std::to_string(foreign) + " words of the drawn layer and mip are neither their previous bytes nor the drawn color, first " + firstForeign);
    Expect(red == area, subject + ": " + std::to_string(red) + " texels hold the drawn color, expected " + std::to_string(area));
    if (target.mode == Graphics::ColorTileMode::Linear) {
        std::size_t misplaced = 0;
        std::string first;
        for (std::uint32_t y = 0; y < mip.height; ++y) {
            for (std::uint32_t x = 0; x < mip.width; ++x) {
                const auto word = begin + (std::uint64_t{y} * mip.pitchBytes + std::uint64_t{x} * ElementBytes) / 4u;
                const bool inside = x >= draw.rect.left && x < draw.rect.right && y >= draw.rect.top && y < draw.rect.bottom;
                if ((words[word] == Red) == inside) continue;
                if (misplaced++ == 0u) first = "texel (" + std::to_string(x) + ", " + std::to_string(y) + ") holds " + Hex(words[word]);
            }
        }
        Expect(misplaced == 0u, subject + ": " + std::to_string(misplaced) + " texels are drawn outside the rectangle or kept inside it, first " + first);
        return;
    }
    if (mip.tail) return;
    const auto blockBytes = Graphics::ColorTileModeBlockBytes(target.mode);
    const auto blockWidth = mip.pitchBytes / ElementBytes / mip.blocksPerRow;
    const auto blockHeight = blockBytes / ElementBytes / blockWidth;
    const auto blocks = static_cast<std::uint32_t>(mip.tiledSize / blockBytes);
    std::size_t wrongBlocks = 0;
    std::string first;
    for (std::uint32_t block = 0; block < blocks; ++block) {
        const auto bx = block % mip.blocksPerRow;
        const auto by = block / mip.blocksPerRow;
        const auto expected = Overlap(draw.rect, bx * blockWidth, by * blockHeight, (bx + 1u) * blockWidth, (by + 1u) * blockHeight);
        const auto from = begin + std::uint64_t{block} * blockBytes / 4u;
        const auto actual = static_cast<std::uint64_t>(std::count(words.begin() + static_cast<std::ptrdiff_t>(from), words.begin() + static_cast<std::ptrdiff_t>(from + blockBytes / 4u), Red));
        if (actual == expected) continue;
        if (wrongBlocks++ == 0u) first = "block (" + std::to_string(bx) + ", " + std::to_string(by) + ") holds " + std::to_string(actual) + " drawn texels, expected " + std::to_string(expected);
    }
    Expect(wrongBlocks == 0u, subject + ": " + std::to_string(wrongBlocks) + " of " + std::to_string(blocks) + " " + std::to_string(blockBytes / 1024u) + " KiB blocks hold another number of drawn texels than the rectangle covers in them, first " + first);
}

void Run(const Target& target, std::span<const DrawCase> draws) {
    static const bool registered = [] {
        DrawSubmit::Register(VertexHeader, BoundedVertex(), DrawSubmit::VertexShaderType);
        DrawSubmit::Register(PixelHeader, RedPixel(), DrawSubmit::PixelShaderType);
        return true;
    }();
    static_cast<void>(registered);
    const auto layout = LayoutOf(target);
    const auto pages = static_cast<std::uint32_t>((target.misalign + layout.bytes + PageBytes - 1u) / PageBytes);
    PrivatePages image(pages);
    PrivatePages output(static_cast<std::uint32_t>((layout.bytes + PageBytes - 1u) / PageBytes));
    const auto base = image.Address(target.misalign);
    Require(base % Graphics::ColorTileModeBlockBytes(target.mode) == 0u, std::string(target.name) + ": the image base is not aligned to its tile blocks");
    const VertexBuffer vertices;
    auto* words = reinterpret_cast<volatile std::uint32_t*>(static_cast<std::uintptr_t>(base));
    for (std::size_t index = 0; index < draws.size(); ++index) {
        const auto& draw = draws[index];
        for (std::uint64_t word = 0; word < layout.bytes / 4u; ++word) words[word] = Background(word, draw.seed);
        const auto what = std::string(target.name) + ", draw " + std::to_string(index + 1u) + " after the CPU rewrote the whole image";
        Draw(target, layout, base, draw, vertices);
        Verify(GpuRead(base, layout.bytes, output, what), target, layout, draw, what + ", read by an address-based dispatch");
        Verify(CpuRead(base, layout.bytes), target, layout, draw, what + ", read by the CPU");
    }
}

void TiledLayers() {
    constexpr Target target{"a 64 KiB tiled array of 3 layers and 3 mips", Graphics::ColorTileMode::RenderTarget, 512, 256, 3, 3, 0};
    constexpr std::array<DrawCase, 3> draws{{
        {1, 0, {40, 30, 200, 100}, 1},
        {1, 0, {300, 150, 460, 250}, 2},
        {2, 1, {10, 20, 70, 60}, 3},
    }};
    Run(target, draws);
}

void LinearLayers() {
    constexpr Target target{"a linear array of 3 layers", Graphics::ColorTileMode::Linear, 512, 256, 1, 3, 0};
    constexpr std::array<DrawCase, 2> draws{{
        {1, 0, {40, 30, 200, 100}, 1},
        {1, 0, {300, 150, 460, 250}, 2},
    }};
    Run(target, draws);
}

void UnalignedLayers() {
    constexpr Target target{"a 4 KiB tiled array of 3 layers based 12 KiB past a 64 KiB boundary", Graphics::ColorTileMode::Standard4KB, 512, 256, 1, 3, 0x3000};
    constexpr std::array<DrawCase, 2> draws{{
        {1, 0, {40, 30, 200, 100}, 1},
        {2, 0, {300, 150, 460, 250}, 2},
    }};
    Run(target, draws);
}

}

#endif

int main() {
#ifdef _WIN32
    return DrawSubmit::Main("color target unit", [] {
        DrawSubmit::Cases({
            {"64 KiB tiled layers and mips", TiledLayers},
            {"linear layers", LinearLayers},
            {"unaligned 4 KiB tiled layers", UnalignedLayers},
        });
    });
#else
    std::printf("skipped, commit imports are Windows-only\n");
    return VulkanTestSkipped;
#endif
}
