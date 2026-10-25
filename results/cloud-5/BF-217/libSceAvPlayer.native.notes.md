# libSceAvPlayer.native (2 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 2)

**What it is:** the native-process build of the media player. Its own `Export.cpp` adds two debug-print names, `sceAvPlayerPrintf` and `sceAvPlayerVprintf`.

**Existing state:**
- **Unbuilt Export.cpp.** `core/libs/prx/libSceAvPlayer.native/CMakeLists.txt:1-2` calls `add_sce_avplayer_library(libSceAvPlayer.native)`. That helper compiles `libSceAvPlayer/Export.cpp`, `src/Player.cpp`, `src/Source.cpp` and `libkernel/Time/TimedWait.cpp` (`libSceAvPlayer/AvPlayerLibrary.cmake:3-8`), not the native `Export.cpp`. Neither row is exported. The `.native` module exports the whole libSceAvPlayer API instead.
- The player keeps no log text. `sceAvPlayerSetLogCallback` stores a callback that is never called (`core/libs/prx/libSceAvPlayer/Export.cpp:158`, debt `docs/dev/debt/libSceAvPlayer.md:5`).
- Debt `docs/dev/debt/libSceAvPlayer.native.md:5`: the names come from the PS5 3.20 export table and are unverified. It says they throw, but they are not exported.
- Tests: none for `.native`. `guest_avplayer*` (`libSceAvPlayer/CMakeLists.txt:8`, `:13-15`) link the non-native library.
- Public source checked: Orbital `db_types.json` at b321fcdbd7 `#L119` gives `int32_t sceAvPlayerVprintf(const char* format, va_list args)`. It has no `sceAvPlayerPrintf`.

## Families

- **Debug print (2, D):**
  - Vprintf has a public prototype; Printf has none (presumably variadic, which is unconfirmed).
  - For both, it is unknown whether the text goes to the TTY or to the SetLogCallback callback, and what they return. Not A to libc `vprintf`: same shape, but the destination and return value are unconfirmed.
  - Both are "(not exported: Export.cpp is not built)".

## Batch proposal

None.

## Caveats

- If a title ever imports them, the nearest lead is Orbital's Vprintf prototype plus the existing callback slot. A source would still have to say whether the callback receives the formatted text.
- PR 8747 (closed) declared both.
