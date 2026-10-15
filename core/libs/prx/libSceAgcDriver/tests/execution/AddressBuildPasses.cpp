#include "DrawSubmitFixture.hpp"
#include <chrono>
#include <cstdlib>
#include <thread>

#ifdef _WIN32

namespace {

using namespace DrawSubmit;

constexpr std::uint32_t Width = 128;
constexpr std::uint32_t Height = 128;
constexpr std::uint32_t RenderTargetAttrib3 = (0x1bu << 14u) | (1u << 24u) | (1u << 27u);
constexpr std::uint32_t VertexUserWords = 4;
constexpr std::uint32_t Red = 0xff0000ffu;
constexpr std::uint32_t Cleared = 0u;
constexpr std::uint32_t LabelValue = 0x40400000u;
constexpr std::uint32_t OutsideLabel = 0x1234u;
constexpr auto ReportInterval = std::chrono::milliseconds(10500);

using Corners = std::array<std::array<float, 4>, 3>;
constexpr Corners FullScreen{{{-1.0f, -1.0f, 0.5f, 1.0f}, {3.0f, -1.0f, 0.5f, 1.0f}, {-1.0f, 3.0f, 0.5f, 1.0f}}};
constexpr Corners Degenerate{{{-1.0f, -1.0f, 0.5f, 1.0f}, {-1.0f, -1.0f, 0.5f, 1.0f}, {-1.0f, -1.0f, 0.5f, 1.0f}}};
constexpr Corners Collapsed{{{-1.0f, -1.0f, 0.5f, 1.0f}, {-1.0f, -1.0f, 0.5f, 1.0f}, {-1.0f, 3.0f, 0.5f, 1.0f}}};

NewPath::PrivatePages* Target = nullptr;

Header BoundedHeader;
Header UnboundedHeader;
Header OtherBoundedHeader;
Header PixelHeader;

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

const Program& OtherBoundedVertex() {
    static const Program program = [] {
        using namespace DrawSubmit::Isa;
        Program built;
        built << Vop1(VMovB32, 20, InlineInteger(0))
              << Indexed(BufferLoadDwordX4, 10, 5, 8)
              << WaitAll
              << Export(PositionTarget, 10, false)
              << EndProgram;
        return built;
    }();
    return program;
}

const Program& UnboundedVertex() {
    static const Program program = [] {
        using namespace DrawSubmit::Isa;
        Program built;
        built << Vop1(VMovB32, 15, InlineInteger(0))
              << Mubuf(BufferLoadDwordX4, 16, 15, 8)
              << WaitAll;
        for (std::uint32_t word = 0; word < 4; ++word) built << Vop1(VReadFirstLaneB32, 20 + word, Vgpr(16 + word));
        built << Indexed(BufferLoadDwordX4, 10, 5, 20)
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

struct VertexBuffer {
    NewPath::PrivatePages pages{1};

    explicit VertexBuffer(const Corners& corners) { std::memcpy(pages.Data(), corners.data(), sizeof(corners)); }

    std::array<std::uint32_t, VertexUserWords> Descriptor() const {
        const auto address = pages.Address();
        return {Low(address), (High(address) & 0xffffu) | (16u << 16u), static_cast<std::uint32_t>(FullScreen.size()), 0x01016facu};
    }
};

void Begin(Commands& commands, const Program& vertex) {
    Baseline(commands, {Target->Data(), Width, Height}, CenterAndPosition);
    Context(commands, 0x3b8, RenderTargetAttrib3);
    Context(commands, 0x31b, 0u);
    Programs(commands, vertex, VertexUserWords, RedPixel(), 0u);
    Instances(commands, 1u);
}

void Draw(Commands& commands, std::span<const std::uint32_t> userData) {
    VertexUserData(commands, userData);
    DrawAuto(commands, static_cast<std::uint32_t>(FullScreen.size()));
}

void Label(Commands& commands, std::uint64_t address, std::uint32_t value) {
    commands.Append(0x49u, {0x514u, (1u << 29u) | (2u << 24u), Low(address), High(address), value, 0u, 0u});
}

void Submit(Commands& commands) {
    commands.Submit(0);
    NewPath::WaitIdle();
}

void ExpectPixels(std::uint32_t expected, const std::string& what) {
    const auto pixels = ReadBack(reinterpret_cast<const std::uint32_t*>(Target->Data()), Width * Height);
    const auto wrong = static_cast<std::size_t>(std::ranges::count_if(pixels, [expected](std::uint32_t pixel) { return pixel != expected; }));
    Expect(wrong == 0u, what + ": " + std::to_string(wrong) + " of " + std::to_string(pixels.size()) + " pixels are not " + Hex(expected));
}

struct Passes {
    std::uint64_t begun = 0;
    std::uint64_t continued = 0;
};

std::optional<Passes> ParseReport(const std::string& log) {
    const std::string marker = "render passes ";
    const auto at = log.rfind(marker);
    if (at == std::string::npos) return std::nullopt;
    unsigned long long begun = 0;
    unsigned long long continued = 0;
    if (std::sscanf(log.c_str() + at + marker.size(), "%llu begun, %llu draws continued one", &begun, &continued) != 2) return std::nullopt;
    return Passes{begun, continued};
}

void Run() {
    Expect(std::getenv("APS5_PROFILE_DRAW") != nullptr, "address build passes: the test must run with APS5_PROFILE_DRAW set, which counts the draws that continue a render pass");
    Register(BoundedHeader, BoundedVertex(), VertexShaderType);
    Register(UnboundedHeader, UnboundedVertex(), VertexShaderType);
    Register(OtherBoundedHeader, OtherBoundedVertex(), VertexShaderType);
    Register(PixelHeader, RedPixel(), PixelShaderType);
    NewPath::PrivatePages target(1);
    Target = &target;
    const VertexBuffer full(FullScreen);
    const VertexBuffer degenerate(Degenerate);
    const VertexBuffer collapsed(Collapsed);
    NewPath::PrivatePages labels(1);
    NewPath::PrivatePages table(1);
    labels.Words()[0] = 0u;
    const auto fullDescriptor = full.Descriptor();
    for (std::uint32_t word = 0; word < 4; ++word) table.Words()[word] = fullDescriptor[word];
    const auto tableDescriptor = NewPath::BufferResource(table.Address(), 16u);
    const std::array<std::uint32_t, VertexUserWords> unboundedUserData{tableDescriptor[0], tableDescriptor[1], tableDescriptor[2], tableDescriptor[3]};
    const auto start = std::chrono::steady_clock::now();

    std::fill_n(reinterpret_cast<std::uint32_t*>(Target->Data()), Width * Height, Cleared);
    {
        Commands commands;
        Begin(commands, BoundedVertex());
        Draw(commands, fullDescriptor);
        Draw(commands, fullDescriptor);
        Submit(commands);
    }
    ExpectPixels(Red, "two bounded draws back to back");

    std::fill_n(reinterpret_cast<std::uint32_t*>(Target->Data()), Width * Height, Cleared);
    {
        Commands commands;
        Begin(commands, BoundedVertex());
        Draw(commands, fullDescriptor);
        Label(commands, labels.Address(), OutsideLabel);
        Draw(commands, fullDescriptor);
        Submit(commands);
    }
    ExpectPixels(Red, "two bounded draws around a label store outside their ranges");
    AgcDriver::GuestMemory::FlushGpuWrites(labels.Address(), 4u);
    Expect(labels.Words()[0] == OutsideLabel, "two bounded draws around a label store outside their ranges: the label holds " + Hex(labels.Words()[0]) + ", expected " + Hex(OutsideLabel));

    std::fill_n(reinterpret_cast<std::uint32_t*>(Target->Data()), Width * Height, Cleared);
    {
        Commands commands;
        Begin(commands, BoundedVertex());
        Draw(commands, degenerate.Descriptor());
        Label(commands, collapsed.pages.Address(16u), LabelValue);
        Draw(commands, collapsed.Descriptor());
        Submit(commands);
    }
    ExpectPixels(Red, "a label store inside the next draw's vertex range: the draw did not read the stored vertex (x = 3.0) that makes its triangle cover the target");

    std::fill_n(reinterpret_cast<std::uint32_t*>(Target->Data()), Width * Height, Cleared);
    {
        Commands commands;
        Begin(commands, BoundedVertex());
        Draw(commands, fullDescriptor);
        Programs(commands, UnboundedVertex(), VertexUserWords, RedPixel(), 0u);
        Draw(commands, unboundedUserData);
        Submit(commands);
    }
    ExpectPixels(Red, "a bounded draw, then a draw whose vertex reads are unbounded");

    std::fill_n(reinterpret_cast<std::uint32_t*>(Target->Data()), Width * Height, Cleared);
    {
        Commands commands;
        Begin(commands, BoundedVertex());
        Draw(commands, fullDescriptor);
        Programs(commands, OtherBoundedVertex(), VertexUserWords, RedPixel(), 0u);
        Draw(commands, fullDescriptor);
        Submit(commands);
    }
    ExpectPixels(Red, "a bounded draw, then a bounded draw of another vertex program");

    std::this_thread::sleep_until(start + ReportInterval);
    std::string log;
    {
        NewPath::StderrCapture capture;
        Commands commands;
        Begin(commands, BoundedVertex());
        Draw(commands, fullDescriptor);
        Submit(commands);
        log = capture.Finish();
    }
    const auto passes = ParseReport(log);
    Expect(passes.has_value(), "address build passes: no [draws] report with the render pass counts after 10 s:\n" + log);
    if (!passes) return;
    constexpr Passes expected{8, 3};
    Expect(passes->begun == expected.begun && passes->continued == expected.continued, "address build passes: the draws began " + std::to_string(passes->begun) + " render passes and " + std::to_string(passes->continued) + " continued one; expected " + std::to_string(expected.begun) + " and " + std::to_string(expected.continued) + " (two bounded draws: one pass; two bounded draws around a label store outside their ranges: one pass; a label store inside the second draw's vertex range: two; a bounded draw then an unbounded reader: two; two bounded draws of two vertex programs: one pass; the report draw: one)");
}

}

#endif

int main() {
#ifdef _WIN32
    return DrawSubmit::Main("address build passes", Run);
#else
    std::printf("skipped, commit imports are Windows-only\n");
    return VulkanTestSkipped;
#endif
}
