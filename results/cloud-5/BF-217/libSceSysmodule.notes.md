# libSceSysmodule (6 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 6)

**What it is:** system-module loading by id (public 16-bit ids and internal 32-bit ids), module handles and unwind info, plus the start-up hooks libkernel calls.

**Existing state** (`core/libs/prx/libSceSysmodule/Export.cpp`, built at `core/libs/prx/libSceSysmodule/CMakeLists.txt:1`):
- 10 done.
  - Load and unload only count references in `gLoadCount` (`:34`) and never load code.
  - `sceSysmoduleGetModuleHandleInternal` opens `libs/<table name>.prx` (`:37-40`, `:152-181`) and caches the handle in `gHandles` (`:35`).
  - The id table is `kModuleTable` (`ModuleTable.hpp`).
- Errors: INVALID_VALUE 0x805A1000 and UNLOADED 0x805A1001 (`:24-25`).
- Debt:
  - `:5`: the public calls accept an unknown id, log it and return 0, while the internal calls throw for it;
  - `:9-12`: the module table;
  - `:13`: the six todo rows; only `sceSysmoduleLoadModuleByNameInternal` has a shape, from SharpProspero;
  - `:17`: the Windows unwind cache.
- Tests: `guest_sysmodule` (`CMakeLists.txt:6`, `:21`) and `guest_sysmodule_maps_scan` (`:24`).
- Sources read for this pass:
  - [shadPS4 `sysmodule.cpp`](https://github.com/shadps4-emu/shadPS4/blob/68818c769a2545d1071fb97e83e3a979b244f324/src/core/libraries/sysmodule/sysmodule.cpp) at 68818c76: five of the six are parameterless stubs that return 0, and PreloadModuleForLibkernel runs a PS4 preload list (`#L142-L145`).
  - [SharpProspero `Sysmodule.cs`](https://github.com/SvenGDK/SharpProspero/blob/5dfdb4b0b166e83dc9ae275001afb695f12c7174/src/SharpProspero/Interop/Sysmodule/Sysmodule.cs#L149-L151).

## Families

- **By-name internal pair (2), D:**
  - `sceSysmoduleLoadModuleByNameInternal(uint8_t* fname, uint64_t a1..a5)`: the shape comes from SharpProspero, but the meaning of a1-a5, the name form and the return value (0 or a handle) are unknown.
  - `sceSysmoduleUnloadModuleByNameInternal` has no shape.
- **libkernel start-up hooks (2), D:** `sceSysmoduleMapLibcForLibkernel` and `sceSysmodulePreloadModuleForLibkernel`. Their arguments and effect have no recorded PS5 counterpart in a relinked process. shadPS4's preload list is PS4's.
- **Queries (2), D:** `sceSysmoduleIsCalledFromSysModule` and `sceSysmoduleIsCameraPreloaded`. What they test and how they answer are unknown.

## Batch proposal

None.

## Caveats

- shadPS4 returns 0 from five of the six. A constant success here would be C-silent, which needs a title, and debt `:13` names none.
- Done calls that still throw through another path:
  - the internal load, unload, IsLoaded and handle calls throw for an id missing from the table (`:219-221`, `:251-253`, `:263-265`, `:282-284`, `:301-303`, `:159-162`);
  - `sceSysmoduleGetModuleInfoForUnwind` throws for an unmapped address (`:146-148`).
