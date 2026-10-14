#include "NewPathFixture.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include <cstdlib>
#include <exception>
#include <stdexcept>

#ifdef _WIN32

namespace {

using namespace NewPath;
using ShaderRecompiler::ShaderStage;

constexpr std::uint32_t Lanes = 16;
constexpr std::uint32_t CrossingStart = static_cast<std::uint32_t>(PageBytes) - 32u;
constexpr std::uint32_t StoredBase = 0x6b000000u;
constexpr std::uint32_t Untouched = 0x11111111u;

int failures = 0;

void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

Program CrossingProgram;
Program StoreProgram;
Program ReadOnlyStoreProgram;

void BuildPrograms() {
    using namespace Isa;
    CrossingProgram << Vop2(VLshlRevB32, 1, InlineInteger(2), 0)
                    << Vop2(VAddNcU32, 1, Literal, 1) << CrossingStart
                    << Mubuf(0x0eu, 10, 1, 0)
                    << Global(0x0eu, 14, 1, 0, 4)
                    << WaitAll
                    << Vop2(VLshlRevB32, 3, InlineInteger(5), 0)
                    << Mubuf(0x1eu, 10, 3, 8)
                    << Mubuf(0x1eu, 14, 3, 8, false, 16)
                    << EndProgram;
    StoreProgram << Vop2(VLshlRevB32, 1, InlineInteger(12), 0)
                 << Vop2(VAddNcU32, 2, Literal, 0) << StoredBase
                 << Mubuf(BufferStoreDword, 2, 1, 0)
                 << Global(GlobalStoreDword, 0, 1, 2, 4)
                 << EndProgram;
    ReadOnlyStoreProgram << Vop2(VLshlRevB32, 1, InlineInteger(2), 0)
                         << Vop2(VAddNcU32, 2, Literal, 0) << StoredBase
                         << Global(GlobalStoreDword, 0, 1, 2, 0)
                         << EndProgram;
}

void Dispatch(AgcDriver::VulkanDevice& device, const Program& program, std::span<const std::uint32_t> userData, std::uint32_t threads) {
    const auto code = program.Code();
    const std::array<ShaderRecompiler::MemoryRegion, 1> regions{{{static_cast<std::uintptr_t>(program.Address()), std::as_bytes(code)}}};
    const ShaderRecompiler::ShaderComputeStageInfo compute{{threads, 1, 1}, 0, {false, false, false}, false, 1};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Compute, program.Address(), code, 0, {}},
        {32, 0, userData, compute, std::nullopt, std::nullopt, regions},
        device.Target(),
        {0, 0, 0, 128}
    };
    request.useCache = false;
    const auto result = ShaderRecompiler::Recompile(request);
    device.Dispatch(result, 1, 1, 1, static_cast<std::uintptr_t>(program.Address()));
    device.WaitIdle();
}

std::uint32_t Pattern(std::uint32_t page, std::uint32_t word) {
    return 0x40000000u | (page << 16u) | word;
}

std::uint32_t WordAt(const std::uint8_t* base, std::uint64_t offset) {
    std::uint32_t value = 0;
    std::memcpy(&value, base + offset, 4);
    return value;
}

void CrossingTwoImports(AgcDriver::VulkanDevice& device) {
    const auto reserved = 2 * PageBytes;
    auto* base = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(reserved, PageBytes));
    Require(base != nullptr, "page table: cannot reserve two pages");
    GuestArena::GuestArenaCommit_nid_postfix(base, PageBytes, PAGE_READWRITE, 16384);
    GuestArena::GuestArenaCommit_nid_postfix(base + PageBytes, PageBytes, PAGE_READWRITE, 16384);
    GuestAllocations::Mutation().Add(base, reserved, true, true, true);
    for (std::uint32_t page = 0; page < 2; ++page) {
        for (std::uint32_t word = 0; word < WordsPerPage; ++word) {
            const auto value = Pattern(page, word);
            std::memcpy(base + page * PageBytes + word * 4u, &value, 4);
        }
    }
    PrivatePages output(1);
    std::memset(output.Data(), 0xa5, output.Bytes());
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(base));
    std::vector<std::uint32_t> user(12, 0u);
    const auto source = BufferResource(address, static_cast<std::uint32_t>(reserved));
    const auto target = BufferResource(output.Address(), static_cast<std::uint32_t>(output.Bytes()));
    std::copy(source.begin(), source.end(), user.begin());
    user[4] = Low(address);
    user[5] = High(address);
    std::copy(target.begin(), target.end(), user.begin() + 8);
    Dispatch(device, CrossingProgram, user, Lanes);
    for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
        const auto first = CrossingStart + lane * 4u;
        for (std::uint32_t kind = 0; kind < 2; ++kind) {
            for (std::uint32_t component = 0; component < 4; ++component) {
                const auto at = first + component * 4u;
                const auto expected = WordAt(base, at);
                const auto actual = WordAt(output.Data(), lane * 32u + kind * 16u + component * 4u);
                Expect(actual == expected, std::string(kind == 0 ? "a run-time V# load" : "a GLOBAL load") + " of 16 bytes at guest +" + Hex(first) + (first + 16u > PageBytes && first < PageBytes ? " (across two imports)" : "") + " read " + Hex(actual) + " for +" + Hex(at) + ", expected " + Hex(expected));
            }
        }
    }
    GuestAllocations::Mutation().Remove(base);
    GuestArena::GuestArenaReset_nid_postfix(base, reserved);
    GuestArena::GuestArenaRelease_nid_postfix(base, reserved);
}

void StoresAndWrittenPages(AgcDriver::VulkanDevice& device) {
    PrivatePages pages(3);
    for (std::uint64_t word = 0; word < pages.Bytes() / 4u; ++word) pages.Words()[word] = Untouched;
    Expect(AgcDriver::GuestMemory::Watched(pages.Address(), pages.Bytes()), "written-page records: committed guest memory is not write-watched, so GPU and CPU writes cannot be told apart");
    AgcDriver::GuestMemory::CollectWritesUncached(pages.Address(), pages.Bytes());
    const auto generation = AgcDriver::GuestMemory::TrackerGeneration();
    std::vector<std::uint32_t> user(6, 0u);
    const auto buffer = BufferResource(pages.Address(0), static_cast<std::uint32_t>(PageBytes));
    std::copy(buffer.begin(), buffer.end(), user.begin());
    user[4] = Low(pages.Address(PageBytes));
    user[5] = High(pages.Address(PageBytes));
    Dispatch(device, StoreProgram, user, Lanes);
    AgcDriver::GuestMemory::FlushGpuWrites(pages.Address(), pages.Bytes());
    for (std::uint32_t page = 0; page < 2; ++page) {
        for (std::uint32_t lane = 0; lane < Lanes; ++lane) {
            const auto stored = pages.Words()[page * WordsPerPage + lane * WordsPerSubpage];
            Expect(stored == StoredBase + lane, std::string(page == 0 ? "a run-time V# store" : "a GLOBAL store") + " of lane " + std::to_string(lane) + " left " + Hex(stored) + ", expected " + Hex(StoredBase + lane));
            Expect(pages.Words()[page * WordsPerPage + lane * WordsPerSubpage + 1] == Untouched, "a store wrote past its dword");
        }
    }
    const std::array<std::uint64_t, 3> generations{generation, generation, generation};
    std::array<std::uint8_t, 3> changed{};
    std::array<std::uint8_t, 3> cpu{};
    AgcDriver::GuestMemory::CollectWritesUncached(pages.Address(), pages.Bytes());
    AgcDriver::GuestMemory::ChangedBlocks(pages.Address(), pages.Bytes(), generations, changed, cpu);
    Expect(changed[0] == AgcDriver::GuestMemory::BlockWritten && cpu[0] == 0, "written-page records: the page a V# store wrote was not marked written by the GPU at completion");
    Expect(changed[1] == AgcDriver::GuestMemory::BlockWritten && cpu[1] == 0, "written-page records: the page a GLOBAL store wrote was not marked written by the GPU at completion");
    Expect(changed[2] == AgcDriver::GuestMemory::BlockUnchanged, "written-page records: a page no store wrote was marked written");
}

void StoreToReadOnlyPage(AgcDriver::VulkanDevice& device) {
    PrivatePages page(1, false);
    for (std::uint32_t word = 0; word < Lanes; ++word) page.Words()[word] = Untouched;
    const std::vector<std::uint32_t> user{Low(page.Address()), High(page.Address())};
    std::string error;
    {
        StderrCapture capture;
        try {
            Dispatch(device, ReadOnlyStoreProgram, user, Lanes);
        } catch (const std::exception& failure) {
            error = failure.what();
        }
        error += capture.Finish();
    }
    Expect(error.find("BDA access failed") != std::string::npos && error.find("reason=2") != std::string::npos, "a GLOBAL store to a page the guest maps without CPU write access did not fault with Permission: " + error);
    const auto at = error.find("address=0x");
    const auto faulted = at != std::string::npos ? std::strtoull(error.c_str() + at + 10, nullptr, 16) : 0ull;
    Expect(faulted >= page.Address() && faulted < page.Address() + Lanes * 4u, "the Permission fault does not name an address the program stored to: " + error);
    for (std::uint32_t word = 0; word < Lanes; ++word) Expect(page.Words()[word] == Untouched, "a store that faulted with Permission landed at word " + std::to_string(word));
}

}

#endif

int main() {
#ifdef _WIN32
    try {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        BuildPrograms();
        CrossingTwoImports(*device);
        StoresAndWrittenPages(*device);
        StoreToReadOnlyPage(*device);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: exception: %s\n", error.what());
        return 1;
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d new path page table checks failed\n", failures);
        return 1;
    }
    std::printf("new path page table tests passed\n");
    return 0;
#else
    std::printf("skipped, commit imports are Windows-only\n");
    return VulkanTestSkipped;
#endif
}
