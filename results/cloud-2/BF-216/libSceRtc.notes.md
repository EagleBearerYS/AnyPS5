# libSceRtc (1 todo: A 0, B-S 0, B-M 0, B-L 0, C 0, D 1)

Upstream main 2742004ed2.

- **What it is:** the real-time clock library: current ticks of the system, network, ad-network, debug-network and raw network clocks, tick arithmetic, calendar conversions, DOS/Win32/time_t conversions, RFC 2822/3339 formatting and parsing.
- **Existing state:** 60 done.
  - The four `sceRtcSetCurrent*Tick` setters (`core/libs/prx/libSceRtc/Export.cpp:771-785`) go through `setCurrentTick` (`:146`), which keeps a process-local clock per console clock id and never changes the host clock (debt `docs/dev/debt/libSceRtc.md:16`, `:22`).
  - Many behaviours come from the disassembly of libSceRtc.sprx on firmware 13.60 or PS4 measurements (debt `:5-18`).
  - Test: `guest_rtc_tests` (`core/libs/tests/GuestRtc.cpp`), built at `core/libs/prx/libSceRtc/CMakeLists.txt:7` and registered at `:11` and `:12`.
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 1.
- **D (1):** `sceRtcSetConf(int, int, int, int)` (`Export.cpp:787`). The debt (`:16`) records that the console passes the third and fourth arguments to the same clock-set call with clock 0 and that "what the pair selects is unknown". The first two arguments are unexplained, and the host has no clock configuration for it to change.
- **Batch proposal:** none.
- **Caveats:** it is not a forward to `sceRtcSetCurrentTick`: that takes a tick pointer, while SetConf passes two integers whose meaning is unknown.
