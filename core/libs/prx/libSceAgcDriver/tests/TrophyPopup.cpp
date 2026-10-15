#include "prx/libSceAgcDriver/Execution/include/TrophyPopup.hpp"
#include "prx/libSceNpTrophy2/include/TrophyStore.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace std::chrono_literals;
using AgcDriver::OverlayCorner;
using AgcDriver::OverlayMetrics;
using AgcDriver::TrophyPopupEntry;
using AgcDriver::TrophyPopupQueue;

void require(bool value, const std::string& reason) {
    if (value) return;
    std::fprintf(stderr, "%s\n", reason.c_str());
    std::abort();
}

bool refused(const char* value) {
    try {
        static_cast<void>(AgcDriver::ParseTrophyPopupSettings(value));
    } catch (const std::exception& error) {
        return std::string_view(error.what()).find("ANYPS5_TROPHY_POPUP") != std::string_view::npos;
    }
    return false;
}

void settingsTests() {
    for (const char* shown : {static_cast<const char*>(nullptr), "", "1"}) {
        const auto settings = AgcDriver::ParseTrophyPopupSettings(shown);
        require(settings.enabled && !settings.corner, "an unset, empty or 1 ANYPS5_TROPHY_POPUP does not show the pop-up in its default corner");
    }
    require(!AgcDriver::ParseTrophyPopupSettings("0").enabled, "ANYPS5_TROPHY_POPUP=0 does not turn the pop-up off");
    const std::pair<const char*, OverlayCorner> corners[] = {{"tl", OverlayCorner::TopLeft}, {"tr", OverlayCorner::TopRight}, {"bl", OverlayCorner::BottomLeft}, {"br", OverlayCorner::BottomRight}};
    for (const auto& [name, corner] : corners) {
        const auto settings = AgcDriver::ParseTrophyPopupSettings(name);
        require(settings.enabled && settings.corner == corner, std::string("ANYPS5_TROPHY_POPUP=") + name + " does not choose its corner");
        for (const auto overlay : {OverlayCorner::TopLeft, OverlayCorner::TopRight, OverlayCorner::BottomLeft, OverlayCorner::BottomRight}) {
            require(AgcDriver::TrophyPopupCorner(settings, overlay) == corner, "a chosen pop-up corner follows the overlay's");
        }
    }
    for (const char* invalid : {"2", "on", "off", "TL", " br", "br ", "true", "00"}) require(refused(invalid), std::string("ANYPS5_TROPHY_POPUP='") + invalid + "' is accepted or its error does not name the variable");
    const std::pair<OverlayCorner, OverlayCorner> opposite[] = {{OverlayCorner::TopLeft, OverlayCorner::BottomRight}, {OverlayCorner::TopRight, OverlayCorner::BottomLeft}, {OverlayCorner::BottomLeft, OverlayCorner::TopRight}, {OverlayCorner::BottomRight, OverlayCorner::TopLeft}};
    for (const auto& [overlay, popup] : opposite) {
        require(AgcDriver::OppositeOverlayCorner(overlay) == popup, "the opposite corner is wrong");
        require(AgcDriver::TrophyPopupCorner(AgcDriver::TrophyPopupSettings{}, overlay) == popup, "the default pop-up corner is not opposite the overlay");
    }
    const auto& configured = AgcDriver::TrophyPopupConfiguration();
    require(configured.enabled && !configured.corner, "without ANYPS5_TROPHY_POPUP the pop-up is off or in a fixed corner");
}

std::int32_t shownId(TrophyPopupQueue& queue, TrophyPopupQueue::Clock::time_point at) {
    const auto shown = queue.Current(at);
    return shown ? shown->trophyId : -1;
}

void queueTests() {
    TrophyPopupQueue queue;
    const auto start = TrophyPopupQueue::Clock::time_point{} + 1h;
    require(!queue.Pending() && shownId(queue, start) == -1, "an empty queue shows a pop-up");
    queue.Push({12, {}, 0});
    queue.Push({13, {}, 0});
    require(queue.Pending() && queue.Size() == 2, "two unlocks were not queued");
    require(shownId(queue, start) == 12 && shownId(queue, start + 4999ms) == 12, "the first unlock is not shown for 5 s");
    require(shownId(queue, start + 5s) == 13 && shownId(queue, start + 9999ms) == 13, "the second unlock does not follow the first one");
    require(shownId(queue, start + 10s) == -1 && !queue.Pending() && queue.Size() == 0, "the queue does not empty after both unlocks were shown");

    const auto late = start + 60s;
    queue.Push({14, {}, 0});
    require(shownId(queue, late) == 14, "an unlock queued while no frame was presented was not shown");
    queue.Push({15, {}, 0});
    require(shownId(queue, late + 4s) == 14 && shownId(queue, late + 5s) == 15, "an unlock queued while another one shows does not wait for it");
    require(shownId(queue, late + 10s) == -1, "the queue does not empty");

    const auto gap = start + 120s;
    queue.Push({16, {}, 0});
    queue.Push({17, {}, 0});
    require(shownId(queue, gap) == 16, "the first unlock is not shown");
    require(shownId(queue, gap + 12s) == 17 && shownId(queue, gap + 16999ms) == 17 && shownId(queue, gap + 17s) == -1, "a pause between presents skips or shortens the next unlock");

    const auto burst = start + 300s;
    for (std::int32_t id = 20; id < 25; ++id) queue.Push({id, {}, 0});
    for (std::int32_t index = 0; index < 5; ++index) {
        require(shownId(queue, burst + index * 5s) == 20 + index && shownId(queue, burst + index * 5s + 4999ms) == 20 + index, "five unlocks at once are not shown one after another, 5 s each");
    }
    require(shownId(queue, burst + 25s) == -1 && !queue.Pending(), "five unlocks at once do not end");
}

void linesTests() {
    using Lines = std::vector<std::string>;
    require(AgcDriver::TrophyPopupLines({12, {}, 0}) == Lines{"TROPHY UNLOCKED", "TROPHY 12"}, "an unlock without a name does not show its id");
    require(AgcDriver::TrophyPopupLines({12, "   ", 0}) == Lines{"TROPHY UNLOCKED", "TROPHY 12"}, "a blank name does not show the id");
    require(AgcDriver::TrophyPopupLines({7, "The Lady of the House", 'G'}) == Lines{"TROPHY UNLOCKED", "The Lady of the House", "GOLD"}, "a name and grade are not shown");
    const std::pair<char, const char*> grades[] = {{'P', "PLATINUM"}, {'G', "GOLD"}, {'S', "SILVER"}, {'B', "BRONZE"}};
    for (const auto& [grade, text] : grades) require(AgcDriver::TrophyPopupLines({1, "A", grade}).back() == text, std::string("grade ") + grade + " is not shown as " + text);
    require(AgcDriver::TrophyPopupLines({1, "A", 'X'}).size() == 2, "an unknown grade is shown");

    const auto text = [](std::string_view value) { return AgcDriver::TrophyPopupText(value, AgcDriver::TrophyPopupMetrics::Columns); };
    require(text("Caf\xc3\xa9 \xe2\x98\x95 100%") == "Caf? ? 100%", "UTF-8 characters without a glyph are not one '?' each");
    require(text("\xf0\x9f\x8f\x86 Gold") == "? Gold", "a four-byte character is not one '?'");
    require(text("\xff\xfe") == "??" && text("a\xe2\x98") == "a??" && text("\xc3") == "?", "invalid or cut UTF-8 is not one '?' per byte");
    require(text("Tab\tand\nnew line") == "Tab and new line", "control characters are not shown as spaces");
    require(text("user@site ~ [1]") == "user?site ? ?1?", "ASCII without a glyph is not shown as '?'");
    const std::string longName(40, 'n');
    require(text(longName) == std::string(29, 'n') + "...", "a name longer than the pop-up is not cut with '...'");
    require(text(std::string(32, 'n')) == std::string(32, 'n'), "a name that fits is cut");
    require(AgcDriver::TrophyPopupText("abcdef", 2) == "..", "a narrow cut is not dots only");
}

std::vector<std::uint8_t> textMask(const AgcDriver::OverlayLayout& layout) {
    std::vector<std::uint8_t> covered(static_cast<std::size_t>(layout.bounds.width) * static_cast<std::size_t>(layout.bounds.height), 0);
    for (std::size_t index = 1; index < layout.rects.size(); ++index) {
        const auto& rect = layout.rects[index];
        require(rect.color == static_cast<std::uint32_t>(AgcDriver::OverlayColor::Text), "a pop-up rectangle after the background is not text");
        require(rect.x >= layout.bounds.x && rect.y >= layout.bounds.y && rect.x + rect.width <= layout.bounds.x + layout.bounds.width && rect.y + rect.height <= layout.bounds.y + layout.bounds.height, "pop-up text leaves the pop-up");
        for (std::int32_t y = rect.y; y < rect.y + rect.height; ++y) {
            for (std::int32_t x = rect.x; x < rect.x + rect.width; ++x) ++covered[static_cast<std::size_t>(y - layout.bounds.y) * static_cast<std::size_t>(layout.bounds.width) + static_cast<std::size_t>(x - layout.bounds.x)];
        }
    }
    return covered;
}

void requireLinesDrawn(const AgcDriver::OverlayLayout& layout, const std::vector<std::string>& lines) {
    const auto mask = textMask(layout);
    const auto s = layout.scale;
    std::size_t lit = 0;
    for (const auto value : mask) {
        require(value <= 1, "pop-up text rectangles overlap");
        lit += value;
    }
    std::size_t expected = 0;
    for (std::size_t line = 0; line < lines.size(); ++line) {
        const auto top = OverlayMetrics::Padding + static_cast<std::int32_t>(line) * OverlayMetrics::LinePitch;
        for (std::size_t column = 0; column < lines[line].size(); ++column) {
            const auto& glyph = AgcDriver::OverlayGlyph(lines[line][column]);
            for (std::int32_t gy = 0; gy < OverlayMetrics::GlyphHeight; ++gy) {
                for (std::int32_t gx = 0; gx < OverlayMetrics::Advance; ++gx) {
                    const bool set = gx < OverlayMetrics::GlyphWidth && (glyph[static_cast<std::size_t>(gy)] & (0x10u >> gx)) != 0;
                    if (set) expected += static_cast<std::size_t>(s) * static_cast<std::size_t>(s);
                    for (std::int32_t py = 0; py < s; ++py) {
                        for (std::int32_t px = 0; px < s; ++px) {
                            const auto x = (OverlayMetrics::Padding + static_cast<std::int32_t>(column) * OverlayMetrics::Advance + gx) * s + px;
                            const auto y = (top + gy) * s + py;
                            require(mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(layout.bounds.width) + static_cast<std::size_t>(x)] == (set ? 1 : 0), "'" + std::string(1, lines[line][column]) + "' of '" + lines[line] + "' is drawn wrong at glyph pixel " + std::to_string(gx) + "," + std::to_string(gy));
                        }
                    }
                }
            }
        }
    }
    require(lit == expected, "the pop-up lights pixels outside its glyphs");
}

void layoutTests() {
    const auto lines = AgcDriver::TrophyPopupLines({12, {}, 0});
    const std::pair<VkExtent2D, std::int32_t> scales[] = {{{1280, 720}, 2}, {{1920, 1080}, 2}, {{2560, 1440}, 3}, {{3840, 2160}, 5}, {{320, 180}, 1}};
    for (const auto& [extent, scale] : scales) {
        require(AgcDriver::TrophyPopupScale(extent, lines) == scale, std::to_string(extent.width) + "x" + std::to_string(extent.height) + " draws the pop-up at scale " + std::to_string(AgcDriver::TrophyPopupScale(extent, lines)) + ", not " + std::to_string(scale));
        const auto width = (15 * OverlayMetrics::Advance - 1 + 2 * OverlayMetrics::Padding) * scale;
        const auto height = (2 * OverlayMetrics::Padding + 2 * OverlayMetrics::LinePitch - (OverlayMetrics::LinePitch - OverlayMetrics::GlyphHeight)) * scale;
        const auto margin = OverlayMetrics::Margin * scale;
        for (const auto corner : {OverlayCorner::TopLeft, OverlayCorner::TopRight, OverlayCorner::BottomLeft, OverlayCorner::BottomRight}) {
            const auto layout = AgcDriver::LayOutTrophyPopup(lines, extent, corner);
            const bool right = corner == OverlayCorner::TopRight || corner == OverlayCorner::BottomRight;
            const bool bottom = corner == OverlayCorner::BottomLeft || corner == OverlayCorner::BottomRight;
            const auto x = right ? static_cast<std::int32_t>(extent.width) - margin - width : margin;
            const auto y = bottom ? static_cast<std::int32_t>(extent.height) - margin - height : margin;
            require(layout.scale == scale && layout.bounds.x == x && layout.bounds.y == y && layout.bounds.width == width && layout.bounds.height == height, "the pop-up is not " + std::to_string(width) + "x" + std::to_string(height) + " at " + std::to_string(x) + "," + std::to_string(y) + " in " + std::to_string(extent.width) + "x" + std::to_string(extent.height));
            require(layout.rects.front().x == layout.bounds.x && layout.rects.front().width == width && layout.rects.front().color == static_cast<std::uint32_t>(AgcDriver::OverlayColor::Background), "the pop-up does not start with its background");
            requireLinesDrawn(layout, lines);
        }
    }
    const auto named = AgcDriver::TrophyPopupLines({7, "The Lady's Quest: #1 (100%)!", 'P'});
    const auto layout = AgcDriver::LayOutTrophyPopup(named, {1920, 1080}, OverlayCorner::BottomRight);
    require(layout.bounds.width == (28 * OverlayMetrics::Advance - 1 + 2 * OverlayMetrics::Padding) * 2 && layout.bounds.height == (2 * OverlayMetrics::Padding + 3 * OverlayMetrics::LinePitch - 2) * 2, "the pop-up does not grow with its longest line and its line count");
    requireLinesDrawn(layout, named);
    bool wide = false;
    try {
        static_cast<void>(AgcDriver::LayOutTrophyPopup({std::string(33, 'n')}, {1920, 1080}, OverlayCorner::TopLeft));
    } catch (const std::exception&) {
        wide = true;
    }
    require(wide, "a line wider than the pop-up was laid out");
    const auto tiny = AgcDriver::LayOutTrophyPopup(lines, {64, 16}, OverlayCorner::BottomRight);
    require(tiny.scale == 1 && tiny.bounds.x == 0 && tiny.bounds.y == 0, "a pop-up larger than the window does not start at its corner");
}

void glyphTests() {
    const std::string added = "abcdefghijklmnopqrstuvwxyzJQYZ-+,;'\"!?&()#%_";
    const std::string earlier = "0123456789ABCDEFGHIKLMNOPRSTUVWX.:/";
    for (const char character : added) {
        require(AgcDriver::OverlayHasGlyph(character), std::string("'") + character + "' has no glyph");
        const auto& glyph = AgcDriver::OverlayGlyph(character);
        require(std::any_of(glyph.begin(), glyph.end(), [](std::uint8_t row) { return row != 0; }) && std::all_of(glyph.begin(), glyph.end(), [](std::uint8_t row) { return row <= 0x1f; }), std::string("the glyph of '") + character + "' is empty or wider than 5 pixels");
    }
    const auto all = added + earlier;
    for (std::size_t first = 0; first < all.size(); ++first) {
        for (std::size_t second = first + 1; second < all.size(); ++second) {
            require(AgcDriver::OverlayGlyph(all[first]) != AgcDriver::OverlayGlyph(all[second]), std::string("'") + all[first] + "' and '" + all[second] + "' have the same glyph");
        }
    }
    for (const char missing : std::string("@~[]{}<>=*$^`|\\")) require(!AgcDriver::OverlayHasGlyph(missing), std::string("'") + missing + "' has a glyph");
}

void listenerTest(bool enabled) {
    require(AgcDriver::TrophyPopupConfiguration().enabled == enabled, enabled ? "the pop-up is off without ANYPS5_TROPHY_POPUP" : "ANYPS5_TROPHY_POPUP=0 does not turn the pop-up off");
    require(AgcDriver::ListenForTrophyUnlocks() == enabled && AgcDriver::ListenForTrophyUnlocks() == enabled, enabled ? "the pop-up does not listen for unlocks" : "the pop-up listens for unlocks while it is off");
    const auto previous = std::filesystem::current_path();
    const auto root = std::filesystem::temp_directory_path() / ("anyps5-trophy-popup-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(root / "app0" / "sce_sys");
    std::filesystem::current_path(root);
    std::FILE* param = std::fopen("app0/sce_sys/param.json", "wb");
    require(param != nullptr, "param.json was not created");
    std::fputs(R"({"titleId":"PPSA00000","localizedParameters":{"en-US":{"titleName":"Example"}}})", param);
    std::fclose(param);
    auto& popups = AgcDriver::TrophyPopups();
    require(!popups.Pending() && popups.Size() == 0, "the pop-up queue is not empty at the start");
    require(NpTrophy2RecordUnlock_nid_no_patch(0x10000000, 0, 12), "trophy 12 was not recorded");
    require(!NpTrophy2RecordUnlock_nid_no_patch(0x10000000, 0, 12), "trophy 12 was recorded twice");
    require(NpTrophy2RecordUnlock_nid_no_patch(0x10000000, 0, 13), "trophy 13 was not recorded");
    if (enabled) {
        require(popups.Pending() && popups.Size() == 2, "two new unlocks did not queue two pop-ups");
        const auto now = TrophyPopupQueue::Clock::now();
        require(shownId(popups, now) == 12 && shownId(popups, now + 5s) == 13 && shownId(popups, now + 10s) == -1, "the unlocks are not shown in order");
    } else {
        require(!popups.Pending() && popups.Size() == 0, "unlocks queued pop-ups with ANYPS5_TROPHY_POPUP=0");
    }
    std::filesystem::current_path(previous);
    std::filesystem::remove_all(root);
}

}

int main(int argc, char** argv) {
    const std::string_view mode = argc == 2 ? argv[1] : "";
    if (mode == "listener" || mode == "disabled") {
        listenerTest(mode == "listener");
        std::printf("trophy pop-up %s test passed\n", mode == "listener" ? "listener" : "disabled-listener");
        return 0;
    }
    require(mode.empty(), "unknown test mode");
    settingsTests();
    queueTests();
    linesTests();
    layoutTests();
    glyphTests();
    std::printf("trophy pop-up settings, queue, text, layout and glyph tests passed\n");
    return 0;
}
