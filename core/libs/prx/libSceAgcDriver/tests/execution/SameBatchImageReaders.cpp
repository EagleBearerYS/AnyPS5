#include "DeviceTableFixture.hpp"
#include "prx/libSceAgcDriver/Graphics/include/GuestTextureResource.hpp"
#include "prx/libSceAgcDriver/Graphics/include/TextureTiling.hpp"
#include <algorithm>
#include <chrono>
#include <set>
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
using NewPath::Require;
namespace Graphics = AgcDriver::Graphics;
using Graphics::StorageTexture;
using Clock = std::chrono::steady_clock;

constexpr std::uint32_t Side = 256;
constexpr std::uint32_t Layers = 2;
constexpr std::uint32_t Type2DArray = 13;
constexpr std::uint32_t Standard64KB = 0x09u;
constexpr std::uint32_t Lanes = 64;
constexpr std::uint32_t Left = 96;
constexpr std::uint32_t Top = 40;
constexpr std::uint32_t Rows = 48;
constexpr std::uint32_t RectMark = 0xa0000000u;
constexpr std::uint32_t CopyLanes = 64;
constexpr std::uint32_t SpinLanes = 64;
constexpr std::uint32_t SpinLabelWord = 1024;
constexpr std::uint32_t StoreLabelWord = 2048;
constexpr std::uint32_t CalibrationRounds = 1u << 20u;
constexpr std::uint64_t MaxRounds = std::uint64_t{1} << 31u;
constexpr auto Busy = std::chrono::milliseconds(600);
constexpr auto CalibrationTime = std::chrono::milliseconds(50);

class Surface {
public:
    Surface() : pages(static_cast<std::uint32_t>((BytesOf() + PageBytes - 1u) / PageBytes)), base(pages.Address()) {
        geometry = Graphics::DescribeSurface(Graphics::DecodeTextureResource(Descriptor()));
        Require(geometry.guestBytes <= pages.Bytes() && geometry.layerBytes == 4u * PageBytes, "same batch image readers: the surface is not two layers of four 64 KiB units");
    }

    std::array<std::uint32_t, 8> Descriptor() const { return DescriptorAt(base); }

    static std::array<std::uint32_t, 8> DescriptorAt(std::uint64_t address) {
        return {
            Low(address >> 8u),
            (Low(address >> 40u) & 0xffu) | (DeviceTable::Format32UInt << 20u) | (((Side - 1u) & 3u) << 30u),
            ((Side - 1u) >> 2u) | ((Side - 1u) << 14u) | (1u << 31u),
            0xfacu | (Standard64KB << 20u) | (Type2DArray << 28u),
            Layers - 1u,
            0u,
            0u,
            0u,
        };
    }

    static std::uint64_t BytesOf() { return Graphics::DescribeSurface(Graphics::DecodeTextureResource(DescriptorAt(PageBytes))).guestBytes; }

    std::uint64_t Address(std::uint64_t offset = 0) const { return base + offset; }
    std::uint64_t Bytes() const { return geometry.guestBytes; }
    std::uint64_t LayerBytes() const { return geometry.layerBytes; }

private:
    PrivatePages pages;
    std::uint64_t base;
    Graphics::SurfaceGeometry geometry;
};

std::uint32_t Background(std::uint64_t word, std::uint32_t seed) {
    return (seed << 28u) | static_cast<std::uint32_t>(word & 0x0fffffffu);
}

void Fill(const Surface& surface, std::uint32_t seed) {
    auto* words = reinterpret_cast<volatile std::uint32_t*>(static_cast<std::uintptr_t>(surface.Address()));
    for (std::uint64_t word = 0; word < surface.Bytes() / 4u; ++word) words[word] = Background(word, seed);
    AgcDriver::GuestMemory::BumpCollectEpoch();
}

const Program& StoreProgram() {
    static const Program program = [] {
        using namespace DeviceTable::Asm;
        constexpr std::uint32_t SLoadDwordX8 = 0x03u;
        constexpr std::uint32_t VOrB32 = 0x1cu;
        Program built;
        built << Smem(SLoadDwordX8, 36, 0, DeviceTable::ImageSlot * 4u) << WaitAll
              << Vop2(VAddNcU32, 1, Literal, 0) << Left
              << Vop1(VMovB32, 2, 2)
              << Vop2(VAddNcU32, 2, Literal, 2) << Top
              << Vop1(VMovB32, 3, InlineInteger(0))
              << Vop2(VLshlRevB32, 4, InlineInteger(12), 2)
              << Vop2(VOrB32, 4, Vgpr(1), 4)
              << Vop2(VOrB32, 4, Literal, 4) << RectMark
              << std::array<std::uint32_t, 2>{0xf0000000u | (ImageStore << 18u) | 0x1000u | (1u << 8u) | (5u << 3u), ((36u / 4u) << 16u) | (4u << 8u) | 1u}
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

TestGuest::Array<std::uint32_t, DeviceTable::SrtWords> StoreSrt;

void Store(Commands& commands, const Surface& surface) {
    std::fill(StoreSrt.begin(), StoreSrt.end(), 0u);
    const auto image = surface.Descriptor();
    std::copy(image.begin(), image.end(), StoreSrt.begin() + DeviceTable::ImageSlot);
    AgcDriver::GuestMemory::BumpCollectEpoch();
    const auto srt = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(StoreSrt.data()));
    const std::array<std::uint32_t, 2> userData{Low(srt), High(srt)};
    commands.Dispatch(StoreProgram(), userData, Lanes, Rows);
}

void Copy(Commands& commands, std::uint64_t source, std::uint64_t bytes, const PrivatePages& output) {
    const auto from = NewPath::BufferResource(source, static_cast<std::uint32_t>(bytes));
    const auto to = NewPath::BufferResource(output.Address(), static_cast<std::uint32_t>(bytes));
    const std::array<std::uint32_t, 8> userData{from[0], from[1], from[2], from[3], to[0], to[1], to[2], to[3]};
    commands.Dispatch(CopyProgram(), userData, CopyLanes, static_cast<std::uint32_t>(bytes / 4u / CopyLanes));
}

void Spin(Commands& commands, const PrivatePages& scratch, std::uint32_t rounds) {
    const auto buffer = NewPath::BufferResource(scratch.Address(), SpinLanes * 4u);
    const std::array<std::uint32_t, 5> userData{buffer[0], buffer[1], buffer[2], buffer[3], rounds};
    commands.Dispatch(SpinProgram(), userData, SpinLanes);
}

std::string Submitted(Commands& commands, bool settle) {
    std::string failure;
    NewPath::StderrCapture capture;
    try {
        commands.Submit(0);
        NewPath::WaitIdle();
        if (settle) NewPath::Settle();
    } catch (const std::exception& error) {
        failure = error.what();
    }
    const auto log = capture.Finish();
    if (log.find("failed") != std::string::npos || log.find("rejected") != std::string::npos) failure += "\n" + log;
    return failure;
}

std::uint32_t Calibrate(const PrivatePages& scratch) {
    for (std::uint64_t measured = 0;; measured = measured == 0u ? CalibrationRounds : measured * 4u) {
        Commands commands;
        Spin(commands, scratch, measured == 0u ? 1u : static_cast<std::uint32_t>(measured));
        const auto start = Clock::now();
        commands.Submit(0);
        NewPath::WaitIdle();
        NewPath::Settle();
        const auto took = Clock::now() - start;
        if (measured == 0u || (took < CalibrationTime && measured < MaxRounds / 4u)) continue;
        const auto microseconds = std::max<long long>(std::chrono::duration_cast<std::chrono::microseconds>(took).count(), 1);
        const auto rounds = measured * static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(Busy).count()) / static_cast<std::uint64_t>(microseconds);
        return static_cast<std::uint32_t>(std::clamp<std::uint64_t>(rounds, CalibrationRounds, MaxRounds));
    }
}

bool LabelPending(std::uint64_t address) {
    const auto* recorder = Graphics::Recorder::Active();
    return recorder != nullptr && recorder->LabelPendingAt(address);
}

std::uint32_t UnitOf(std::uint32_t layer, std::uint32_t x, std::uint32_t y) {
    return layer * 4u + (y / 128u) * 2u + x / 128u;
}

void ExpectWords(std::span<const std::uint32_t> words, std::uint32_t seed, const std::string& what) {
    std::set<std::uint32_t> expected;
    for (std::uint32_t y = Top; y < Top + Rows; ++y) {
        for (std::uint32_t x = Left; x < Left + Lanes; ++x) expected.insert(RectMark | (y << 12u) | x);
    }
    std::set<std::uint32_t> seen;
    std::size_t foreign = 0;
    std::size_t misplaced = 0;
    std::string first;
    for (std::size_t word = 0; word < words.size(); ++word) {
        const auto value = words[word];
        if (value == Background(word, seed)) continue;
        const auto unit = static_cast<std::uint32_t>(word * 4u / PageBytes);
        if (!expected.contains(value)) {
            if (foreign++ == 0u && first.empty()) first = "byte " + Hex(word * 4u) + " of unit " + std::to_string(unit) + " holds " + Hex(value) + ", expected its previous " + Hex(Background(word, seed));
            continue;
        }
        seen.insert(value);
        if (UnitOf(0u, value & 0xfffu, (value >> 12u) & 0xfffu) != unit && misplaced++ == 0u && first.empty()) first = "a stored texel lies in unit " + std::to_string(unit);
    }
    const auto missing = expected.size() - seen.size();
    Expect(foreign == 0u && misplaced == 0u && missing == 0u, what + ": " + std::to_string(foreign) + " words are neither their previous bytes nor a stored texel, " + std::to_string(misplaced) + " stored texels lie in another unit and " + std::to_string(missing) + " of " + std::to_string(expected.size()) + " stored texels are missing; first " + first);
}

std::vector<std::uint32_t> Words(const PrivatePages& pages, std::uint64_t bytes) {
    AgcDriver::GuestMemory::FlushGpuWrites(pages.Address(), bytes);
    std::vector<std::uint32_t> words(static_cast<std::size_t>(bytes / 4u));
    for (std::size_t word = 0; word < words.size(); ++word) words[word] = pages.Words()[word];
    return words;
}

void ReaderInBatch() {
    const Surface surface;
    PrivatePages output(static_cast<std::uint32_t>(surface.Bytes() / PageBytes));
    PrivatePages scratch(1);
    Fill(surface, 1u);
    for (std::uint64_t word = 0; word < output.Bytes() / 4u; ++word) output.Words()[word] = 0xdeadbeefu;
    scratch.Words()[StoreLabelWord] = 0u;
    const std::string what = "a dispatch loading the image's memory through the page table right after a dispatch storing into a rectangle of its layer 0, in one submission";
    Commands commands;
    Store(commands, surface);
    Copy(commands, surface.Address(), surface.Bytes(), output);
    commands.WriteData(scratch.Address(StoreLabelWord * 4u), {1u});
    const auto failure = Submitted(commands, true);
    Expect(failure.empty(), what + ": the submission failed: " + failure);
    Expect(scratch.Words()[StoreLabelWord] == 1u, what + ": the label after the reader did not land");
    ExpectWords(Words(output, surface.Bytes()), 1u, what);
}

void CpuReadInFlight(std::uint32_t rounds) {
    const Surface surface;
    PrivatePages scratch(1);
    Fill(surface, 2u);
    scratch.Words()[SpinLabelWord] = 0u;
    scratch.Words()[StoreLabelWord] = 0u;
    const auto spinLabel = scratch.Address(SpinLabelWord * 4u);
    const auto storeLabel = scratch.Address(StoreLabelWord * 4u);
    const std::string what = "a CPU read of the image's memory while the batch holding the store into a rectangle of its layer 0 still runs";
    Commands commands;
    Spin(commands, scratch, rounds);
    commands.WriteData(spinLabel, {1u});
    Store(commands, surface);
    commands.WriteData(storeLabel, {1u});
    std::string failure;
    bool recorded = false;
    bool running = false;
    std::vector<std::uint32_t> read(static_cast<std::size_t>(surface.Bytes() / 4u));
    {
        NewPath::StderrCapture capture;
        try {
            commands.Submit(0);
            for (const auto deadline = Clock::now() + std::chrono::seconds(10); !recorded && Clock::now() < deadline;) {
                if (LabelPending(storeLabel) || scratch.Words()[StoreLabelWord] == 1u) {
                    recorded = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            running = scratch.Words()[SpinLabelWord] == 0u;
            AgcDriver::GuestMemory::Read(surface.Address(), std::as_writable_bytes(std::span(read)));
            NewPath::WaitIdle();
            NewPath::Settle();
        } catch (const std::exception& error) {
            failure = error.what();
        }
        const auto log = capture.Finish();
        if (log.find("failed") != std::string::npos) failure += "\n" + log;
    }
    Expect(failure.empty(), what + ": the submission or the read failed: " + failure);
    Expect(recorded, what + ": the store's label was never recorded");
    Expect(running, what + ": the batch had completed before the read, so the read did not meet a running batch");
    ExpectWords(read, 2u, what);
}

void FlushAfterCompletion() {
    const Surface surface;
    PrivatePages scratch(1);
    Fill(surface, 3u);
    scratch.Words()[StoreLabelWord] = 0u;
    const std::string what = "the image after the batch holding the store into a rectangle of its layer 0 completed";
    Commands commands;
    Store(commands, surface);
    commands.WriteData(scratch.Address(StoreLabelWord * 4u), {1u});
    const auto failure = Submitted(commands, true);
    Expect(failure.empty(), what + ": the submission failed: " + failure);
    Expect(StorageTexture::FindPending(surface.Address(), surface.Bytes()) != nullptr, what + ": the store left no image with pending results over the surface");
    const auto flush = [&](std::uint64_t offset, std::uint64_t bytes) {
        bool stored = false;
        NewPath::Locked([&] { stored = StorageTexture::FlushPending(surface.Address(offset), static_cast<std::size_t>(bytes), nullptr, "same batch image reader test"); });
        NewPath::Settle();
        return stored;
    };
    Expect(!flush(surface.LayerBytes() / 2u, surface.LayerBytes() / 2u), what + ": a flush of the units of layer 0 below the rectangle, which the store did not write, stored the image");
    Expect(!flush(surface.LayerBytes(), surface.LayerBytes()), what + ": a flush of layer 1, which the store did not write, stored the image");
    Expect(flush(0u, surface.LayerBytes() / 2u), what + ": a flush of the units the store wrote stored nothing");
    std::vector<std::uint32_t> read(static_cast<std::size_t>(surface.Bytes() / 4u));
    AgcDriver::GuestMemory::Read(surface.Address(), std::as_writable_bytes(std::span(read)));
    ExpectWords(read, 3u, what + ", read by the CPU");
}

}

#endif

int main() {
#ifdef _WIN32
    {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
    }
    try {
        ReaderInBatch();
    } catch (const std::exception& error) {
        Expect(false, std::string("reader in the batch: exception: ") + error.what());
    }
    try {
        PrivatePages scratch(1);
        const auto rounds = Calibrate(scratch);
        CpuReadInFlight(rounds);
    } catch (const std::exception& error) {
        Expect(false, std::string("CPU read in flight: exception: ") + error.what());
    }
    try {
        FlushAfterCompletion();
    } catch (const std::exception& error) {
        Expect(false, std::string("flush after completion: exception: ") + error.what());
    }
    try {
        AgcDriverShutdown_nid_postfix();
    } catch (const std::exception& error) {
        Expect(false, std::string("shutdown: ") + error.what());
    }
    return DeviceTable::Finish("same batch image reader");
#else
    std::printf("skipped, the guest arena is Windows-only here\n");
    return VulkanTestSkipped;
#endif
}
