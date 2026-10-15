#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_GHOSTTRACE_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_GHOSTTRACE_HPP

#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace AgcDriver::Graphics {

inline const std::vector<std::pair<std::uint64_t, std::uint64_t>>& GhostRanges() {
    static const auto ranges = [] {
        std::vector<std::pair<std::uint64_t, std::uint64_t>> parsed;
        const char* text = std::getenv("APS5_TRACE_GHOST");
        if (text == nullptr) return parsed;
        const std::string value(text);
        std::size_t at = 0;
        while (at < value.size()) {
            const auto comma = value.find(',', at);
            const auto item = value.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
            at = comma == std::string::npos ? value.size() : comma + 1;
            const auto split = item.find_first_of("+-");
            if (split == std::string::npos) continue;
            const auto begin = std::strtoull(item.substr(0, split).c_str(), nullptr, 0);
            const auto second = std::strtoull(item.substr(split + 1).c_str(), nullptr, 0);
            const auto end = item[split] == '+' ? begin + second : second;
            if (end > begin) parsed.emplace_back(begin, end);
        }
        if (parsed.empty()) parsed.emplace_back(0, std::numeric_limits<std::uint64_t>::max());
        return parsed;
    }();
    return ranges;
}

inline bool GhostTraceEnabled() {
    static const bool enabled = std::getenv("APS5_TRACE_GHOST") != nullptr;
    return enabled;
}

inline bool GhostWindowOpen() {
    static const auto window = [] {
        std::pair<std::uint64_t, std::uint64_t> parsed{0, std::numeric_limits<std::uint64_t>::max()};
        const char* text = std::getenv("APS5_TRACE_GHOST_PRESENTS");
        if (text == nullptr) return parsed;
        const std::string value(text);
        const auto colon = value.find(':');
        parsed.first = std::strtoull(value.substr(0, colon).c_str(), nullptr, 0);
        if (colon != std::string::npos) parsed.second = std::strtoull(value.substr(colon + 1).c_str(), nullptr, 0);
        return parsed;
    }();
    const auto present = Recorder::Presents();
    return present >= window.first && present <= window.second;
}

inline bool GhostTraced(std::uint64_t address, std::uint64_t bytes) {
    if (!GhostTraceEnabled() || !GhostWindowOpen()) return false;
    const auto end = bytes > std::numeric_limits<std::uint64_t>::max() - address ? std::numeric_limits<std::uint64_t>::max() : address + std::max<std::uint64_t>(bytes, 1);
    for (const auto& [begin, last] : GhostRanges()) {
        if (address < last && begin < end) return true;
    }
    return false;
}

inline std::vector<std::pair<std::uint64_t, std::uint64_t>> GhostIntersections(std::uint64_t begin, std::uint64_t end) {
    std::vector<std::pair<std::uint64_t, std::uint64_t>> inside;
    if (!GhostTraceEnabled() || !GhostWindowOpen()) return inside;
    for (const auto& [first, last] : GhostRanges()) {
        const auto from = std::max(first, begin);
        const auto to = std::min(last, end);
        if (from < to) inside.emplace_back(from, to);
    }
    return inside;
}

inline bool GhostBudget(std::uint64_t present, std::uint32_t perPresent) {
    static std::mutex mutex;
    static std::uint64_t lastPresent = ~0ull;
    static std::uint32_t used = 0;
    std::lock_guard lock(mutex);
    if (present != lastPresent) {
        lastPresent = present;
        used = 0;
    }
    return used++ < perPresent;
}

inline void GhostTrace(const char* format, ...) {
    static const auto started = std::chrono::steady_clock::now();
    static std::atomic<std::uint64_t> sequence{0};
    static std::mutex mutex;
    char text[1024];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    const auto packet = GuestMemory::CurrentPacket();
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    std::lock_guard lock(mutex);
    std::fprintf(stderr, "[ghost] #%llu present %llu t %.1f ms packet 0x%x queue 0x%x: %s\n", static_cast<unsigned long long>(sequence.fetch_add(1, std::memory_order_relaxed)), static_cast<unsigned long long>(Recorder::Presents()), ms, packet.opcode, packet.queue, text);
}

}

#endif
