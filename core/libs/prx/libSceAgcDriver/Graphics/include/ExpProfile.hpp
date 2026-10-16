#ifndef AGC_GRAPHICS_EXP_PROFILE_HPP
#define AGC_GRAPHICS_EXP_PROFILE_HPP

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <mutex>

namespace AgcDriver::Graphics::ExpProfile {

enum Slot : std::size_t {
    Builds,
    BuildBindings,
    BuildPrecollect,
    BuildUpload,
    BuildDescriptorsA,
    BuildStageA,
    BuildImages,
    BuildBda,
    BuildDescriptorsB,
    BuildStageB,
    TableReuseCheck,
    TablesReused,
    TablesResolved,
    TableElements,
    PrepareBindings,
    PrepareBindingsCalls,
    BindSets,
    Revalidate,
    IndexAccepted,
    IndexPartial,
    IndexRebound,
    IndexBumpedElements,
    IndexComparedElements,
    IndexFallback,
    IndexSubscribe,
    IndexCheck,
    IndexSweep,
    IndexScan,
    IndexTableCompare,
    RevalidateHead,
    RevalidateTail,
    Draws,
    DrawPhaseBase,
    Count = DrawPhaseBase + 13
};

inline bool Enabled() {
    static const bool enabled = std::getenv("APS5_PROFILE_DRAW") != nullptr;
    return enabled;
}

struct Totals {
    std::mutex mutex;
    std::array<double, Count> values{};
};

inline Totals& State() {
    static Totals totals;
    return totals;
}

inline void Add(std::size_t slot, double value) {
    if (!Enabled()) return;
    auto& totals = State();
    std::lock_guard lock(totals.mutex);
    totals.values[slot] += value;
}

inline std::array<double, Count> Take() {
    auto& totals = State();
    std::lock_guard lock(totals.mutex);
    const auto values = totals.values;
    totals.values = {};
    return values;
}

class Timer {
public:
    explicit Timer(std::size_t slot) : slot(slot), start(Enabled() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}) {}
    ~Timer() {
        if (Enabled()) Add(slot, std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
    }
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

private:
    std::size_t slot;
    std::chrono::steady_clock::time_point start;
};

}

#endif
