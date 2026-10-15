#include "NewPathFixture.hpp"
#include "VulkanTestDevice.hpp"
#include <exception>
#include <stdexcept>

#ifdef _WIN32

namespace {

using namespace NewPath;

constexpr std::uint32_t Lanes = 16;
constexpr std::uint32_t SliceWords = Lanes;
constexpr std::uint32_t Untouched = 0xdeadbeefu;
constexpr std::uint32_t SlabDispatches = 800;
constexpr std::uint32_t WrittenRegionPages = 8;
constexpr std::uint32_t PagesPerBlock = SubpagesPerPage;
constexpr std::uint32_t PageStrideShift = 8;

int failures = 0;

void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

const Program& SliceStoreProgram() {
    static const Program program = [] {
        using namespace Isa;
        Program built;
        built << Vop2(VLshlRevB32, 2, InlineInteger(2), 0)
              << Vop2(VAddNcU32, 1, 4, 0)
              << Mubuf(BufferStoreDword, 1, 2, 0)
              << EndProgram;
        return built;
    }();
    return program;
}

const Program& PageStoreProgram() {
    static const Program program = [] {
        using namespace Isa;
        Program built;
        built << Vop2(VLshlRevB32, 2, InlineInteger(PageStrideShift), 0)
              << Vop2(VAddNcU32, 1, 4, 0)
              << Mubuf(BufferStoreDword, 1, 2, 0)
              << EndProgram;
        return built;
    }();
    return program;
}

const Program& CopyProgram() {
    static const Program program = [] {
        using namespace Isa;
        Program built;
        built << Vop2(VLshlRevB32, 1, InlineInteger(2), 0)
              << Mubuf(BufferLoadDword, 2, 1, 0)
              << WaitAll
              << Mubuf(BufferStoreDword, 2, 1, 4)
              << EndProgram;
        return built;
    }();
    return program;
}

const Program& UnboundedCopyProgram() {
    static const Program program = [] {
        using namespace Isa;
        Program built;
        built << Vop1(VMovB32, 15, InlineInteger(0))
              << Mubuf(0x0eu, 16, 15, 0)
              << WaitAll;
        for (std::uint32_t word = 0; word < 4; ++word) built << Vop1(VReadFirstLaneB32, 8 + word, Vgpr(16 + word));
        built << Vop2(VLshlRevB32, 1, InlineInteger(2), 0)
              << Mubuf(BufferLoadDword, 2, 1, 8)
              << WaitAll
              << Mubuf(BufferStoreDword, 2, 1, 4)
              << EndProgram;
        return built;
    }();
    return program;
}

std::array<std::uint32_t, 5> StoreUserData(std::uint64_t address, std::uint32_t bytes, std::uint32_t seed) {
    const auto buffer = BufferResource(address, bytes);
    return {buffer[0], buffer[1], buffer[2], buffer[3], seed};
}

template<std::size_t N>
std::array<std::uint32_t, 4 * N> Resources(const std::array<std::pair<std::uint64_t, std::uint32_t>, N>& buffers) {
    std::array<std::uint32_t, 4 * N> userData{};
    for (std::size_t index = 0; index < N; ++index) {
        const auto buffer = BufferResource(buffers[index].first, buffers[index].second);
        std::copy(buffer.begin(), buffer.end(), userData.begin() + 4 * index);
    }
    return userData;
}

std::string Submitted(Commands& commands) {
    std::string failure;
    StderrCapture capture;
    try {
        commands.Submit(0);
        WaitIdle();
    } catch (const std::exception& error) {
        failure = error.what();
    }
    const auto log = capture.Finish();
    if (log.find("BDA access failed") != std::string::npos) failure += "\n" + log;
    return failure;
}

std::uint32_t SlabSeed(std::uint32_t round, std::uint32_t dispatch) {
    return (round << 28u) | (dispatch << 8u);
}

void ManyDispatchesPerBatch() {
    PrivatePages output(1);
    for (std::uint32_t word = 0; word < WordsPerPage; ++word) output.Words()[word] = Untouched;
    for (std::uint32_t round = 1; round <= 2; ++round) {
        Commands commands;
        for (std::uint32_t dispatch = 0; dispatch < SlabDispatches; ++dispatch) commands.Dispatch(SliceStoreProgram(), StoreUserData(output.Address(dispatch * SliceWords * 4u), SliceWords * 4u, SlabSeed(round, dispatch)), Lanes);
        const auto failure = Submitted(commands);
        const auto what = std::string("many dispatches per batch, submission ") + std::to_string(round) + " of " + std::to_string(SlabDispatches) + " dispatches";
        Expect(failure.empty(), what + ": the submission reported a failure: " + failure);
        AgcDriver::GuestMemory::FlushGpuWrites(output.Address(), output.Bytes());
        std::size_t wrong = 0;
        std::string first;
        for (std::uint32_t word = 0; word < WordsPerPage; ++word) {
            const auto dispatch = word / SliceWords;
            const auto expected = dispatch < SlabDispatches ? SlabSeed(round, dispatch) + word % SliceWords : Untouched;
            const auto actual = output.Words()[word];
            if (actual == expected) continue;
            if (wrong++ == 0u) first = "word " + std::to_string(word) + " (dispatch " + std::to_string(dispatch) + ") is " + Hex(actual) + ", expected " + Hex(expected);
        }
        Expect(wrong == 0u, what + ": " + std::to_string(wrong) + " words differ, first " + first);
    }
}

bool PageWritten(std::uint32_t page) {
    return (page / PagesPerBlock) % 2u == 0u && (page % PagesPerBlock) % 3u == 0u;
}

std::uint32_t CpuWord(std::uint32_t word) {
    return 0x3c000000u | word;
}

std::uint32_t PageSeed(std::uint32_t page) {
    return 0x9a000000u | (page << 12u);
}

void WrittenPages() {
    PrivatePages region(WrittenRegionPages);
    const auto words = static_cast<std::uint32_t>(region.Bytes() / 4u);
    for (std::uint32_t word = 0; word < words; ++word) region.Words()[word] = CpuWord(word);
    Expect(AgcDriver::GuestMemory::Watched(region.Address(), region.Bytes()), "written pages: committed guest memory is not write-watched");
    AgcDriver::GuestMemory::CollectWritesUncached(region.Address(), region.Bytes());
    const auto generation = AgcDriver::GuestMemory::TrackerGeneration();
    const auto pages = WrittenRegionPages * PagesPerBlock;
    std::uint32_t written = 0;
    Commands commands;
    for (std::uint32_t page = 0; page < pages; ++page) {
        if (!PageWritten(page)) continue;
        ++written;
        commands.Dispatch(PageStoreProgram(), StoreUserData(region.Address(std::uint64_t{page} * SubpageBytes), static_cast<std::uint32_t>(SubpageBytes), PageSeed(page)), Lanes);
    }
    const auto failure = Submitted(commands);
    Expect(failure.empty(), "written pages: the submission of " + std::to_string(written) + " page stores reported a failure: " + failure);
    AgcDriver::GuestMemory::FlushGpuWrites(region.Address(), region.Bytes());
    for (std::uint32_t page = 0; page < pages; ++page) {
        std::size_t wrong = 0;
        std::string first;
        for (std::uint32_t offset = 0; offset < WordsPerSubpage; ++offset) {
            const auto word = page * WordsPerSubpage + offset;
            const auto lane = offset >> (PageStrideShift - 2u);
            const bool stored = PageWritten(page) && (offset & ((1u << (PageStrideShift - 2u)) - 1u)) == 0u && lane < Lanes;
            const auto expected = stored ? PageSeed(page) + lane : CpuWord(word);
            const auto actual = region.Words()[word];
            if (actual == expected) continue;
            if (wrong++ == 0u) first = "word " + std::to_string(offset) + " is " + Hex(actual) + ", expected " + Hex(expected);
        }
        Expect(wrong == 0u, "written pages: 4 KiB page " + std::to_string(page) + (PageWritten(page) ? " (stored by a dispatch)" : " (no dispatch stored to it)") + ": " + std::to_string(wrong) + " words differ, first " + first);
    }
    std::array<std::uint64_t, WrittenRegionPages> generations{};
    generations.fill(generation);
    std::array<std::uint8_t, WrittenRegionPages> changed{};
    std::array<std::uint8_t, WrittenRegionPages> cpu{};
    AgcDriver::GuestMemory::CollectWritesUncached(region.Address(), region.Bytes());
    AgcDriver::GuestMemory::ChangedBlocks(region.Address(), region.Bytes(), generations, changed, cpu);
    for (std::uint32_t block = 0; block < WrittenRegionPages; ++block) {
        const bool stored = block % 2u == 0u;
        if (stored) Expect(changed[block] == AgcDriver::GuestMemory::BlockWritten && cpu[block] == 0u, "written pages: 64 KiB block " + std::to_string(block) + ", whose pages the dispatches stored to, was not marked written by the GPU at completion");
        else Expect(changed[block] == AgcDriver::GuestMemory::BlockUnchanged, "written pages: 64 KiB block " + std::to_string(block) + ", which no dispatch stored to, was marked written");
    }
}

std::uint32_t LabelWord(std::uint32_t word) {
    return 0x22000000u | word;
}

void CompleteLabel(std::uint64_t address, std::uint32_t value, std::uint64_t stamp) {
    Locked([&] {
        auto* recorder = AgcDriver::Graphics::Recorder::Active();
        Require(recorder != nullptr, "completion labels: the device has no recorder");
        std::array<std::byte, 4> bytes{};
        std::memcpy(bytes.data(), &value, sizeof(value));
        recorder->NotePendingWrite(address, bytes.size());
        recorder->AfterCompletions(address, bytes, stamp, 0, false);
        recorder->Submit();
    });
}

void ExpectCopied(const PrivatePages& output, std::uint32_t slot, const std::function<std::uint32_t(std::uint32_t lane)>& expected, const std::string& what) {
    AgcDriver::GuestMemory::FlushGpuWrites(output.Address(), output.Bytes());
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
        const auto actual = output.Words()[slot * SliceWords + lane];
        Expect(actual == expected(lane), what + ": lane " + std::to_string(lane) + " read " + Hex(actual) + ", expected " + Hex(expected(lane)));
    }
}

void CompletionLabels() {
    constexpr std::uint32_t covered = 0;
    constexpr std::uint32_t unrelated = 1024;
    constexpr std::uint32_t unbounded = 2048;
    constexpr std::array<std::uint32_t, 3> values{0xc0de0001u, 0xc0de0002u, 0xc0de0003u};
    PrivatePages labels(1);
    PrivatePages other(1);
    PrivatePages table(1);
    PrivatePages output(1);
    for (std::uint32_t word = 0; word < WordsPerPage; ++word) {
        labels.Words()[word] = LabelWord(word);
        other.Words()[word] = 0x55000000u | word;
        output.Words()[word] = Untouched;
    }
    const auto descriptor = BufferResource(labels.Address(unbounded * 4u), SliceWords * 4u);
    for (std::uint32_t word = 0; word < 4; ++word) table.Words()[word] = descriptor[word];

    CompleteLabel(labels.Address(covered * 4u), values[0], std::uint64_t{1} << 40u);
    {
        Commands commands;
        commands.Dispatch(CopyProgram(), Resources<2>({{{labels.Address(covered * 4u), SliceWords * 4u}, {output.Address(0), SliceWords * 4u}}}), Lanes);
        const auto failure = Submitted(commands);
        Expect(failure.empty(), "completion label in the read range: the submission reported a failure: " + failure);
        ExpectCopied(output, 0, [&](std::uint32_t lane) { return lane == 0u ? values[0] : LabelWord(covered + lane); }, "completion label in the read range: a dispatch whose V# covers a label stored at its batch's completion");
    }

    CompleteLabel(labels.Address(unrelated * 4u), values[1], (std::uint64_t{1} << 40u) + 1u);
    {
        Commands commands;
        commands.Dispatch(CopyProgram(), Resources<2>({{{other.Address(0), SliceWords * 4u}, {output.Address(SliceWords * 4u), SliceWords * 4u}}}), Lanes);
        const auto failure = Submitted(commands);
        Expect(failure.empty(), "completion label outside the read range: the submission reported a failure: " + failure);
        ExpectCopied(output, 1, [](std::uint32_t lane) { return 0x55000000u | lane; }, "completion label outside the read range: a dispatch that reads unrelated memory");
    }

    CompleteLabel(labels.Address(unbounded * 4u), values[2], (std::uint64_t{1} << 40u) + 2u);
    {
        Commands commands;
        commands.Dispatch(UnboundedCopyProgram(), Resources<2>({{{table.Address(0), 16u}, {output.Address(2u * SliceWords * 4u), SliceWords * 4u}}}), Lanes);
        const auto failure = Submitted(commands);
        Expect(failure.empty(), "completion label read by an unbounded dispatch: the submission reported a failure: " + failure);
        ExpectCopied(output, 2, [&](std::uint32_t lane) { return lane == 0u ? values[2] : LabelWord(unbounded + lane); }, "completion label read by an unbounded dispatch: a dispatch whose V# comes from a vector load reads a label stored at its batch's completion");
    }

    Settle();
    AgcDriver::GuestMemory::FlushGpuWrites(labels.Address(), labels.Bytes());
    const std::array<std::uint32_t, 3> words{covered, unrelated, unbounded};
    for (std::size_t index = 0; index < words.size(); ++index) {
        const auto stored = labels.Words()[words[index]];
        Expect(stored == values[index], "completion labels: the label at word " + std::to_string(words[index]) + " holds " + Hex(stored) + " after its batch completed, expected " + Hex(values[index]));
    }
}

}

#endif

int main() {
#ifdef _WIN32
    {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
    }
    const std::array<std::pair<const char*, void (*)()>, 3> cases{{
        {"many dispatches per batch", ManyDispatchesPerBatch},
        {"written pages", WrittenPages},
        {"completion labels", CompletionLabels},
    }};
    for (const auto& [name, run] : cases) {
        try {
            run();
        } catch (const std::exception& error) {
            Expect(false, std::string(name) + ": exception: " + error.what());
        }
    }
    try {
        AgcDriverShutdown_nid_postfix();
    } catch (const std::exception& error) {
        Expect(false, std::string("shutdown: ") + error.what());
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d BDA batch checks failed\n", failures);
        return 1;
    }
    std::printf("BDA batch tests passed\n");
    return 0;
#else
    std::printf("skipped, commit imports are Windows-only\n");
    return VulkanTestSkipped;
#endif
}
