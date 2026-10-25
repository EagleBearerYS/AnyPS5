# libSceDipsw (9 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 9)

**What it is:** the kernel's development-switch ("dip switch") interface: reading, setting and clearing console debug switches, plus mode queries (development mode, assist mode, Razor profiler disabled).

**Existing state:**
- Nothing is done. `core/libs/prx/libSceDipsw/Export.cpp:9-52` holds nine parameterless declarations, each of which throws.
- There is no `CMakeLists.txt`, so `Export.cpp` is built by `core/libs/CMakeLists.txt:159` and all nine names are exported.
- No state and no error constants.
- Debt: one entry (`docs/dev/debt/libSceDipsw.md:5`). It lists the names and NIDs from the PS5 3.20 inventory, says no credible prototypes were found, and says "callers must not call them until signatures are verified".
- Tests: none registered.
- Neighbours: no library implements a dip-switch or mode query.
  - The kit-type queries in libkernel_sys, `sceKernelIsDevKit` and `sceKernelIsTestKit` (`core/libs/prx/libkernel_sys/Export.Discovery.cpp:1430`, `:1510`), also throw.
  - Orbital's type database (`https://github.com/AlexAltea/orbital/blob/b321fcdbd700cc02da1cb1ad25412781d5ec73c6/tools/ida/db_types.json`) has none of these names. Its only `sceKernelIs*Mode` entry is `sceKernelIsNeoMode`.

## Families

- **D (9).** Every row is D for the same two reasons: there is no prototype, and the answer is console state that nobody has measured.
  - Mode queries (IsDevelopmentMode, IsAssistMode, IsDisableRazor, CheckDipsw): answering 0 "because this is a retail unit" would be a guess. Nothing in the tree measures it or gives it.
  - Switch data (InitializeDipsw, Read/WriteDipswData, Set/UnsetDipsw): privileged calls whose switch numbering, buffer layout and result for an app process are not public.

## Batch proposal

None.
- PR 10511 (open) is recorded as implementing InitializeDipsw, IsAssistMode and IsDevelopmentMode. PR 8747 (closed) declared all nine.
- Under this census those rows stay D until a prototype is cited and the answer is measured.

## Caveats

- A title that only imports these names loads today. Any call throws, which is the intended behaviour while the ABI is unknown.
