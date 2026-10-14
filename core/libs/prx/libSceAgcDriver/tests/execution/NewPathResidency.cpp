#include "NewPathFixture.hpp"
#include "VulkanTestDevice.hpp"
#include <cstdlib>
#include <exception>
#include <map>
#include <utility>

#ifdef _WIN32

namespace {

using namespace NewPath;
using AgcDriver::Graphics::ResidencyAccess;
using AgcDriver::Graphics::ResidencyCounts;

constexpr std::uint32_t ReadLanes = MirrorReader::Lanes;
constexpr std::uint32_t ReadPages = MirrorReader::Pages;
constexpr std::uint32_t SectionPages = 32;
constexpr std::uint32_t WriteLanes = SubpagesPerPage;
constexpr std::uint32_t WrittenBase = 0x7c000000u;
constexpr std::uint32_t MirrorMarker = 0x5a000001u;
constexpr std::uint32_t StoredByCpu = 0x5a000002u;
constexpr std::uint32_t HotStore = 0x5a000100u;
constexpr std::uint32_t WrittenBack = 0x5a000003u;
constexpr std::uint32_t DroppedMarker = 0x5a000004u;
constexpr std::uint32_t EvictedMarker = 0x5a000005u;
constexpr std::uint32_t AdmittedMarker = 0x5a000006u;

int failures = 0;

void Expect(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

Program WriteProgram;

void BuildPrograms() {
    using namespace Isa;
    WriteProgram << Vop2(VLshlRevB32, 1, InlineInteger(12), 0)
                 << Vop2(VAddNcU32, 2, Literal, 0) << WrittenBase
                 << Mubuf(BufferStoreDword, 2, 1, 0)
                 << EndProgram;
}

std::uint32_t Pattern(std::uint32_t tag, std::uint64_t page, std::uint32_t word) {
    return (tag << 28u) | (static_cast<std::uint32_t>(page) << 14u) | word;
}

class Model {
public:
    Model(SharedPages& pages, std::uint32_t tag) : pages(pages), tag(tag) {}

    void Fill() {
        const auto count = pages.Bytes() / PageBytes;
        for (std::uint64_t page = 0; page < count; ++page) {
            auto* words = pages.Words(page);
            for (std::uint32_t word = 0; word < WordsPerPage; ++word) words[word] = Pattern(tag, page, word);
        }
        values.clear();
    }

    void Retag(std::uint32_t next) {
        tag = next;
        Fill();
    }

    void Store(std::uint64_t page, std::uint32_t word, std::uint32_t value) {
        pages.Words(page)[word] = value;
        values[{page, word}] = value;
    }

    void Set(std::uint64_t page, std::uint32_t word, std::uint32_t value) { values[{page, word}] = value; }

    std::uint32_t Value(std::uint64_t page, std::uint32_t word) const {
        const auto found = values.find({page, word});
        return found != values.end() ? found->second : Pattern(tag, page, word);
    }

    SharedPages& Pages() { return pages; }

private:
    SharedPages& pages;
    std::uint32_t tag;
    std::map<std::pair<std::uint64_t, std::uint32_t>, std::uint32_t> values;
};

class Harness {
public:
    void RecordRead(Commands& commands, const SharedPages& source, std::uint64_t firstPage, std::uint32_t pages = ReadPages) { reader.Record(commands, source.Address(firstPage), pages); }
    std::vector<std::uint32_t> Collect() { return reader.Collect(); }
    std::vector<std::uint32_t> Read(const SharedPages& source, std::uint64_t firstPage, std::uint32_t pages = ReadPages) { return reader.Read(source.Address(firstPage), pages); }

    void RecordWrite(Commands& commands, const SharedPages& target, std::uint64_t page) {
        const auto buffer = BufferResource(target.Address(page), static_cast<std::uint32_t>(PageBytes));
        commands.Dispatch(WriteProgram, buffer, WriteLanes);
    }

private:
    MirrorReader reader;
};

std::string Where(const char* what, std::uint64_t page, std::uint32_t subpage) {
    return std::string(what) + ": page " + std::to_string(page) + " subpage " + std::to_string(subpage);
}

void ExpectLanes(const std::vector<std::uint32_t>& lanes, const Model& model, std::uint64_t firstPage, std::uint32_t pages, const std::map<std::uint32_t, std::uint32_t>& overrides, const char* what) {
    for (std::uint32_t lane = 0; lane < ReadLanes; ++lane) {
        const auto page = firstPage + lane / SubpagesPerPage;
        const auto subpage = lane % SubpagesPerPage;
        auto expected = lane / SubpagesPerPage < pages ? model.Value(page, subpage * WordsPerSubpage) : 0u;
        if (const auto found = overrides.find(lane); found != overrides.end()) expected = found->second;
        if (lanes[lane] == expected) continue;
        Expect(false, Where(what, page, subpage) + " read " + Hex(lanes[lane]) + ", expected " + Hex(expected));
        return;
    }
}

ResidencyCounts Counts() {
    ResidencyCounts counts;
    Locked([&] { counts = Residency().Manager().Counts(); });
    return counts;
}

bool Resident(const SharedPages& pages, std::uint64_t page) {
    bool resident = false;
    Locked([&] { resident = Residency().Manager().Slot(PageOf(pages.Address(page))).has_value(); });
    return resident;
}

bool Guarded(const SharedPages& pages, std::uint64_t page) {
    return AgcDriver::GuestMemory::WriteGuarded(PageOf(pages.Address(page)));
}

bool Hot(const SharedPages& pages, std::uint64_t page) {
    bool hot = false;
    Locked([&] { hot = Residency().Manager().Hot(PageOf(pages.Address(page))); });
    return hot;
}

void Use(const SharedPages& pages, std::uint64_t first, std::uint64_t count) {
    Locked([&] { Residency().Manager().Use(pages.Address(first), count * PageBytes, ResidencyAccess::Read); });
}

void WarmUp(Harness& harness, Model& model) {
    harness.Read(model.Pages(), 0);
    const auto lanes = harness.Read(model.Pages(), 0);
    ExpectLanes(lanes, model, 0, ReadPages, {}, "warm-up read of guest memory");
}

void Admission(Harness& harness, Model& model) {
    auto& pages = model.Pages();
    const auto before = Counts();
    Use(pages, 0, 4);
    const auto after = Counts();
    Expect(after.admitted == before.admitted + 4, "admission: a read use of four pages admitted " + std::to_string(after.admitted - before.admitted));
    for (std::uint64_t page = 0; page < 4; ++page) {
        Expect(Resident(pages, page), "admission: page " + std::to_string(page) + " is not resident after its use");
        Expect(Guarded(pages, page), "admission: CPU-writable page " + std::to_string(page) + " is resident without a write guard");
    }
    FillMirror(pages.Address(1, 2), MirrorMarker);
    const auto lanes = harness.Read(pages, 0);
    ExpectLanes(lanes, model, 0, ReadPages, {{1 * SubpagesPerPage + 2, MirrorMarker}}, "a dispatch reading a resident page through the page table did not read its mirror");
    Expect(pages.Words(1)[2 * WordsPerSubpage] == model.Value(1, 2 * WordsPerSubpage), "admission: a change made only to the mirror reached guest memory");
}

void DemotionAndVersions(Harness& harness, Model& model) {
    auto& pages = model.Pages();
    const auto before = Counts();
    Commands commands;
    harness.RecordRead(commands, pages, 0);
    harness.RecordWrite(commands, pages, 2);
    commands.Submit(0);
    const auto lanes = harness.Collect();
    ExpectLanes(lanes, model, 0, ReadPages, {{1 * SubpagesPerPage + 2, MirrorMarker}}, "a read recorded before a demotion in the same submission did not keep the page table version with the mirror");
    for (std::uint32_t subpage = 0; subpage < SubpagesPerPage; ++subpage) {
        const auto stored = pages.Words(2)[subpage * WordsPerSubpage];
        Expect(stored == WrittenBase + subpage, Where("a store to a page that was resident before its dispatch", 2, subpage) + " left " + Hex(stored) + ", expected " + Hex(WrittenBase + subpage));
        model.Set(2, subpage * WordsPerSubpage, WrittenBase + subpage);
    }
    const auto after = Counts();
    Expect(after.demoted >= before.demoted + 4, "demotion: a dispatch that stores through the page table demoted " + std::to_string(after.demoted - before.demoted) + " of the four GPU-writable mirrors");
    for (std::uint64_t page = 0; page < 4; ++page) {
        Expect(!Resident(pages, page), "demotion: page " + std::to_string(page) + " stayed resident through a storing dispatch");
        Expect(!Guarded(pages, page), "demotion: page " + std::to_string(page) + " left residency but kept its write guard");
    }
    const auto again = harness.Read(pages, 0);
    ExpectLanes(again, model, 0, ReadPages, {}, "after demotion a dispatch did not read the imports");
}

void GuardFaultAndRefresh(Harness& harness, Model& model) {
    auto& pages = model.Pages();
    Use(pages, 5, 1);
    Expect(Resident(pages, 5) && Guarded(pages, 5), "write guard: page 5 was not admitted with a guard");
    ExpectLanes(harness.Read(pages, 0), model, 0, ReadPages, {}, "write guard: the read before the store");
    const auto before = Counts();
    model.Store(5, 3 * WordsPerSubpage, StoredByCpu);
    Expect(!Guarded(pages, 5), "write guard: a CPU store to a guarded resident page did not take the guard's fault");
    const auto lanes = harness.Read(pages, 0);
    ExpectLanes(lanes, model, 0, ReadPages, {}, "write guard: the dispatch after a CPU store to a resident page read a stale mirror");
    const auto after = Counts();
    Expect(after.faults == before.faults + 1 && after.refreshed >= before.refreshed + 1, "write guard: the ordering point applied " + std::to_string(after.faults - before.faults) + " faults and " + std::to_string(after.refreshed - before.refreshed) + " refreshes, expected one of each");
    Expect(Resident(pages, 5) && Guarded(pages, 5), "write guard: the refreshed page did not stay resident with its guard re-armed");
}

void HotRule(Harness& harness, Model& model) {
    auto& pages = model.Pages();
    Use(pages, 6, 1);
    ExpectLanes(harness.Read(pages, 0), model, 0, ReadPages, {}, "hot rule: the read after admission");
    Expect(Resident(pages, 6), "hot rule: page 6 was not admitted");
    std::uint32_t stores = 0;
    for (; stores < 8 && Resident(pages, 6); ++stores) {
        model.Store(6, 4 * WordsPerSubpage, HotStore + stores);
        ExpectLanes(harness.Read(pages, 0), model, 0, ReadPages, {}, "hot rule: a dispatch after a CPU store to page 6");
    }
    Expect(!Resident(pages, 6) && Hot(pages, 6), "hot rule: a page the CPU stored to before each of " + std::to_string(stores) + " submissions stayed resident or did not turn hot");
    Expect(!Guarded(pages, 6), "hot rule: a page that left residency as hot kept its guard");
    const auto faults = Counts().faults;
    model.Store(6, 5 * WordsPerSubpage, HotStore + 0x80u);
    Expect(Counts().faults == faults, "hot rule: a store to a hot page faulted");
    Use(pages, 6, 1);
    Expect(!Resident(pages, 6), "hot rule: a use right after the page turned hot admitted it again");
    ExpectLanes(harness.Read(pages, 0), model, 0, ReadPages, {}, "hot rule: the read of a hot page through its import");
    const auto cooled = Counts().cooled;
    bool admitted = false;
    for (std::uint32_t quiet = 0; quiet < 6 && !admitted; ++quiet) {
        harness.Read(pages, 0);
        Use(pages, 6, 1);
        admitted = Resident(pages, 6);
    }
    Expect(admitted && Counts().cooled > cooled, "hot rule: a hot page the CPU stopped writing was not admitted again after six quiet submissions");
}

void WriteBackOnCpuRead(Harness& harness, Model& model) {
    auto& pages = model.Pages();
    Use(pages, 7, 1);
    harness.Read(pages, 0);
    FillMirror(pages.Address(7, 3), WrittenBack);
    Locked([&] { Residency().Manager().NoteGpuWritten(pages.Address(7, 3), SubpageBytes); });
    Settle();
    Expect(pages.Words(7)[3 * WordsPerSubpage] == model.Value(7, 3 * WordsPerSubpage), "write-back: guest memory held the mirror's bytes before a CPU read asked for them");
    const auto before = Counts();
    std::uint32_t value = 0;
    AgcDriver::GuestMemory::Read(pages.Address(7, 3, 5), std::as_writable_bytes(std::span(&value, 1)), 4);
    Expect(value == WrittenBack, "write-back: a driver CPU read of a dirty mirror page read " + Hex(value) + ", expected the GPU's " + Hex(WrittenBack));
    for (std::uint32_t word = 0; word < WordsPerSubpage; ++word) {
        if (pages.Words(7)[3 * WordsPerSubpage + word] == WrittenBack) continue;
        Expect(false, "write-back: word " + std::to_string(word) + " of the dirty subpage was not written back");
        break;
    }
    Expect(pages.Words(7)[2 * WordsPerSubpage] == model.Value(7, 2 * WordsPerSubpage) && pages.Words(7)[4 * WordsPerSubpage] == model.Value(7, 4 * WordsPerSubpage), "write-back: a clean subpage of the page was overwritten");
    const auto after = Counts();
    Expect(after.writtenBackBytes == before.writtenBackBytes + SubpageBytes, "write-back: " + std::to_string(after.writtenBackBytes - before.writtenBackBytes) + " bytes written back, expected one subpage");
    std::uint16_t dirty = 1;
    Locked([&] { dirty = Residency().Manager().Dirty(PageOf(pages.Address(7))); });
    Expect(dirty == 0, "write-back: the page stayed dirty after its write-back");
    for (std::uint32_t word = 0; word < WordsPerSubpage; ++word) model.Set(7, 3 * WordsPerSubpage + word, WrittenBack);
}

void DropOnUnmap(Harness& harness) {
    SharedPages pages(2);
    Model model(pages, 2);
    model.Fill();
    Use(pages, 0, 1);
    Expect(Resident(pages, 0), "drop: the page was not admitted");
    FillMirror(pages.Address(0, 4), DroppedMarker);
    ExpectLanes(harness.Read(pages, 0, 2), model, 0, 2, {{4, DroppedMarker}}, "drop: the read through the mirror before the unmap");
    const auto before = Counts();
    pages.Unmap();
    pages.Map();
    model.Retag(3);
    ExpectLanes(harness.Read(pages, 0, 2), model, 0, 2, {}, "drop: a dispatch after an unmap and a new mapping at the same address did not read the new memory");
    Expect(!Resident(pages, 0), "drop: an unmapped page stayed resident");
    Expect(Counts().dropped >= before.dropped + 1, "drop: the unmap dropped no resident page");
}

void EvictionUnderBudget(Harness& harness) {
    Settle();
    std::uint32_t chunkSlots = 0;
    Locked([&] {
        auto& manager = Residency().Manager();
        manager.SetBudget(0);
        chunkSlots = manager.ChunkSlots();
    });
    Settle();
    const auto cleared = Counts();
    Expect(cleared.resident == 0 && cleared.chunks == 0, "eviction: a zero budget left " + std::to_string(cleared.resident) + " pages resident in " + std::to_string(cleared.chunks) + " chunks");
    SharedPages pages(chunkSlots + ReadPages);
    Model model(pages, 4);
    model.Fill();
    Locked([&] { Residency().Manager().SetBudget(std::uint64_t{chunkSlots} * PageBytes); });
    Use(pages, 0, ReadPages);
    FillMirror(pages.Address(1, 2), EvictedMarker);
    Settle();
    Use(pages, ReadPages, chunkSlots - ReadPages);
    Settle();
    ExpectLanes(harness.Read(pages, 0), model, 0, ReadPages, {{1 * SubpagesPerPage + 2, EvictedMarker}}, "eviction: the read through the mirror before the budget was reached");
    Settle();
    const auto before = Counts();
    Use(pages, chunkSlots, ReadPages);
    const auto after = Counts();
    Expect(after.evicted == before.evicted + ReadPages, "eviction: admitting " + std::to_string(ReadPages) + " pages past a one-chunk budget evicted " + std::to_string(after.evicted - before.evicted));
    for (std::uint64_t page = 0; page < ReadPages; ++page) {
        Expect(!Resident(pages, page), "eviction: least recent page " + std::to_string(page) + " stayed resident");
        Expect(Resident(pages, chunkSlots + page), "eviction: newly used page " + std::to_string(chunkSlots + page) + " was not admitted");
    }
    Expect(Resident(pages, ReadPages), "eviction: a more recently used page was evicted");
    ExpectLanes(harness.Read(pages, 0), model, 0, ReadPages, {}, "eviction: a dispatch did not read the imports of evicted pages");
    FillMirror(pages.Address(chunkSlots + 1, 2), AdmittedMarker);
    ExpectLanes(harness.Read(pages, chunkSlots), model, chunkSlots, ReadPages, {{1 * SubpagesPerPage + 2, AdmittedMarker}}, "eviction: a dispatch did not read the mirror of a page admitted in an evicted page's slot");
    Settle();
    Locked([&] { Residency().Manager().SetBudget(0); });
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
        BuildPrograms();
        SharedPages section(SectionPages);
        Model model(section, 1);
        model.Fill();
        Harness harness;
        WarmUp(harness, model);
        Residency();
        Admission(harness, model);
        DemotionAndVersions(harness, model);
        GuardFaultAndRefresh(harness, model);
        HotRule(harness, model);
        WriteBackOnCpuRead(harness, model);
        DropOnUnmap(harness);
        EvictionUnderBudget(harness);
        AgcDriverShutdown_nid_postfix();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: exception: %s\n", error.what());
        AgcDriverShutdown_nid_postfix();
        return 1;
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d new path residency checks failed\n", failures);
        return 1;
    }
    std::printf("new path residency tests passed\n");
    return 0;
#else
    std::printf("skipped, commit imports and write guards are Windows-only\n");
    return VulkanTestSkipped;
#endif
}
