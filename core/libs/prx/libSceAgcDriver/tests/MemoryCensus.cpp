#include "prx/libSceAgcDriver/Execution/include/MemoryCensus.hpp"
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using AgcDriver::MemoryCensus::RegionClass;

void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr std::uint64_t ArenaBegin = 0x10000;
constexpr std::uint64_t ArenaEnd = 0xfc00000000;

std::uint64_t Resident(const AgcDriver::MemoryCensus::RegionTotals& totals, RegionClass type) {
    return totals.resident[static_cast<std::size_t>(type)];
}

void Classifies() {
    using AgcDriver::MemoryCensus::ClassifyMapping;
    Check(ClassifyMapping(0x200000000, 0x200004000, "rw-p", 0, "", ArenaBegin, ArenaEnd) == RegionClass::Arena, "an anonymous range inside the arena is not arena");
    Check(ClassifyMapping(0x200000000, 0x200004000, "rw-s", 7, "/memfd:direct memory (deleted)", ArenaBegin, ArenaEnd) == RegionClass::Arena, "a shared view inside the arena is not arena");
    Check(ClassifyMapping(0xfbffffc000, 0xfc00004000, "rw-p", 0, "", ArenaBegin, ArenaEnd) == RegionClass::Arena, "a range crossing the arena end is not arena");
    Check(ClassifyMapping(0x7f0000000000, 0x7f0000001000, "rw-s", 9, "/memfd:direct memory (deleted)", ArenaBegin, ArenaEnd) == RegionClass::Mapped, "a shared view outside the arena is not mapped");
    Check(ClassifyMapping(0x7f0000000000, 0x7f0000001000, "rw-p", 9, "/memfd:anyps5 (deleted)", ArenaBegin, ArenaEnd) == RegionClass::Mapped, "a private memfd view is not mapped");
    Check(ClassifyMapping(0x7f0000000000, 0x7f0000001000, "r-xp", 1234, "/usr/lib/x86_64-linux-gnu/libc.so.6", ArenaBegin, ArenaEnd) == RegionClass::Image, "a library is not an image");
    Check(ClassifyMapping(0x7f0000000000, 0x7f0000001000, "rw-p", 0, "[heap]", ArenaBegin, ArenaEnd) == RegionClass::Private, "the brk heap is not private");
    Check(ClassifyMapping(0x7f0000000000, 0x7f0000001000, "rw-p", 0, "", ArenaBegin, ArenaEnd) == RegionClass::Private, "an anonymous range is not private");
    Check(ClassifyMapping(0x0, 0x10000, "r--p", 0, "", ArenaBegin, ArenaEnd) == RegionClass::Private, "a range ending at the arena start is arena");
}

void SumsSmaps() {
    const std::string_view text =
        "200000000-200400000 rw-p 00000000 00:00 0 \n"
        "Size:               4096 kB\n"
        "Rss:                1024 kB\n"
        "Pss:                1024 kB\n"
        "Swap:                 16 kB\n"
        "VmFlags: rd wr mr mw me ac\n"
        "aaaa00000000-aaaa00100000 rw-s 00000000 00:01 77 /memfd:direct memory (deleted)\n"
        "Rss:                 512 kB\n"
        "Swap:                  0 kB\n"
        "7f1234000000-7f1234200000 r-xp 00000000 08:01 1234 /usr/lib/libvulkan.so.1\n"
        "Rss:                 256 kB\n"
        "Anonymous:            12 kB\n"
        "7f1234400000-7f1234800000 rw-p 00000000 00:00 0\n"
        "Rss:                2048 kB\n"
        "Swap:                  8 kB\n"
        "garbage line without a colon\n"
        "Rss: not-a-number kB\n"
        "Rss: 99999999999999999999 kB\n"
        "Rss: 64 MB\n"
        "ffffffffff600000-ffffffffff601000 --xp 00000000 00:00 0 [vsyscall]\n"
        "Rss:                   4 kB\n";
    AgcDriver::MemoryCensus::SmapsParser parser(ArenaBegin, ArenaEnd);
    std::string_view rest = text;
    while (!rest.empty()) {
        const auto end = rest.find('\n');
        parser.Line(rest.substr(0, end));
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
    }
    const auto& totals = parser.Totals();
    Check(totals.regions == 5, "the parser did not find five regions");
    Check(Resident(totals, RegionClass::Arena) == 1024u * 1024u, "arena resident bytes are wrong");
    Check(Resident(totals, RegionClass::Mapped) == 512u * 1024u, "mapped resident bytes are wrong");
    Check(Resident(totals, RegionClass::Image) == 256u * 1024u, "image resident bytes are wrong");
    Check(Resident(totals, RegionClass::Private) == (2048u + 4u) * 1024u, "private resident bytes are wrong or a malformed line was counted");
    Check(totals.swapped[static_cast<std::size_t>(RegionClass::Arena)] == 16u * 1024u, "arena swap bytes are wrong");
    Check(totals.swapped[static_cast<std::size_t>(RegionClass::Private)] == 8u * 1024u, "private swap bytes are wrong");
    AgcDriver::MemoryCensus::SmapsParser orphan(ArenaBegin, ArenaEnd);
    orphan.Line("Rss: 100 kB");
    orphan.Line("");
    Check(orphan.Totals().regions == 0 && Resident(orphan.Totals(), RegionClass::Private) == 0, "a field before any region header was counted");
    Check(parser.PrivateMappings().empty(), "a parser not asked to record private mappings recorded some");
    AgcDriver::MemoryCensus::SmapsParser recording(ArenaBegin, ArenaEnd, true);
    rest = text;
    while (!rest.empty()) {
        const auto end = rest.find('\n');
        recording.Line(rest.substr(0, end));
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
    }
    const auto& mappings = recording.PrivateMappings();
    Check(mappings.size() == 2, "the parser did not record the two private mappings");
    Check(mappings[0].begin == 0x7f1234400000ull && mappings[0].end == 0x7f1234800000ull && mappings[0].resident == 2048u * 1024u && mappings[0].permissions == "rw-p" && mappings[0].name.empty(), "the anonymous mapping was recorded wrong or a malformed line was counted");
    Check(mappings[1].begin == 0xffffffffff600000ull && mappings[1].resident == 4u * 1024u && mappings[1].permissions == "--xp" && mappings[1].name == "[vsyscall]", "the named private mapping was recorded wrong");
    Check(recording.Totals().resident == totals.resident, "recording private mappings changed the totals");
}

void ParsesStatus() {
    const auto totals = AgcDriver::MemoryCensus::ParseStatus(
        "Name:\tagc\n"
        "VmPeak:\t  900000 kB\n"
        "VmRSS:\t  300000 kB\n"
        "RssAnon:\t  200000 kB\n"
        "RssFile:\t   60000 kB\n"
        "RssShmem:\t   40000 kB\n"
        "VmSwap:\t     128 kB\n"
        "Threads:\t12\n"
        "VmRSS: broken\n");
    Check(totals.rss == 300000ull * 1024u, "VmRSS is wrong or a broken line replaced it");
    Check(totals.anonymous == 200000ull * 1024u && totals.file == 60000ull * 1024u && totals.shmem == 40000ull * 1024u, "the RSS split is wrong");
    Check(totals.swap == 128u * 1024u, "VmSwap is wrong");
    const auto empty = AgcDriver::MemoryCensus::ParseStatus("");
    Check(empty.rss == 0 && empty.swap == 0, "an empty status is not zero");
}

void Remainders() {
    using AgcDriver::MemoryCensus::Unattributed;
    Check(Unattributed(100, {30, 20}) == 50, "a positive remainder is wrong");
    Check(Unattributed(10, {30, 20}) == -40, "a negative remainder is wrong");
    Check(Unattributed(0, {}) == 0, "an empty remainder is not zero");
    constexpr auto huge = std::numeric_limits<std::uint64_t>::max();
    Check(Unattributed(huge, {huge, huge}) == 0, "saturated parts do not cancel a saturated total");
    Check(Unattributed(5, {huge}) == -(std::numeric_limits<std::int64_t>::max() - 5), "an overflowing part does not saturate");
}

void PlansProbes() {
    using AgcDriver::MemoryCensus::LivePool;
    using AgcDriver::MemoryCensus::PoolKind;
    using AgcDriver::MemoryCensus::PoolShape;
    const PoolShape chain{PoolKind::Chain, 0, 1024, {{7, 4098}, {2, 1024}, {3, 1024}, {0, 512}}};
    const PoolShape large{PoolKind::Dedicated, 0, 1, {{2, 16384}}};
    const PoolShape small{PoolKind::Dedicated, 0, 1, {{2, 100}}};
    const std::vector<LivePool> pools{{chain, 10, true}, {large, 1, true}, {chain, 50, true}, {small, 1, true}, {chain, 20, true}, {large, 1, true}, {chain, 30, false}};
    Check(AgcDriver::MemoryCensus::Descriptors(chain) == 6658, "a shape's descriptors are not the sum of its sizes");
    const auto plan = AgcDriver::MemoryCensus::PlanProbes(pools, 2);
    Check(plan.probed.size() == 2 && plan.skipped.size() == 2, "the plan did not probe two create-infos and skip the rest");
    Check(plan.probed[0].shape == large && plan.probed[0].pools == 2 && plan.probed[0].sets == 2, "the largest create-info by descriptors times pools is not probed first");
    Check(plan.probed[1].shape == chain && plan.probed[1].pools == 3 && plan.probed[1].sets == 80 && plan.probed[1].representative == 2, "the chain group or its fullest pool is wrong");
    Check(plan.skipped[0].shape == chain && !plan.skipped[0].probeable && plan.skipped[0].pools == 1, "a pool with a chained create-info was probed or merged");
    Check(plan.skipped[1].shape == small && plan.skipped[1].pools == 1, "the create-info beyond the limit was not skipped");
    Check(AgcDriver::MemoryCensus::PlanProbes({}, 16).probed.empty(), "an empty pool list planned a probe");
}

void EstimatesProbes() {
    using AgcDriver::MemoryCensus::ProbeEstimate;
    AgcDriver::MemoryCensus::PoolGroup group;
    group.pools = 3;
    group.sets = 80;
    Check(ProbeEstimate(group, {4096, 8192, 50}) == 3u * 4096u + 8192u * 80u / 50u, "the estimate does not scale pools and sets");
    Check(ProbeEstimate(group, {-100, 8192, 50}) == 8192u * 80u / 50u, "a negative pool delta was not clamped");
    Check(ProbeEstimate(group, {4096, -5, 50}) == 3u * 4096u, "a negative set delta was not clamped");
    Check(ProbeEstimate(group, {4096, 8192, 0}) == 3u * 4096u, "sets were scaled with no probed set");
}

void GroupsAllocations() {
    using AgcDriver::MemoryCensus::AllocationGroups;
    using AgcDriver::MemoryCensus::SizeClassOf;
    constexpr std::uint64_t Mebibyte = 1ull << 20u;
    Check(SizeClassOf(0) == 0 && SizeClassOf(Mebibyte - 1) == 0 && SizeClassOf(Mebibyte) == 1 && SizeClassOf(4 * Mebibyte) == 2 && SizeClassOf(16 * Mebibyte) == 3 && SizeClassOf(64 * Mebibyte - 1) == 3 && SizeClassOf(64 * Mebibyte) == 4, "a size class boundary is wrong");
    AllocationGroups groups;
    groups.Add(0x1000, 0x3000, 0x2000, "rw");
    groups.Add(0x1000, 0x5000, 0x2000, "rw");
    groups.Add(0x1000, 0x6000, 0x1000, "r");
    groups.Add(0x10000000, 0x10500000, 5 * Mebibyte, "rw", "[heap]");
    groups.Add(0x20000000, 0x24600000, 70 * Mebibyte, "rwx");
    groups.Add(0x30000000, 0x30001000, 0, "rw");
    Check(groups.Count() == 4 && groups.Bytes() == 0x5000 + 75 * Mebibyte, "regions of one allocation were not merged or bytes were lost");
    const auto classes = groups.Histogram();
    Check(classes[0].count == 2 && classes[0].bytes == 0x5000 && classes[1].count == 0 && classes[2].count == 1 && classes[2].bytes == 5 * Mebibyte && classes[3].count == 0 && classes[4].count == 1 && classes[4].bytes == 70 * Mebibyte, "the size histogram is wrong");
    const std::vector<std::uint64_t> blocks{0x5, 0x6000, 0x20000010};
    groups.Tag(blocks, AgcDriver::MemoryCensus::TagHeapBlock);
    const std::vector<std::uint64_t> device{0x2000};
    groups.Tag(device, AgcDriver::MemoryCensus::TagDeviceMemory);
    Check(groups.Tagged(AgcDriver::MemoryCensus::TagHeapBlock) == 1 && groups.Tagged(AgcDriver::MemoryCensus::TagDeviceMemory) == 1, "an address outside every allocation or at an allocation's end was tagged");
    const auto largest = groups.Largest(2);
    Check(largest.size() == 2 && largest[0].base == 0x20000000 && largest[1].base == 0x10000000 && largest[1].name == "[heap]", "the largest allocations are wrong");
    const auto merged = groups.Largest(16);
    Check(merged.size() == 4 && merged[2].base == 0x1000 && merged[2].regions == 3 && merged[2].protections == "rw/r", "the merged allocation lost its regions or protections");
    const auto text = AgcDriver::MemoryCensus::GroupsText(groups, 2);
    Check(text.find("4 allocations 75.0 MiB; by size <1 MiB 2 (0.0 MiB), 1-4 MiB 0 (0.0 MiB), 4-16 MiB 1 (5.0 MiB), 16-64 MiB 0 (0.0 MiB), >=64 MiB 1 (70.0 MiB)") == 0, "the histogram text is wrong: " + text);
    Check(text.find("; largest: 0x20000000 70.0 MiB 1 regions rwx (heap block), 0x10000000 5.0 MiB 1 regions rw [heap]") != std::string::npos, "the largest allocations text is wrong: " + text);
    Check(AgcDriver::MemoryCensus::GroupsText(AllocationGroups{}, 16).find("0 allocations 0.0 MiB") == 0, "an empty grouping does not print zero allocations");
}

}

int main() {
    try {
        Classifies();
        SumsSmaps();
        ParsesStatus();
        Remainders();
        PlansProbes();
        EstimatesProbes();
        GroupsAllocations();
        std::cout << "memory census parsing tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
