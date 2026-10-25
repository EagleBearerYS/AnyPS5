# libSceDseehx (4 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 4)

**What it is:** a system audio conversion module with init, extended init, convert and flush entry points (sysmodule `0x80000056`, `core/libs/prx/libSceSysmodule/ModuleTable.hpp:251`). The name matches Sony's DSEE HX audio upscaling, but no tree file or cited source describes what it converts.

**Existing state:**
- Built: no `CMakeLists.txt` of its own, so `core/libs/CMakeLists.txt:159` compiles `core/libs/prx/libSceDseehx/Export.cpp`.
- 0 done. Four parameterless declarations (`Export.cpp:9`, `:14`, `:19`, `:24`; `include/Api.hpp:7-10`). There is no state, no error constant and no test.
- Debt (`docs/dev/debt/libSceDseehx.md:5`): the names and NIDs are in the PS5 3.20 inventory, but "no credible prototypes were found".

## Families

- **D (4): Convert, ExtInit, Flush, Init.**
  - No prototype.
  - No neighbour: no other library on main has a Dseehx name (`in/main-names.json`).
  - No public description of the conversion.

## Batch proposal

None.

## Caveats

- Even with prototypes, the conversion would be B-L at best, and only if its algorithm were public. Nothing cited in the tree describes it.
