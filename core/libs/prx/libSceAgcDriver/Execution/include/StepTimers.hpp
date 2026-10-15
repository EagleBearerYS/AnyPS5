#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_STEPTIMERS_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_STEPTIMERS_HPP

#include "prx/libSceAgcDriver/Execution/include/ProfileOutput.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace AgcDriver {

struct StepSite {
    const char* name;
    std::atomic<std::uint64_t> nanoseconds{0};
    std::atomic<std::uint64_t> calls{0};
    StepSite* next = nullptr;
};

inline std::atomic<StepSite*>& StepSites() {
    static std::atomic<StepSite*> head{nullptr};
    return head;
}

inline bool StepsTimed() {
    static const bool timed = std::getenv("APS5_PROFILE_DRAW") != nullptr;
    return timed;
}

inline StepSite& RegisterStep(StepSite& site) {
    site.next = StepSites().load(std::memory_order_relaxed);
    while (!StepSites().compare_exchange_weak(site.next, &site, std::memory_order_release, std::memory_order_relaxed)) {
    }
    return site;
}

inline void ReportSteps(std::chrono::steady_clock::time_point now) {
    static std::atomic<std::int64_t> lastReport{0};
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    auto last = lastReport.load(std::memory_order_relaxed);
    if (last == 0) {
        lastReport.compare_exchange_strong(last, seconds);
        return;
    }
    if (seconds - last < 10 || !lastReport.compare_exchange_strong(last, seconds)) return;
    std::string report;
    for (auto* site = StepSites().load(std::memory_order_acquire); site != nullptr; site = site->next) {
        const auto count = site->calls.exchange(0, std::memory_order_relaxed);
        const auto total = site->nanoseconds.exchange(0, std::memory_order_relaxed);
        if (count == 0) continue;
        char text[160];
        std::snprintf(text, sizeof(text), " %s %.1f us x%llu;", site->name, static_cast<double>(total) / 1000.0 / static_cast<double>(count), static_cast<unsigned long long>(count));
        report += text;
    }
    ProfilePrint_nid_no_patch("[steps] per call (10 s):%s\n", report.c_str());
}

class StepTimer {
public:
    explicit StepTimer(StepSite& site) : site(site), enabled(StepsTimed()), start(enabled ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}) {}
    ~StepTimer() {
        if (!enabled) return;
        const auto now = std::chrono::steady_clock::now();
        site.nanoseconds.fetch_add(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count()), std::memory_order_relaxed);
        site.calls.fetch_add(1, std::memory_order_relaxed);
        ReportSteps(now);
    }
    StepTimer(const StepTimer&) = delete;
    StepTimer& operator=(const StepTimer&) = delete;

private:
    StepSite& site;
    bool enabled;
    std::chrono::steady_clock::time_point start;
};

#define APS5_STEP_JOIN(a, b) a##b
#define APS5_STEP_NAME(a, b) APS5_STEP_JOIN(a, b)
#define APS5_STEP(text)                                                                                            \
    static ::AgcDriver::StepSite& APS5_STEP_NAME(stepSite, __LINE__) = ::AgcDriver::RegisterStep(*new ::AgcDriver::StepSite{text}); \
    const ::AgcDriver::StepTimer APS5_STEP_NAME(stepTimer, __LINE__)(APS5_STEP_NAME(stepSite, __LINE__))

}

#endif
