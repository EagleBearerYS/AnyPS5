# libSceRazorCpu (14 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 14)

**What it is:** the Razor CPU profiler client: markers, plots, bookmarks, syncs, job-manager events, data tags, logical file accesses and capture state, plus internal capture, fiber, GPU-marker and workload hooks (sysmodule `0x80000075`, `core/libs/prx/libSceSysmodule/ModuleTable.hpp:282`).

**Existing state** (`core/libs/prx/libSceRazorCpu/Export.cpp`):
- Built: `libSceRazorCpu/CMakeLists.txt:1` compiles `Export.cpp`.
- 21 done, all on the uninitialized-profiler path, which the debt calls confirmed (`docs/dev/debt/libSceRazorCpu.md:5`, under Silent stubs):
  - markers, syncs, plots, bookmarks, tags and file accesses return 0 without reading their arguments (`:13-142`);
  - `sceRazorCpuIsCapturing` returns 0 (`:9-11`);
  - `sceRazorCpuFlushOccurred` returns false and writes nothing (`:63-66`).
- No state and no error constant.
- Debt:
  - `:9` and `:10`: the 13 parameterless rows; "No credible prototypes were found";
  - `:11`: argument recovery for the done JobManager calls;
  - `:12`: GetDataTagStorageSize throws because the entry size is unknown.
- Test: `guest_razor_cpu_tests` (`core/libs/tests/GuestRazorCpu.cpp`), built at `CMakeLists.txt:6` and registered at `:10` (`add_test(NAME guest_razor_cpu ...)`). It asserts every done call and that GetDataTagStorageSize throws (`GuestRazorCpu.cpp:74`).

## Families

- **D (13): internal hooks with no prototype.**
  - FiberLogNameChange (`:144`), FiberSwitch (`:149`), GpuMarkerSync (`:154`), InitializeGpuMarkerContext (`:159`), InitializeInternal (`:164`) and SetPopMarkerCallback (`:169`): debt `:9`.
  - SetPushMarkerCallback (`:174`), SetPushMarkerStaticCallback (`:179`), StartCaptureInternal (`:184`), StopCaptureInternal (`:189`), WorkloadRunBegin (`:194`), WorkloadRunEnd (`:199`) and WorkloadSubmit (`:204`): debt `:10`.
  - SharpProspero's `RazorCpu.cs` (https://github.com/SvenGDK/SharpProspero/blob/5dfdb4b0b166e83dc9ae275001afb695f12c7174/src/SharpProspero/Interop/Debug/RazorCpu.cs) covers only the done public calls.
  - The confirmed "returns 0" answer covers the done markers. Extending it to these is a guess:
    - InitializeGpuMarkerContext probably fills a context;
    - the Set*Callback calls take a function;
    - the capture controls change IsCapturing.
- **D (1): `sceRazorCpuGetDataTagStorageSize`** (`:97`, `size_t(uint32_t tagCount)`, SharpProspero `RazorCpu.cs#L142-L144`).
  - The prototype is known, but the bytes per tag entry are not (debt `:12`).
  - The guest allocates whatever this returns, even though the done `sceRazorCpuInitDataTags` ignores the storage (`:87-91`). Any constant would be invented.

## Batch proposal

None.

## Caveats

- The debt files the whole module under "Silent stubs" and also calls the uninitialized path "confirmed". If that path was measured for the internal hooks too, the measurement is not recorded, and these rows could not be promoted from it.
