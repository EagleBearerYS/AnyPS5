#include "prx/libSceAgcDriver/Execution/include/MemoryCensus.hpp"
#include <algorithm>
#include <cstdio>
#include <limits>
#include <map>
#include <optional>

namespace AgcDriver::MemoryCensus {
namespace {

std::string_view trimmed(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\n' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

std::string_view token(std::string_view& text) {
    text = trimmed(text);
    const auto end = text.find_first_of(" \t");
    const auto word = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end);
    return word;
}

std::optional<std::uint64_t> number(std::string_view text, unsigned base) {
    if (text.empty()) return std::nullopt;
    std::uint64_t value = 0;
    for (const char c : text) {
        unsigned digit = 0;
        if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A' + 10);
        else return std::nullopt;
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / base) return std::nullopt;
        value = value * base + digit;
    }
    return value;
}

std::optional<std::uint64_t> kilobytes(std::string_view line, std::string_view name) {
    if (line.size() <= name.size() || line.substr(0, name.size()) != name || line[name.size()] != ':') return std::nullopt;
    auto rest = line.substr(name.size() + 1);
    const auto value = number(token(rest), 10);
    if (!value || token(rest) != "kB" || *value > std::numeric_limits<std::uint64_t>::max() / 1024u) return std::nullopt;
    return *value * 1024u;
}

}

RegionClass ClassifyMapping(std::uint64_t begin, std::uint64_t end, std::string_view permissions, std::uint64_t inode, std::string_view path, std::uint64_t arenaBegin, std::uint64_t arenaEnd) {
    if (begin < arenaEnd && end > arenaBegin) return RegionClass::Arena;
    if (permissions.size() >= 4 && permissions[3] == 's') return RegionClass::Mapped;
    if (path.substr(0, 7) == "/memfd:" || path.substr(0, 5) == "/SYSV" || path.substr(0, 5) == "/dev/") return RegionClass::Mapped;
    if (inode != 0) return RegionClass::Image;
    return RegionClass::Private;
}

void SmapsParser::Line(std::string_view line) {
    line = trimmed(line);
    if (line.empty()) return;
    const auto dash = line.find('-');
    const auto space = line.find(' ');
    if (dash != std::string_view::npos && space != std::string_view::npos && dash < space) {
        const auto begin = number(line.substr(0, dash), 16);
        const auto end = number(line.substr(dash + 1, space - dash - 1), 16);
        if (begin && end && *begin <= *end) {
            auto rest = line.substr(space);
            const auto permissions = token(rest);
            token(rest);
            token(rest);
            const auto inode = number(token(rest), 10);
            const auto path = trimmed(rest);
            current = ClassifyMapping(*begin, *end, permissions, inode.value_or(0), path, arenaBegin, arenaEnd);
            inRegion = true;
            ++totals.regions;
            if (recordPrivate && current == RegionClass::Private) privateMappings.push_back({*begin, *end, 0, std::string(permissions), std::string(path)});
            return;
        }
    }
    if (!inRegion) return;
    const auto index = static_cast<std::size_t>(current);
    if (const auto rss = kilobytes(line, "Rss")) {
        totals.resident[index] += *rss;
        if (recordPrivate && current == RegionClass::Private && !privateMappings.empty()) privateMappings.back().resident += *rss;
    } else if (const auto swap = kilobytes(line, "Swap")) {
        totals.swapped[index] += *swap;
    }
}

StatusTotals ParseStatus(std::string_view text) {
    StatusTotals totals;
    while (!text.empty()) {
        const auto end = text.find('\n');
        const auto line = trimmed(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (const auto value = kilobytes(line, "VmRSS")) totals.rss = *value;
        else if (const auto anonymous = kilobytes(line, "RssAnon")) totals.anonymous = *anonymous;
        else if (const auto file = kilobytes(line, "RssFile")) totals.file = *file;
        else if (const auto shmem = kilobytes(line, "RssShmem")) totals.shmem = *shmem;
        else if (const auto swap = kilobytes(line, "VmSwap")) totals.swap = *swap;
    }
    return totals;
}

std::int64_t Unattributed(std::uint64_t total, std::initializer_list<std::uint64_t> named) {
    constexpr auto limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    std::uint64_t sum = 0;
    for (const auto part : named) sum = part > limit - std::min(sum, limit) ? limit : sum + part;
    const auto clampedTotal = std::min(total, limit);
    return clampedTotal >= sum ? static_cast<std::int64_t>(clampedTotal - sum) : -static_cast<std::int64_t>(sum - clampedTotal);
}

namespace {

bool listed(std::string_view list, std::string_view item) {
    while (!list.empty()) {
        const auto slash = list.find('/');
        if (list.substr(0, slash) == item) return true;
        if (slash == std::string_view::npos) break;
        list.remove_prefix(slash + 1);
    }
    return false;
}

}

void AllocationGroups::Add(std::uint64_t group, std::uint64_t end, std::uint64_t bytes, std::string_view protection, std::string_view name) {
    if (groups.empty() || groups.back().base != group) groups.push_back({group, end, 0, 0, {}, std::string(name), 0});
    auto& current = groups.back();
    current.end = std::max(current.end, end);
    current.bytes += bytes;
    ++current.regions;
    if (current.name.empty() && !name.empty()) current.name = name;
    if (!protection.empty() && !listed(current.protections, protection)) current.protections += (current.protections.empty() ? "" : "/") + std::string(protection);
}

void AllocationGroups::Tag(std::span<const std::uint64_t> addresses, std::uint32_t tag) {
    std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) { return a.base < b.base; });
    for (const auto address : addresses) {
        auto found = std::upper_bound(groups.begin(), groups.end(), address, [](std::uint64_t value, const Group& group) { return value < group.base; });
        if (found == groups.begin()) continue;
        --found;
        if (address < found->end) found->tags |= tag;
    }
}

std::size_t SizeClassOf(std::uint64_t bytes) {
    constexpr std::uint64_t Mebibyte = 1ull << 20u;
    return bytes < Mebibyte ? 0u : bytes < 4u * Mebibyte ? 1u : bytes < 16u * Mebibyte ? 2u : bytes < 64u * Mebibyte ? 3u : 4u;
}

std::array<AllocationGroups::SizeClass, AllocationGroups::SizeClassCount> AllocationGroups::Histogram() const {
    std::array<SizeClass, SizeClassCount> classes{};
    for (const auto& group : groups) {
        auto& size = classes[SizeClassOf(group.bytes)];
        ++size.count;
        size.bytes += group.bytes;
    }
    return classes;
}

std::vector<AllocationGroups::Group> AllocationGroups::Largest(std::size_t count) const {
    auto largest = groups;
    const auto order = [](const Group& a, const Group& b) { return a.bytes != b.bytes ? a.bytes > b.bytes : a.base < b.base; };
    const auto kept = std::min(count, largest.size());
    std::partial_sort(largest.begin(), largest.begin() + static_cast<std::ptrdiff_t>(kept), largest.end(), order);
    largest.resize(kept);
    return largest;
}

std::uint64_t AllocationGroups::Bytes() const {
    std::uint64_t total = 0;
    for (const auto& group : groups) total += group.bytes;
    return total;
}

std::size_t AllocationGroups::Tagged(std::uint32_t tag) const {
    return static_cast<std::size_t>(std::count_if(groups.begin(), groups.end(), [&](const Group& group) { return (group.tags & tag) != 0; }));
}

std::string GroupsText(const AllocationGroups& groups, std::size_t largest) {
    constexpr std::array<const char*, AllocationGroups::SizeClassCount> SizeNames{"<1 MiB", "1-4 MiB", "4-16 MiB", "16-64 MiB", ">=64 MiB"};
    const auto mib = [](std::uint64_t bytes) { return static_cast<double>(bytes) / 1048576.0; };
    char text[256];
    std::snprintf(text, sizeof(text), "%zu allocations %.1f MiB; by size", groups.Count(), mib(groups.Bytes()));
    std::string line = text;
    const auto classes = groups.Histogram();
    for (std::size_t index = 0; index < classes.size(); ++index) {
        std::snprintf(text, sizeof(text), "%s %s %llu (%.1f MiB)", index == 0 ? "" : ",", SizeNames[index], static_cast<unsigned long long>(classes[index].count), mib(classes[index].bytes));
        line += text;
    }
    line += "; largest:";
    const auto top = groups.Largest(largest);
    for (std::size_t index = 0; index < top.size(); ++index) {
        const auto& group = top[index];
        std::snprintf(text, sizeof(text), "%s 0x%llx %.1f MiB %u regions %s", index == 0 ? "" : ",", static_cast<unsigned long long>(group.base), mib(group.bytes), group.regions, group.protections.empty() ? "-" : group.protections.c_str());
        line += text;
        if (!group.name.empty()) line += " " + group.name;
        if ((group.tags & TagHeapBlock) != 0) line += " (heap block)";
        if ((group.tags & TagDeviceMemory) != 0) line += " (device memory)";
    }
    if (top.empty()) line += " none";
    return line;
}

std::uint64_t Descriptors(const PoolShape& shape) {
    std::uint64_t total = 0;
    for (const auto& size : shape.sizes) total += size.second;
    return total;
}

ProbePlan PlanProbes(std::span<const LivePool> pools, std::size_t limit) {
    std::map<std::pair<bool, PoolShape>, PoolGroup> groups;
    for (std::size_t index = 0; index < pools.size(); ++index) {
        const auto& pool = pools[index];
        auto [found, inserted] = groups.try_emplace({pool.probeable, pool.shape});
        auto& group = found->second;
        if (inserted) {
            group.shape = pool.shape;
            group.probeable = pool.probeable;
            group.representative = index;
        } else if (pool.sets > pools[group.representative].sets) {
            group.representative = index;
        }
        ++group.pools;
        group.sets += pool.sets;
    }
    std::vector<PoolGroup> ordered;
    ordered.reserve(groups.size());
    for (auto& [key, group] : groups) ordered.push_back(std::move(group));
    std::stable_sort(ordered.begin(), ordered.end(), [](const PoolGroup& a, const PoolGroup& b) { return Descriptors(a.shape) * a.pools > Descriptors(b.shape) * b.pools; });
    ProbePlan plan;
    for (auto& group : ordered) (group.probeable && plan.probed.size() < limit ? plan.probed : plan.skipped).push_back(std::move(group));
    return plan;
}

std::uint64_t ProbeEstimate(const PoolGroup& group, const ProbeMeasure& measure) {
    const auto pool = static_cast<std::uint64_t>(std::max<std::int64_t>(measure.pool, 0));
    const auto sets = static_cast<std::uint64_t>(std::max<std::int64_t>(measure.sets, 0));
    const auto perSets = measure.probedSets != 0 ? static_cast<std::uint64_t>(static_cast<double>(sets) * static_cast<double>(group.sets) / static_cast<double>(measure.probedSets)) : 0u;
    return group.pools * pool + perSets;
}

}
