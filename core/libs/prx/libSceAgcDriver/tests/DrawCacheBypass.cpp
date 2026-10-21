#include "prx/libSceAgcDriver/Execution/include/Driver/Draw/DrawCacheBypass.hpp"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using AgcDriver::DriverDetail::DrawCacheBypassWindow;

namespace {

int failures = 0;

bool Expect(bool condition, const std::string& what) {
    if (condition) return true;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
    return false;
}

void testMissesBypassThenProbe() {
    DrawCacheBypassWindow window;
    for (std::uint32_t lookup = 1; lookup <= DrawCacheBypassWindow::Window; ++lookup) {
        if (!Expect(!window.Bypass(), "an all-miss window bypassed the cache before it closed, at lookup " + std::to_string(lookup))) return;
        const bool started = window.Note(false, 0);
        if (!Expect(started == (lookup == DrawCacheBypassWindow::Window), "an all-miss window " + std::string(started ? "started" : "did not start") + " a bypass at lookup " + std::to_string(lookup) + " of " + std::to_string(DrawCacheBypassWindow::Window))) return;
    }
    for (std::uint32_t draw = 0; draw < DrawCacheBypassWindow::IdleDraws; ++draw) {
        if (!Expect(window.Bypass(), "the bypass ended after " + std::to_string(draw) + " of its " + std::to_string(DrawCacheBypassWindow::IdleDraws) + " draws")) return;
    }
    if (!Expect(!window.Bypass(), "the bypass lasted beyond its " + std::to_string(DrawCacheBypassWindow::IdleDraws) + " draws")) return;
    for (std::uint32_t lookup = 1; lookup < DrawCacheBypassWindow::Window; ++lookup) {
        if (!Expect(!window.Note(false, 0), "the probe window after the bypass closed at lookup " + std::to_string(lookup))) return;
    }
    Expect(window.Note(false, 0), "an all-miss probe window after the bypass did not bypass the cache again");
}

void testHitsNeverBypass() {
    DrawCacheBypassWindow window;
    for (std::uint32_t lookup = 1; lookup <= 4 * DrawCacheBypassWindow::Window; ++lookup) {
        if (!Expect(!window.Note(true, 0), "an all-hit sequence started a bypass at lookup " + std::to_string(lookup))) return;
        if (!Expect(!window.Bypass(), "an all-hit sequence bypassed the cache at lookup " + std::to_string(lookup))) return;
    }
}

void testStageHitsKeepTheCache() {
    DrawCacheBypassWindow window;
    const auto stageHits = DrawCacheBypassWindow::MinimumHits + 1;
    for (std::uint32_t lookup = 1; lookup <= DrawCacheBypassWindow::Window; ++lookup) {
        const bool started = window.Note(false, lookup <= stageHits ? 1u : 0u);
        if (!Expect(!started, "a window with 0 whole-draw hits and " + std::to_string(stageHits) + " stage hits (threshold " + std::to_string(DrawCacheBypassWindow::MinimumHits) + ") started a bypass at lookup " + std::to_string(lookup))) return;
    }
    Expect(!window.Bypass(), "a window with stage hits above the threshold bypassed the cache");
}

void testConcurrentWindowsCloseOnce() {
    constexpr std::uint32_t Threads = 4, Windows = 256;
    DrawCacheBypassWindow window;
    std::atomic<bool> go{false};
    std::atomic<std::uint32_t> started{0};
    std::vector<std::thread> workers;
    for (std::uint32_t thread = 0; thread < Threads; ++thread) {
        workers.emplace_back([&] {
            while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
            for (std::uint32_t lookup = 0; lookup < Windows * DrawCacheBypassWindow::Window / Threads; ++lookup) {
                if (window.Note(false, 0)) started.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    go.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();
    Expect(started.load() == Windows, std::to_string(Threads) + " threads noting " + std::to_string(Windows) + " all-miss windows of " + std::to_string(DrawCacheBypassWindow::Window) + " lookups started " + std::to_string(started.load()) + " bypasses");
}

}

int main() {
    testMissesBypassThenProbe();
    testHitsNeverBypass();
    testStageHitsKeepTheCache();
    testConcurrentWindowsCloseOnce();
    if (failures != 0) {
        std::fprintf(stderr, "%d draw cache bypass checks failed\n", failures);
        return 1;
    }
    std::printf("draw cache bypass tests passed\n");
    return 0;
}
