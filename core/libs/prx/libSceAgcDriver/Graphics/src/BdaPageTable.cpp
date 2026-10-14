#include "prx/libSceAgcDriver/Graphics/include/BdaPageTable.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Resources.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>

namespace AgcDriver::Graphics {

namespace {

namespace Abi = ShaderRecompiler::BdaAbi;

constexpr VkBufferUsageFlags TableUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
constexpr std::uint64_t PageBytes = std::uint64_t{1} << Abi::PageShift;
constexpr std::uint64_t WindowBytes = std::uint64_t{1} << Abi::WindowShift;

Abi::PageEntry Segment(const Abi::Range& range, std::uint64_t start) {
    const auto first = range.begin > start ? static_cast<std::uint32_t>(range.begin - start) : 0u;
    return Abi::SegmentEntry(range.deviceAddress - range.begin, first, std::min(range.end - start, Abi::LimitMax), range.permissions);
}

Abi::PageEntry Encode(std::vector<Abi::PageEntry>& entries, std::span<const Abi::Range> ranges, std::uint64_t start, std::uint32_t shift, const std::vector<std::size_t>& meeting) {
    if (meeting.size() == 1u) return Segment(ranges[meeting.front()], start);
    Require(shift >= Abi::SplitShift, "two BDA page ranges meet in one byte");
    const auto block = entries.size();
    entries.resize(block + Abi::SplitChildren);
    const auto childShift = shift - Abi::SplitShift;
    for (std::uint32_t child = 0; child < Abi::SplitChildren; ++child) {
        const auto childStart = start + (static_cast<std::uint64_t>(child) << childShift);
        const auto childEnd = childStart + (std::uint64_t{1} << childShift);
        std::vector<std::size_t> inside;
        for (const auto index : meeting) {
            if (ranges[index].begin < childEnd && childStart < ranges[index].end) inside.push_back(index);
        }
        if (inside.empty()) continue;
        const auto entry = Encode(entries, ranges, childStart, childShift, inside);
        entries[block + child] = entry;
    }
    return Abi::SplitEntry(block * sizeof(Abi::PageEntry));
}

struct Newest {
    std::mutex mutex;
    VkDevice device = VK_NULL_HANDLE;
    std::weak_ptr<const BdaPageTable> table;
    BdaPageTable::Counts counts;
};

Newest& NewestTable() {
    static Newest newest;
    return newest;
}

}

void ValidateBdaRangeList(std::span<const Abi::Range> ranges) {
    for (std::size_t index = 1; index < ranges.size(); ++index) Require(ranges[index - 1].end <= ranges[index].begin, "BDA ranges are unsorted or overlap");
}

void ValidateBdaGrants(std::span<const Abi::Grant> grants) {
    for (std::size_t index = 0; index < grants.size(); ++index) {
        Require(grants[index].begin < grants[index].end, "a BDA write grant is empty");
        Require(index == 0 || grants[index - 1].end < grants[index].begin, "BDA write grants are unsorted, overlap or touch");
    }
}

void ValidateBdaPageRanges(std::span<const Abi::Range> ranges) {
    ValidateBdaRangeList(ranges);
    for (const auto& range : ranges) {
        Require(range.begin < range.end && range.end <= Abi::PageTableEnd, "a BDA page range is empty or ends past the page table");
        Require(range.deviceAddress != 0 && range.end - range.begin <= std::numeric_limits<std::uint64_t>::max() - range.deviceAddress, "a BDA page range has no device address or its device range wraps");
        Require(range.reserved == 0 && (range.permissions & ~(Abi::Read | Abi::Write)) == 0, "a BDA page range has unknown permissions or reserved bits");
    }
}

std::vector<Abi::PageEntry> EncodeBdaWindow(std::uint32_t window, std::span<const Abi::Range> ranges) {
    std::vector<Abi::PageEntry> entries(Abi::WindowPages);
    const auto start = static_cast<std::uint64_t>(window) << Abi::WindowShift;
    const auto end = start + WindowBytes;
    std::vector<std::pair<std::uint64_t, std::size_t>> partial;
    for (std::size_t index = 0; index < ranges.size(); ++index) {
        const auto& range = ranges[index];
        Require(range.begin < end && start < range.end, "a BDA page range does not meet its window");
        const auto first = (std::max(range.begin, start) - start) >> Abi::PageShift;
        const auto last = (std::min(range.end, end) - 1u - start) >> Abi::PageShift;
        for (auto page = first; page <= last; ++page) {
            const auto pageStart = start + (page << Abi::PageShift);
            if (range.begin <= pageStart && pageStart + PageBytes <= range.end) entries[page] = Segment(range, pageStart);
            else partial.emplace_back(page, index);
        }
    }
    for (std::size_t at = 0; at < partial.size();) {
        const auto page = partial[at].first;
        std::vector<std::size_t> meeting;
        for (; at < partial.size() && partial[at].first == page; ++at) meeting.push_back(partial[at].second);
        const auto entry = Encode(entries, ranges, start + (page << Abi::PageShift), Abi::PageShift, meeting);
        entries[page] = entry;
    }
    return entries;
}

std::shared_ptr<const BdaPageTable> BdaPageTable::Build(const Context& context, std::span<const Abi::Range> ranges) {
    ValidateBdaPageRanges(ranges);
    auto& newest = NewestTable();
    std::shared_ptr<const BdaPageTable> previous;
    {
        std::lock_guard lock(newest.mutex);
        if (newest.device == context.device) previous = newest.table.lock();
    }
    std::map<std::uint32_t, std::vector<Abi::Range>> windows;
    for (const auto& range : ranges) {
        const auto last = static_cast<std::uint32_t>((range.end - 1u) >> Abi::WindowShift);
        for (auto window = static_cast<std::uint32_t>(range.begin >> Abi::WindowShift); window <= last; ++window) windows[window].push_back(range);
    }
    std::shared_ptr<BdaPageTable> table(new BdaPageTable());
    std::vector<std::uint64_t> addresses(Abi::WindowCount, 0u);
    std::uint64_t shared = 0;
    for (auto& [window, held] : windows) {
        auto old = previous != nullptr ? previous->find(window) : nullptr;
        if (old != nullptr && old->ranges.size() == held.size() && std::memcmp(old->ranges.data(), held.data(), held.size() * sizeof(Abi::Range)) == 0) {
            table->leaves.push_back(std::move(old));
            ++shared;
        } else {
            const auto entries = EncodeBdaWindow(window, held);
            auto leaf = std::make_shared<Leaf>();
            leaf->window = window;
            leaf->buffer = std::make_shared<Buffer>(context, entries.size() * sizeof(Abi::PageEntry), TableUsage);
            std::memcpy(leaf->buffer->Bytes().data(), entries.data(), entries.size() * sizeof(Abi::PageEntry));
            leaf->ranges = std::move(held);
            table->leaves.push_back(std::move(leaf));
        }
        const auto address = table->leaves.back()->buffer->DeviceAddress();
        Require(address != 0 && address % sizeof(Abi::PageEntry) == 0, "a BDA page table leaf is not aligned for its entries");
        addresses[window] = address;
    }
    table->first = std::make_shared<Buffer>(context, addresses.size() * sizeof(std::uint64_t), TableUsage);
    std::memcpy(table->first->Bytes().data(), addresses.data(), addresses.size() * sizeof(std::uint64_t));
    Require(table->first->DeviceAddress() % sizeof(std::uint64_t) == 0, "the BDA page table is not aligned for its entries");
    std::shared_ptr<const BdaPageTable> built = std::move(table);
    std::lock_guard lock(newest.mutex);
    newest.device = context.device;
    newest.table = built;
    ++newest.counts.tables;
    newest.counts.sharedWindows += shared;
    newest.counts.encodedWindows += windows.size() - shared;
    return built;
}

BdaPageTable::Counts BdaPageTable::Totals() {
    auto& newest = NewestTable();
    std::lock_guard lock(newest.mutex);
    return newest.counts;
}

VkDeviceAddress BdaPageTable::Address() const {
    return first->DeviceAddress();
}

VkDeviceAddress BdaPageTable::LeafAddress(std::uint32_t window) const {
    const auto leaf = find(window);
    return leaf != nullptr ? leaf->buffer->DeviceAddress() : 0u;
}

std::shared_ptr<const BdaPageTable::Leaf> BdaPageTable::find(std::uint32_t window) const {
    const auto found = std::lower_bound(leaves.begin(), leaves.end(), window, [](const auto& leaf, std::uint32_t value) { return leaf->window < value; });
    return found != leaves.end() && (*found)->window == window ? *found : nullptr;
}

}
