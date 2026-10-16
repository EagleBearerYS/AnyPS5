#include "NewPathFixture.hpp"
#include "VulkanTestDevice.hpp"
#include <exception>
#include <string>
#include <vector>

#ifdef _WIN32

namespace {

using namespace NewPath;

constexpr std::uint32_t Lanes = 64;
constexpr std::uint32_t BufferPages = MirrorReader::Pages;
constexpr std::uint64_t BufferBytes = std::uint64_t{BufferPages} * PageBytes;
constexpr std::uint32_t ElementBytes = 16;
constexpr std::uint32_t Rounds = 6;

int failures = 0;

void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

namespace Ops {

using namespace Isa;

constexpr std::uint32_t VMadU32U24 = 0x143u;
constexpr std::uint32_t BufferStoreDwordX4 = 0x1eu;

constexpr std::array<std::uint32_t, 2> Vop3(std::uint32_t opcode, std::uint32_t vdst, std::uint32_t src0, std::uint32_t src1, std::uint32_t src2) {
    return {0xd4000000u | (opcode << 16u) | vdst, src0 | (src1 << 9u) | (src2 << 18u)};
}

constexpr std::array<std::uint32_t, 2> Indexed(std::uint32_t opcode, std::uint32_t vdata, std::uint32_t vindex, std::uint32_t resource) {
    return {0xe0000000u | (opcode << 18u) | 0x2000u, (ZeroOffset << 24u) | ((resource / 4u) << 16u) | (vdata << 8u) | vindex};
}

}

const Program& FillProgram() {
    static const Program program = [] {
        using namespace Ops;
        Program built;
        built << Vop3(VMadU32U24, 4, 8, InlineInteger(64), Vgpr(0))
              << Smem(SLoadDwordX4, 12, 4, 0)
              << WaitAll
              << Vop1(VMovB32, 0, 12)
              << Vop1(VMovB32, 1, 13)
              << Vop1(VMovB32, 2, 14)
              << Vop1(VMovB32, 3, 15)
              << Indexed(BufferStoreDwordX4, 0, 4, 0)
              << EndProgram;
        return built;
    }();
    return program;
}

std::uint32_t Initial(std::uint64_t word) {
    return 0x5e000000u | static_cast<std::uint32_t>(word & 0x00ffffffu);
}

std::uint32_t ValueWord(std::uint32_t round, std::uint32_t word) {
    return 0x80000000u | (round << 8u) | word;
}

std::string Submitted(Commands& commands) {
    std::string failure;
    StderrCapture capture;
    try {
        commands.Submit(0);
        WaitIdle();
        Settle();
    } catch (const std::exception& error) {
        failure = error.what();
    }
    const auto log = capture.Finish();
    if (log.find("failed") != std::string::npos) failure += "\n" + log;
    return failure;
}

void StoreThenRead(std::uint32_t stride) {
    SharedPages buffer(BufferPages);
    PrivatePages values(1);
    MirrorReader reader;
    for (std::uint64_t word = 0; word < BufferBytes / 4u; ++word) buffer.Words()[word] = Initial(word);
    for (std::uint32_t round = 0; round < Rounds; ++round) {
        for (std::uint32_t word = 0; word < 4u; ++word) values.Words()[round * 4u + word] = ValueWord(round, word);
    }
    AgcDriver::GuestMemory::BumpCollectEpoch();
    const auto records = static_cast<std::uint32_t>(BufferBytes / stride);
    const auto groups = records / Lanes;
    const std::string shape = "a dispatch storing a 16-byte value every " + std::to_string(stride) + " bytes of a " + std::to_string(BufferPages) + "-page shared mapping, a barrier, then a dispatch reading one word from every 4 KiB of the mapping, in one submission";
    for (std::uint32_t round = 0; round < Rounds; ++round) {
        const std::string what = shape + ", round " + std::to_string(round);
        Commands commands;
        const auto resource = BufferResource(buffer.Address(), records, stride);
        const auto value = values.Address(std::uint64_t{round} * 16u);
        const std::array<std::uint32_t, 8> userData{resource[0], resource[1], resource[2], resource[3], Low(value), High(value), 0u, 0u};
        commands.Dispatch(FillProgram(), userData, Lanes, groups);
        commands.Barrier();
        reader.Record(commands, buffer.Address(), BufferPages);
        const auto failure = Submitted(commands);
        Expect(failure.empty(), what + ": the submission failed: " + failure);
        const auto lanes = reader.Collect();
        std::size_t wrong = 0;
        std::string pages;
        std::string first;
        for (std::uint32_t lane = 0; lane < MirrorReader::Lanes; ++lane) {
            if (lanes[lane] == ValueWord(round, 0)) continue;
            const auto page = std::to_string(lane / SubpagesPerPage);
            if (pages.empty() || pages.substr(pages.rfind(' ') + 1u) != page) pages += " " + page;
            if (wrong++ == 0u) first = "the lane reading byte " + Hex(std::uint64_t{lane} * 4096u) + " read " + Hex(lanes[lane]) + ", expected " + Hex(ValueWord(round, 0));
        }
        Expect(wrong == 0u, what + ": " + std::to_string(wrong) + " of " + std::to_string(MirrorReader::Lanes) + " reading lanes did not read the value just stored (pages" + pages + "), first " + first);
    }
    AgcDriver::GuestMemory::FlushGpuWrites(buffer.Address(), BufferBytes);
    std::size_t wrong = 0;
    std::string first;
    for (std::uint64_t word = 0; word < BufferBytes / 4u; ++word) {
        const auto within = word * 4u % stride;
        const auto expected = within < ElementBytes ? ValueWord(Rounds - 1u, static_cast<std::uint32_t>(within / 4u)) : Initial(word);
        const auto actual = buffer.Words()[word];
        if (actual == expected || wrong++ != 0u) continue;
        first = "byte " + Hex(word * 4u) + " holds " + Hex(actual) + ", expected " + Hex(expected);
    }
    Expect(wrong == 0u, shape + ": after the last round " + std::to_string(wrong) + " words of the mapping differ, first " + first);
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
        {
            MirrorReader reader;
            PrivatePages warm(1);
            reader.Read(warm.Address(), 1);
        }
        StoreThenRead(ElementBytes);
        StoreThenRead(2u * ElementBytes);
    } catch (const std::exception& error) {
        Expect(false, std::string("exception: ") + error.what());
    }
    try {
        AgcDriverShutdown_nid_postfix();
    } catch (const std::exception& error) {
        Expect(false, std::string("shutdown: ") + error.what());
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d mirror stale read checks failed\n", failures);
        return 1;
    }
    std::printf("mirror stale read tests passed\n");
    return 0;
#else
    std::printf("skipped, the guest arena is Windows-only here\n");
    return VulkanTestSkipped;
#endif
}
