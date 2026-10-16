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
constexpr std::uint32_t QueueA = 0x20;
constexpr std::uint32_t QueueB = 0x28;
constexpr std::uint32_t Equal = 3;
constexpr auto LabelLimit = std::chrono::seconds(10);

std::array<std::uint32_t, 8> DescriptorAt(std::uint64_t address) {
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

std::uint64_t SurfaceBytes() {
    return Graphics::DescribeSurface(Graphics::DecodeTextureResource(DescriptorAt(PageBytes))).guestBytes;
}

std::uint32_t Previous(std::uint64_t word) {
    return 0x16000000u | static_cast<std::uint32_t>(word & 0x00ffffffu);
}

std::uint32_t CpuWord(std::uint64_t word) {
    return 0x29000000u | static_cast<std::uint32_t>(word & 0x00ffffffu);
}

std::uint32_t UnitOf(std::uint32_t x, std::uint32_t y) {
    return (y / 128u) * 2u + x / 128u;
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

TestGuest::Array<std::uint32_t, DeviceTable::SrtWords> StoreSrt;

class Image {
public:
    Image() : bytes(SurfaceBytes()), pages(static_cast<std::uint32_t>((bytes + PageBytes - 1u) / PageBytes)), output(static_cast<std::uint32_t>((bytes + PageBytes - 1u) / PageBytes)), labels(1) {
        Require(bytes == 2u * 4u * PageBytes, "storage shadow ordering: the surface is not two layers of four 64 KiB units");
    }

    void Reset() {
        expected.assign(static_cast<std::size_t>(bytes / 4u), 0u);
        for (std::size_t word = 0; word < expected.size(); ++word) {
            pages.Words()[word] = Previous(word);
            expected[word] = Previous(word);
        }
        for (std::uint64_t word = 0; word < output.Bytes() / 4u; ++word) output.Words()[word] = 0xdeadbeefu;
        for (std::uint32_t word = 0; word < 64u; ++word) labels.Words()[word] = 0u;
        AgcDriver::GuestMemory::BumpCollectEpoch();
    }

    void Store(Commands& commands) {
        std::fill(StoreSrt.begin(), StoreSrt.end(), 0u);
        const auto image = DescriptorAt(pages.Address());
        std::copy(image.begin(), image.end(), StoreSrt.begin() + DeviceTable::ImageSlot);
        AgcDriver::GuestMemory::BumpCollectEpoch();
        const auto srt = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(StoreSrt.data()));
        const std::array<std::uint32_t, 2> userData{Low(srt), High(srt)};
        commands.Dispatch(StoreProgram(), userData, Lanes, Rows);
    }

    void Copy(Commands& commands) {
        const auto from = NewPath::BufferResource(pages.Address(), static_cast<std::uint32_t>(bytes));
        const auto to = NewPath::BufferResource(output.Address(), static_cast<std::uint32_t>(bytes));
        const std::array<std::uint32_t, 8> userData{from[0], from[1], from[2], from[3], to[0], to[1], to[2], to[3]};
        commands.Dispatch(CopyProgram(), userData, CopyLanes, static_cast<std::uint32_t>(bytes / 4u / CopyLanes));
    }

    void CpuWrite() {
        for (std::size_t word = 3; word < expected.size(); word += 7u) {
            pages.Words()[word] = CpuWord(word);
            expected[word] = CpuWord(word);
        }
    }

    std::uint64_t Label(std::uint32_t index) const { return labels.Address(index * 64u); }
    volatile std::uint32_t* LabelWord(std::uint32_t index) const { return labels.Words() + index * 16u; }

    bool AwaitLabel(std::uint32_t index) const {
        for (const auto deadline = Clock::now() + LabelLimit; Clock::now() < deadline;) {
            if (LabelWord(index)[0] == 1u) return true;
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        return false;
    }

    std::vector<std::uint32_t> ReadThroughHook() const {
        std::vector<std::uint32_t> read(static_cast<std::size_t>(bytes / 4u));
        AgcDriver::GuestMemory::Read(pages.Address(), std::as_writable_bytes(std::span(read)));
        return read;
    }

    std::vector<std::uint32_t> Output() const {
        AgcDriver::GuestMemory::FlushGpuWrites(output.Address(), output.Bytes());
        std::vector<std::uint32_t> read(static_cast<std::size_t>(bytes / 4u));
        for (std::size_t word = 0; word < read.size(); ++word) read[word] = output.Words()[word];
        return read;
    }

    void Check(std::span<const std::uint32_t> words, const std::string& what) const {
        std::set<std::uint32_t> stored;
        for (std::uint32_t y = Top; y < Top + Rows; ++y) {
            for (std::uint32_t x = Left; x < Left + Lanes; ++x) stored.insert(RectMark | (y << 12u) | x);
        }
        std::set<std::uint32_t> seen;
        std::size_t foreign = 0;
        std::size_t misplaced = 0;
        std::string first;
        for (std::size_t word = 0; word < words.size(); ++word) {
            const auto value = words[word];
            if (value == expected[word]) continue;
            const auto unit = static_cast<std::uint32_t>(word * 4u / PageBytes);
            if (!stored.contains(value)) {
                if (foreign++ == 0u && first.empty()) first = "byte " + Hex(word * 4u) + " of unit " + std::to_string(unit) + " holds " + Hex(value) + ", expected " + Hex(expected[word]);
                continue;
            }
            seen.insert(value);
            if (UnitOf(value & 0xfffu, (value >> 12u) & 0xfffu) != unit && misplaced++ == 0u && first.empty()) first = "a stored texel lies in unit " + std::to_string(unit);
        }
        const auto missing = stored.size() - seen.size();
        Expect(foreign == 0u && misplaced == 0u && missing == 0u, what + ": " + std::to_string(foreign) + " words are neither the bytes expected there nor a stored texel, " + std::to_string(misplaced) + " stored texels lie in another unit and " + std::to_string(missing) + " of " + std::to_string(stored.size()) + " stored texels are missing; first " + first);
    }

private:
    std::uint64_t bytes;
    PrivatePages pages;
    PrivatePages output;
    PrivatePages labels;
    std::vector<std::uint32_t> expected;
};

std::string Settled() {
    std::string failure;
    try {
        NewPath::WaitIdle();
        NewPath::Settle();
    } catch (const std::exception& error) {
        failure = error.what();
    }
    return failure;
}

const std::string Shape = "a dispatch storing into rows 40-87 and columns 96-159 of layer 0 of a 64 KiB tiled 256x256 array of two layers in host-imported memory";

void ReadInLaterSubmission(Image& image) {
    image.Reset();
    const std::string what = Shape + ", then a dispatch of a later submission loading the image's memory through the page table";
    Commands store;
    image.Store(store);
    store.WriteData(image.Label(0), {1u});
    store.Submit(QueueA);
    Expect(image.AwaitLabel(0), what + ": the store's label never landed");
    Commands reader;
    image.Copy(reader);
    reader.WriteData(image.Label(1), {1u});
    reader.Submit(QueueA);
    Expect(image.AwaitLabel(1), what + ": the reader's label never landed");
    const auto failure = Settled();
    Expect(failure.empty(), what + ": a submission failed: " + failure);
    image.Check(image.Output(), what);
}

void ReadAtLabel(Image& image) {
    image.Reset();
    const std::string what = Shape + ", read by the CPU through the flush hook once the label after it landed";
    Commands store;
    image.Store(store);
    store.WriteData(image.Label(0), {1u});
    store.Submit(QueueA);
    Expect(image.AwaitLabel(0), what + ": the store's label never landed");
    image.Check(image.ReadThroughHook(), what);
    const auto failure = Settled();
    Expect(failure.empty(), what + ": the submission failed: " + failure);
}

void CpuWritesBeforeExecution(Image& image) {
    image.Reset();
    const std::string what = Shape + ", waiting on a CPU-written label, while the CPU writes every seventh word of the image's memory before writing the label";
    Commands store;
    store.Wait(image.Label(1), Equal, 1u, 0xffffffffu);
    image.Store(store);
    store.WriteData(image.Label(0), {1u});
    store.Submit(QueueA);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    image.CpuWrite();
    image.LabelWord(1)[0] = 1u;
    Expect(image.AwaitLabel(0), what + ": the store's label never landed");
    image.Check(image.ReadThroughHook(), what + ", read by the CPU");
    Commands reader;
    image.Copy(reader);
    reader.WriteData(image.Label(2), {1u});
    reader.Submit(QueueA);
    Expect(image.AwaitLabel(2), what + ": the reader's label never landed");
    const auto failure = Settled();
    Expect(failure.empty(), what + ": a submission failed: " + failure);
    image.Check(image.Output(), what + ", read by a dispatch through the page table");
}

void ReadOnOtherStream(Image& image) {
    image.Reset();
    const std::string what = Shape + " on one queue, read through the page table by a dispatch on another queue that waits on the store's label";
    Commands reader;
    reader.Wait(image.Label(0), Equal, 1u, 0xffffffffu);
    image.Copy(reader);
    reader.WriteData(image.Label(1), {1u});
    reader.Submit(QueueB);
    Commands store;
    image.Store(store);
    store.WriteData(image.Label(0), {1u});
    store.Submit(QueueA);
    Expect(image.AwaitLabel(1), what + ": the reader's label never landed");
    const auto failure = Settled();
    Expect(failure.empty(), what + ": the submissions failed: " + failure);
    image.Check(image.Output(), what);
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
        Image image;
        ReadInLaterSubmission(image);
        ReadAtLabel(image);
        CpuWritesBeforeExecution(image);
        ReadOnOtherStream(image);
    } catch (const std::exception& error) {
        Expect(false, std::string("exception: ") + error.what());
    }
    try {
        AgcDriverShutdown_nid_postfix();
    } catch (const std::exception& error) {
        Expect(false, std::string("shutdown: ") + error.what());
    }
    return DeviceTable::Finish("storage shadow ordering");
#else
    std::printf("skipped, the guest arena is Windows-only here\n");
    return VulkanTestSkipped;
#endif
}
