#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_SPLITCLOCKS_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_SPLITCLOCKS_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace AgcDriver::GuestMemory {

bool SplitClocks();
std::size_t SplitClock(const char* name);
void AddSplitClock(std::size_t slot, std::uint64_t ns);
std::vector<std::pair<std::string, std::uint64_t>> SplitClockTotals();

struct SplitLap {
    bool on = SplitClocks();
    std::chrono::steady_clock::time_point at = on ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    void Mark(std::size_t slot) {
        if (!on) return;
        const auto now = std::chrono::steady_clock::now();
        AddSplitClock(slot, static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now - at).count()));
        at = now;
    }
};

}

#endif
