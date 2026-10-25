# libSceNgs2.native (15 todo: D 15)

**What it is:** the PS5 build of NGS2, the voice/rack audio mixing engine (systems, racks, voices, render, waveform parsing, panning and geometry). The todo rows are its stream objects, rack/system command batches and queries, the job-scheduler option reset and three module-enumeration calls.

**Existing state:**
- 44 done, implemented in `core/libs/prx/libSceNgs2.native/src/*.cpp`:
  - `System.cpp`: create/destroy, lock, render, report registry;
  - `Rack.cpp`;
  - `Voice.cpp`;
  - `Render.cpp`, `Atrac9.cpp`, `Custom.cpp`, `Reverb.cpp`, `Pan.cpp` and `Geom.cpp`.
- The same sources build `libSceNgs2` through `add_sce_ngs2_library` (`Ngs2Library.cmake:1-25`; `core/libs/prx/libSceNgs2/CMakeLists.txt:1-3`).
- State and locking: `Ngs2Mutex()` and `Ngs2Lock` (`src/Ngs2Internal.hpp:195-202`), the per-system lock (`src/System.cpp:388-409`) and the report registry (`src/System.cpp:465-484`).
- Errors (`include/Ngs2Types.hpp`): `INVALID_OUT_ADDRESS` 0x804A8010 (`:10`), `INVALID_OUT_SIZE` 0x804A8011 (`:11`), `INVALID_SYSTEM_HANDLE` 0x804A8201 (`:15`) and `INVALID_RACK_HANDLE` 0x804A8202 (`:16`).
- Tests:
  - twelve `ngs2_<name>_tests` from `tests/Ngs2*.cpp`, registered by the foreach in `core/libs/prx/libSceNgs2.native/CMakeLists.txt:5-11` (`add_test_executable` `:7`, `add_test` `:11`);
  - `ngs2_core_render_tests` (`core/libs/prx/libSceNgs2/CMakeLists.txt:6`, `:10`).
- The 15 todos are `(void)` declarations in `core/libs/prx/libSceNgs2.native/Export.cpp:9-82` (`include/Api.hpp:7-21`). No debt entry covers them.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 15.

## Families

Sources checked, all already cited by the tree:
- SharpProspero `Ngs2.cs` at 5dfdb4b (`docs/dev/debt/libSceNgs2.md:6`);
- shadPS4 `ngs2.cpp` at 15abf68 (`docs/dev/debt/libSceNgs2.native.md:37`);
- KytyPS5 `ngs2.cpp` at bb98f26 and df5b9b9 (`docs/dev/debt/libSceNgs2.native.md:16`, `:51`);
- prosper `hle_audio.cpp` at d61eb2c (`docs/dev/debt/libSceNgs2.native.md:5`, `:35`).

None of them gives a usable layout for these rows.

- **D (7): streams.** `StreamCreate`, `StreamCreateWithAllocator`, `StreamDestroy`, `StreamQueryBufferSize`, `StreamQueryInfo`, `StreamResetOption` and `StreamRunCommands`.
  - SharpProspero gives the signatures (`Ngs2.cs:449-461`), with `void*` options and commands.
  - shadPS4 stubs them without parameters (`ngs2.cpp:477-510`); KytyPS5 and prosper omit them.
  - The stream option, info and command layouts, and what a stream does, are unknown.
- **D (2): rack/system command batches.** `RackRunCommands` and `SystemRunCommands`.
  - SharpProspero: `(handle, void* params, numParams)` (`Ngs2.cs:378`, `:411`).
  - prosper's module notes (`hle_audio.cpp:4367-4384`, `:4411`) show that the done SystemLock, SystemUnlock, SetSampleRate and SetGrainSamples, and RackLock/Unlock, queue internal commands 0/1/4/5 through these cores.
  - Neither source gives the exported command record, and the 16-byte voice command record (`src/Voice.cpp:415`) is not known to apply.
- **D (2): rack/system queries.** `RackQueryInfo` and `SystemQueryInfo`.
  - SharpProspero: `(handle, queryId, void* outInfo, infoSize)` (`Ngs2.cs:380`, `:413`).
  - No rack or system query id is known. The voice analogue knows only 0x4001, which came from a title (`src/Voice.cpp:505`; debt `:41`).
- **D (1): `JobSchedulerResetOption`.** Only `(void* outOption)` is known (`Ngs2.cs:477`). The option record and its defaults are unknown.
- **D (3): `ModuleArrayEnumItems`, `ModuleEnumConfigs` and `ModuleQueueEnumItems`.** No prototype in any cited source: shadPS4 has parameterless stubs (`ngs2.cpp:437-450`) and SharpProspero omits them.

## Batch proposals

- None: there are no A, B-S, B-M or C-real rows.

## Dependency notes

- None of these needs third-party code. A stream implementation would be a new subsystem (B-L) once the option and command layouts are recovered from the module.

## Caveats

- `core/libs/prx/libSceNgs2.native/Export.cpp` is not compiled. `add_sce_ngs2_library` lists only `src/*.cpp` (`Ngs2Library.cmake:3-13`), and `core/libs/prx/libSceNgs2.native/CMakeLists.txt:1-2` adds nothing else. So the 15 `(void)` stubs are not in the built `libSceNgs2.native.prx`, and an import of one fails to resolve instead of reaching `NotImplemented_nid_no_patch`. A follow-up should add `Export.cpp` to the library's sources, plus the missing "Unknown function info" debt entry.
- Debt `:5` and `:7` show that prosper's module notes give exact handle-check precedence for these cores (for example SystemRunCommands checks the handle first, 0x804A8201). A future row could check the handle and throw for every command, but that implements nothing for a valid input, so these rows stay D.
