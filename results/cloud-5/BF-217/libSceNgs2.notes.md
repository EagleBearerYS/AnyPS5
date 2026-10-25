# libSceNgs2 (6 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 6)

**What it is:** the NGS2 audio synthesis and mixing engine (systems, racks, voices, waveform parsing, rendering). The six todo rows are its FFT helpers, the user-read-handler waveform parser and two handle enumerations.

**Existing state:**
- The library has no `Export.cpp`:
  - `core/libs/prx/libSceNgs2/CMakeLists.txt:1-2` builds it from libSceNgs2.native's sources (`core/libs/prx/libSceNgs2.native/Ngs2Library.cmake:3-13`);
  - `:3` adds `PendingExports.cpp`, which holds the six todo rows (`PendingExports.cpp:9-32`).
- The census counts the shared done functions (44) under libSceNgs2.native, so libSceNgs2 shows 0 done. They are built into this module too.
- State the rows could use:
  - the live systems (`libSceNgs2.native/src/System.cpp:21`);
  - each system's racks (`src/Ngs2Internal.hpp:190`);
  - the RIFF chunk reader behind ParseWaveformData and ParseWaveformFile (`src/Atrac9.cpp:274`, `:409`, `:420`).
- Debt:
  - `docs/dev/debt/libSceNgs2.md:5`: no credible FFT prototypes;
  - `:6`: ParseWaveformUser and the two enumerations take SharpProspero's signatures and only throw.
- Test: `ngs2_core_render` (source `libSceNgs2.native/tests/Ngs2Render.cpp`), built at `libSceNgs2/CMakeLists.txt:6` and registered at `:10`.

## Families

- **D (6):**
  - **FftInit, FftProcess and FftQuerySize (3):** no prototype in the tree, in SharpProspero or in shadPS4. shadPS4 has only parameterless OK stubs (`https://github.com/shadps4-emu/shadPS4/blob/15abf68c9b52c4b04d094f2837719fdb871021f3/src/core/libraries/ngs2/ngs2.cpp#L417-L430`).
  - **ParseWaveformUser (1):**
    - The prototype (SharpProspero `Ngs2.cs#L470`) and the handler type `(userData, u32 offset, void* data, size_t size) -> s32` (shadPS4 `ngs2.h#L19-L20`) are known.
    - The handler's return convention and how the parser finds the end of the stream are not.
    - The tree's reader takes a total size (`Atrac9.cpp:415`, `:439`), so a user-handler form would be a guess.
  - **SystemEnumHandles and SystemEnumRackHandles (2):**
    - The prototypes (SharpProspero `Ngs2.cs#L366`, `#L396`) and the state are there.
    - The return value is not known: count written, total live or 0. Neither is the answer when there are more handles than `maxHandles`, or when the buffer is null.
    - shadPS4 returns OK without writing anything (`ngs2.cpp#L196-L209`).

## Batch proposal

None. No PR claims these rows.

## Caveats

- **Not exported on Linux (measured), and by the same rule not on macOS.**
  - Ngs2Library.cmake sets `CXX_VISIBILITY_PRESET hidden` on the target (`Ngs2Library.cmake:21`). The native sources wrap their exports in `#pragma GCC visibility push(default)` (for example `src/System.cpp:262`, `src/Atrac9.cpp:405`, `src/Voice.cpp:398`, `src/Rack.cpp:178`).
  - `PendingExports.cpp` has no such pragma. Its include chain (`include/Api.hpp`, `SceTypes.hpp`, `prx/libc/include/General.hpp`) sets no visibility, and `APS5_VABI` is empty off Windows (`core/libs/prx/libc/include/general/VabiMacros.hpp:4-8`).
  - So on ELF and Mach-O hosts these six symbols are hidden: a title importing them gets an unresolved import instead of the documented throw.
  - Measured for this census on a Linux Release build of main e5f23554c9 plus BF-211's four commits (none touches libSceNgs2; the pinned 2742004ed2 changes nothing in libSceNgs2): `readelf -s` on `PendingExports.cpp.o` marks all six `GLOBAL HIDDEN`, and `nm -D --defined-only` on the unpatched `libSceNgs2.prx` lists none of them.
  - This is from reading, not a build. Check it with `nm -D` on `libSceNgs2.prx`.
  - The fix is to wrap `PendingExports.cpp` in the same pragma.
  - The class of each row stays D either way.
- This is a different case from the known unbuilt `libSceNgs2.native/Export.cpp`: here the file is built, but its symbols are hidden.
