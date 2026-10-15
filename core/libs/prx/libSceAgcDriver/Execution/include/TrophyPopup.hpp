#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_TROPHYPOPUP_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_TROPHYPOPUP_HPP

#include "prx/libSceAgcDriver/Execution/include/PerformanceOverlay.hpp"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace AgcDriver {

struct TrophyPopupSettings {
    bool enabled = true;
    std::optional<OverlayCorner> corner;
};

TrophyPopupSettings ParseTrophyPopupSettings(const char* value);
OverlayCorner OppositeOverlayCorner(OverlayCorner corner);
OverlayCorner TrophyPopupCorner(const TrophyPopupSettings& settings, OverlayCorner overlay);

struct TrophyPopupEntry {
    std::int32_t trophyId = 0;
    std::string name;
    char grade = 0;
};

struct TrophyPopupMetrics {
    static constexpr std::size_t Columns = 32;
};

std::string TrophyPopupText(std::string_view text, std::size_t columns);
std::vector<std::string> TrophyPopupLines(const TrophyPopupEntry& entry);
std::int32_t TrophyPopupScale(VkExtent2D extent, const std::vector<std::string>& lines);
OverlayLayout LayOutTrophyPopup(const std::vector<std::string>& lines, VkExtent2D extent, OverlayCorner corner);

class TrophyPopupQueue {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr Clock::duration ShowFor = std::chrono::seconds(5);

    void Push(TrophyPopupEntry entry);
    std::optional<TrophyPopupEntry> Current(Clock::time_point now);
    bool Pending() const { return pending.load(std::memory_order_relaxed); }
    std::size_t Size() const;

private:
    mutable std::mutex mutex;
    std::deque<TrophyPopupEntry> entries;
    std::optional<Clock::time_point> shownSince;
    std::atomic<bool> pending{false};
};

const TrophyPopupSettings& TrophyPopupConfiguration();
TrophyPopupQueue& TrophyPopups();
bool ListenForTrophyUnlocks();

}

#endif
