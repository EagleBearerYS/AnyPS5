#include "prx/libSceAgcDriver/Execution/include/MemoryCensus.hpp"
#include <algorithm>
#include <limits>
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
            return;
        }
    }
    if (!inRegion) return;
    const auto index = static_cast<std::size_t>(current);
    if (const auto rss = kilobytes(line, "Rss")) totals.resident[index] += *rss;
    else if (const auto swap = kilobytes(line, "Swap")) totals.swapped[index] += *swap;
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

}
