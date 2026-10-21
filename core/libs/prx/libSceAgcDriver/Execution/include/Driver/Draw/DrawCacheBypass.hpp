#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_DRIVER_DRAW_DRAWCACHEBYPASS_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_DRIVER_DRAW_DRAWCACHEBYPASS_HPP

#include <atomic>
#include <cstdint>

namespace AgcDriver::DriverDetail {

class DrawCacheBypassWindow {
public:
    static constexpr std::uint32_t Window = 2048, MinimumHits = Window / 20, IdleDraws = 16384;

    bool Bypass();
    bool Note(bool hit, std::uint32_t stages);

private:
    std::atomic<std::uint32_t> lookups{0}, hits{0}, stageHits{0}, idle{0};
};

}

#endif
