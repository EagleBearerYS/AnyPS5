#include "prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawCacheBypass.hpp"

namespace AgcDriver::DriverDetail {

bool DrawCacheBypassWindow::Bypass() {
    auto left = idle.load(std::memory_order_relaxed);
    while (left != 0) {
        if (idle.compare_exchange_weak(left, left - 1, std::memory_order_relaxed)) return true;
    }
    return false;
}

bool DrawCacheBypassWindow::Note(bool hit, std::uint32_t stages) {
    if (hit) hits.fetch_add(1, std::memory_order_relaxed);
    if (stages != 0) stageHits.fetch_add(stages, std::memory_order_relaxed);
    if (lookups.fetch_add(1, std::memory_order_relaxed) + 1 < Window) return false;
    const auto windowHits = hits.exchange(0, std::memory_order_relaxed);
    const auto windowStageHits = stageHits.exchange(0, std::memory_order_relaxed);
    lookups.store(0, std::memory_order_relaxed);
    if (windowHits >= MinimumHits || windowStageHits >= MinimumHits) return false;
    idle.store(IdleDraws, std::memory_order_relaxed);
    return true;
}

}
