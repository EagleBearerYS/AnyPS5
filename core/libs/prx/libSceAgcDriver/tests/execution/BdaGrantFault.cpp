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
constexpr std::uint32_t StoredBase = 0x6b000000u;
constexpr std::uint32_t CorrectBefore = 20;
constexpr std::uint32_t CorrectAfter = 20;

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

const Program& ReadOnlyStoreProgram() {
    static const Program program = [] {
        using namespace Isa;
        Program built;
        built << Vop2(VLshlRevB32, 1, InlineInteger(2), 0)
              << Vop2(VAddNcU32, 2, Literal, 0) << StoredBase
              << Global(GlobalStoreDword, 0, 1, 2, 0)
              << EndProgram;
        return built;
    }();
    return program;
}

std::uint32_t Seed(std::uint32_t dispatch) {
    return 0x51000000u | (dispatch << 8u);
}

std::size_t Count(const std::string& text, const std::string& what) {
    std::size_t count = 0;
    for (auto at = text.find(what); at != std::string::npos; at = text.find(what, at + 1u)) ++count;
    return count;
}

std::string ProgramTag(const Program& program) {
    return "(program " + Hex(program.Address()) + ")";
}

std::string failure;

void StoreOutsideGrants() {
    auto& output = *new PrivatePages(1);
    auto& readOnly = *new PrivatePages(1, false);
    for (std::uint32_t word = 0; word < WordsPerPage; ++word) output.Words()[word] = Untouched;
    for (std::uint32_t word = 0; word < Lanes; ++word) readOnly.Words()[word] = Untouched;
    Commands commands;
    const auto correct = [&](std::uint32_t dispatch) {
        const auto buffer = BufferResource(output.Address(dispatch * SliceWords * 4u), SliceWords * 4u);
        const std::array<std::uint32_t, 5> userData{buffer[0], buffer[1], buffer[2], buffer[3], Seed(dispatch)};
        commands.Dispatch(SliceStoreProgram(), userData, Lanes);
    };
    for (std::uint32_t dispatch = 0; dispatch < CorrectBefore; ++dispatch) correct(dispatch);
    const std::array<std::uint32_t, 2> faulting{Low(readOnly.Address()), High(readOnly.Address())};
    commands.Dispatch(ReadOnlyStoreProgram(), faulting, Lanes);
    for (std::uint32_t dispatch = CorrectBefore; dispatch < CorrectBefore + CorrectAfter; ++dispatch) correct(dispatch);
    std::string log;
    {
        StderrCapture capture;
        try {
            commands.Submit(0);
            WaitIdle();
        } catch (const std::exception& error) {
            failure = error.what();
        }
        log = capture.Finish();
    }
    Expect(!failure.empty(), "store outside the write grants: a GLOBAL store to a page the guest maps without CPU write access, among " + std::to_string(CorrectBefore + CorrectAfter) + " correct dispatches of one batch, reported no failure");
    Expect(Count(failure, "BDA access failed") == 1u && failure.find("reason=2") != std::string::npos, "store outside the write grants: the failure is not one Permission fault: " + failure);
    Expect(failure.find(ProgramTag(ReadOnlyStoreProgram())) != std::string::npos, "store outside the write grants: the fault does not name the storing program " + ProgramTag(ReadOnlyStoreProgram()) + ": " + failure);
    Expect(Count(failure + log, ProgramTag(SliceStoreProgram())) == 0u, "store outside the write grants: a fault was reported for the correct dispatches' program:\n" + failure + "\n" + log);
    const auto at = failure.find("address=0x");
    const auto faulted = at != std::string::npos ? std::strtoull(failure.c_str() + at + 10, nullptr, 16) : 0ull;
    Expect(faulted >= readOnly.Address() && faulted < readOnly.Address() + Lanes * 4u, "store outside the write grants: the fault does not name an address the program stored to: " + failure);
    for (std::uint32_t word = 0; word < Lanes; ++word) Expect(readOnly.Words()[word] == Untouched, "store outside the write grants: the faulting store landed at word " + std::to_string(word));
    std::size_t wrong = 0;
    std::string first;
    for (std::uint32_t word = 0; word < WordsPerPage; ++word) {
        const auto dispatch = word / SliceWords;
        const auto expected = dispatch < CorrectBefore + CorrectAfter ? Seed(dispatch) + word % SliceWords : Untouched;
        const auto actual = output.Words()[word];
        if (actual == expected) continue;
        if (wrong++ == 0u) first = "word " + std::to_string(word) + " (correct dispatch " + std::to_string(dispatch) + ") is " + Hex(actual) + ", expected " + Hex(expected);
    }
    Expect(wrong == 0u, "store outside the write grants: the correct dispatches of the batch lost results: " + std::to_string(wrong) + " words differ, first " + first);
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
        StoreOutsideGrants();
    } catch (const std::exception& error) {
        Expect(false, std::string("exception: ") + error.what());
    }
    try {
        AgcDriverShutdown_nid_postfix();
    } catch (const std::exception& error) {
        Expect(std::string(error.what()) == failure, std::string("shutdown reported another failure: ") + error.what());
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d BDA grant fault checks failed\n", failures);
        return 1;
    }
    std::printf("BDA grant fault tests passed\n");
    return 0;
#else
    std::printf("skipped, commit imports are Windows-only\n");
    return VulkanTestSkipped;
#endif
}
