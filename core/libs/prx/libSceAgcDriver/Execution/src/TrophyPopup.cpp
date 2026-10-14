#include "prx/libSceAgcDriver/Execution/include/TrophyPopup.hpp"
#include "prx/libSceNpTrophy2/include/TrophyStore.hpp"
#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace AgcDriver {
namespace {

std::size_t longestLine(const std::vector<std::string>& lines) {
    std::size_t columns = 0;
    for (const auto& line : lines) columns = std::max(columns, line.size());
    return columns;
}

std::int32_t popupWidth(std::size_t columns, std::int32_t scale) {
    return (static_cast<std::int32_t>(columns) * OverlayMetrics::Advance - 1 + 2 * OverlayMetrics::Padding) * scale;
}

std::int32_t popupHeight(std::size_t lines, std::int32_t scale) {
    const auto glyphGap = OverlayMetrics::LinePitch - OverlayMetrics::GlyphHeight;
    return (2 * OverlayMetrics::Padding + static_cast<std::int32_t>(lines) * OverlayMetrics::LinePitch - glyphGap) * scale;
}

std::size_t sequenceLength(std::string_view text, std::size_t at) {
    const auto lead = static_cast<unsigned char>(text[at]);
    std::size_t length = 0;
    if (lead >= 0xc2 && lead <= 0xdf) length = 2;
    else if (lead >= 0xe0 && lead <= 0xef) length = 3;
    else if (lead >= 0xf0 && lead <= 0xf4) length = 4;
    if (length == 0 || at + length > text.size()) return 1;
    for (std::size_t next = at + 1; next < at + length; ++next) {
        if ((static_cast<unsigned char>(text[next]) & 0xc0u) != 0x80u) return 1;
    }
    return length;
}

void queueUnlock(void* context, const NpTrophy2Unlock& unlock) {
    static_cast<TrophyPopupQueue*>(context)->Push(TrophyPopupEntry{unlock.trophyId, {}, 0});
}

}

TrophyPopupSettings ParseTrophyPopupSettings(const char* value) {
    TrophyPopupSettings parsed;
    const std::string_view text = value != nullptr ? value : "";
    if (text.empty() || text == "1") return parsed;
    if (text == "0") parsed.enabled = false;
    else if (text == "tl") parsed.corner = OverlayCorner::TopLeft;
    else if (text == "tr") parsed.corner = OverlayCorner::TopRight;
    else if (text == "bl") parsed.corner = OverlayCorner::BottomLeft;
    else if (text == "br") parsed.corner = OverlayCorner::BottomRight;
    else throw std::runtime_error("ANYPS5_TROPHY_POPUP must be 1, 0, tl, tr, bl or br, not '" + std::string(text) + "'");
    return parsed;
}

OverlayCorner OppositeOverlayCorner(OverlayCorner corner) {
    switch (corner) {
        case OverlayCorner::TopLeft: return OverlayCorner::BottomRight;
        case OverlayCorner::TopRight: return OverlayCorner::BottomLeft;
        case OverlayCorner::BottomLeft: return OverlayCorner::TopRight;
        case OverlayCorner::BottomRight: return OverlayCorner::TopLeft;
    }
    throw std::invalid_argument("unknown overlay corner");
}

OverlayCorner TrophyPopupCorner(const TrophyPopupSettings& settings, OverlayCorner overlay) {
    return settings.corner ? *settings.corner : OppositeOverlayCorner(overlay);
}

std::string TrophyPopupText(std::string_view text, std::size_t columns) {
    std::string shown;
    for (std::size_t at = 0; at < text.size();) {
        const auto character = text[at];
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x80) {
            if (byte < 0x20 || byte == 0x7f) shown.push_back(' ');
            else shown.push_back(OverlayHasGlyph(character) ? character : '?');
            ++at;
            continue;
        }
        shown.push_back('?');
        at += sequenceLength(text, at);
    }
    if (shown.size() > columns) shown = shown.substr(0, columns > 3 ? columns - 3 : 0) + std::string(std::min<std::size_t>(columns, 3), '.');
    return shown;
}

std::vector<std::string> TrophyPopupLines(const TrophyPopupEntry& entry) {
    std::vector<std::string> lines{"TROPHY UNLOCKED"};
    const auto name = TrophyPopupText(entry.name, TrophyPopupMetrics::Columns);
    lines.push_back(name.find_first_not_of(' ') == std::string::npos ? "TROPHY " + std::to_string(entry.trophyId) : name);
    switch (entry.grade) {
        case 'P': lines.push_back("PLATINUM"); break;
        case 'G': lines.push_back("GOLD"); break;
        case 'S': lines.push_back("SILVER"); break;
        case 'B': lines.push_back("BRONZE"); break;
        default: break;
    }
    return lines;
}

std::int32_t TrophyPopupScale(VkExtent2D extent, const std::vector<std::string>& lines) {
    const auto columns = longestLine(lines);
    const auto fits = [&](std::int32_t scale) {
        const auto margins = 2 * OverlayMetrics::Margin * scale;
        return popupWidth(columns, scale) + margins <= static_cast<std::int32_t>(extent.width) && popupHeight(lines.size(), scale) + margins <= static_cast<std::int32_t>(extent.height);
    };
    auto scale = std::max<std::int32_t>(1, static_cast<std::int32_t>((extent.height + 240) / 480));
    while (scale > 1 && !fits(scale)) --scale;
    return scale;
}

OverlayLayout LayOutTrophyPopup(const std::vector<std::string>& lines, VkExtent2D extent, OverlayCorner corner) {
    Graphics::Require(extent.width != 0 && extent.height != 0, "the trophy pop-up needs a destination");
    Graphics::Require(!lines.empty(), "the trophy pop-up has no text");
    for (const auto& line : lines) Graphics::Require(line.size() <= TrophyPopupMetrics::Columns, "a trophy pop-up line is wider than the pop-up");
    OverlayLayout layout;
    const auto scale = TrophyPopupScale(extent, lines);
    layout.scale = scale;
    const auto width = popupWidth(longestLine(lines), scale);
    const auto height = popupHeight(lines.size(), scale);
    const auto margin = OverlayMetrics::Margin * scale;
    const bool right = corner == OverlayCorner::TopRight || corner == OverlayCorner::BottomRight;
    const bool bottom = corner == OverlayCorner::BottomLeft || corner == OverlayCorner::BottomRight;
    const auto left = std::max(0, right ? static_cast<std::int32_t>(extent.width) - margin - width : margin);
    const auto top = std::max(0, bottom ? static_cast<std::int32_t>(extent.height) - margin - height : margin);
    layout.bounds = {left, top, width, height, static_cast<std::uint32_t>(OverlayColor::Background)};
    layout.rects.push_back(layout.bounds);
    auto row = OverlayMetrics::Padding;
    for (const auto& line : lines) {
        AppendOverlayLine(layout.rects, line, left, top, row, scale);
        row += OverlayMetrics::LinePitch;
    }
    return layout;
}

void TrophyPopupQueue::Push(TrophyPopupEntry entry) {
    std::lock_guard lock(mutex);
    entries.push_back(std::move(entry));
    pending.store(true, std::memory_order_relaxed);
}

std::optional<TrophyPopupEntry> TrophyPopupQueue::Current(Clock::time_point now) {
    std::lock_guard lock(mutex);
    while (!entries.empty()) {
        if (!shownSince) shownSince = now;
        if (now - *shownSince < ShowFor) return entries.front();
        entries.pop_front();
        shownSince.reset();
    }
    pending.store(false, std::memory_order_relaxed);
    return std::nullopt;
}

std::size_t TrophyPopupQueue::Size() const {
    std::lock_guard lock(mutex);
    return entries.size();
}

const TrophyPopupSettings& TrophyPopupConfiguration() {
    static const TrophyPopupSettings parsed = ParseTrophyPopupSettings(std::getenv("ANYPS5_TROPHY_POPUP"));
    return parsed;
}

TrophyPopupQueue& TrophyPopups() {
    static auto* queue = new TrophyPopupQueue;
    return *queue;
}

bool ListenForTrophyUnlocks() {
    static const bool listening = [] {
        if (!TrophyPopupConfiguration().enabled) return false;
        static_cast<void>(NpTrophy2AddUnlockListener_nid_no_patch(&queueUnlock, &TrophyPopups()));
        return true;
    }();
    return listening;
}

}
