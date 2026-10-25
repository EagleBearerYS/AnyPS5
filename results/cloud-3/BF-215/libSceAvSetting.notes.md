# libSceAvSetting (24 todo: D 24)

**What it is:** the system AV-output control library (`sceAvControl*`):
- monitor-info, EDID and HDCP KSV queries;
- video-out mode checks and setters;
- diagnostic dither, layout and fake-monitor modes;
- callback registration for the launcher and the resource arbitrator.

**Existing state:**
- 0 done. Every export is a parameterless declaration that throws (`core/libs/prx/libSceAvSetting/Export.cpp:11-129`, one every 5 lines).
- There is no `CMakeLists.txt` in the library: it is built by the generic loop in `core/libs/CMakeLists.txt:159`. There are no tests, state objects or error constants.
- Debt `docs/dev/debt/libSceAvSetting.md:5-28` lists each of the 24 as "unknown signature".
- No other library in the tree calls `sceAvControl*`. The video-out library's own mode and monitor answers are not tied to these exports by any declaration.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 24.

## Families

- **D (24), unknown signature:** every row is recorded as unknown signature (debt `:5-28`).
  - The getters fill records whose layouts are unknown: monitor info, raw EDID, KSV list, output mode.
  - The setters and checks take mode records that are unknown too.
  - The callback calls take a callback record and a dispatch model that are unknown.
  - None can be C-real: every answer needs either an argument or an output.

## Batch proposals

None. No row is A, B or C-real.

## Dependency notes

- An implementation needs prototypes from a title's call sites or a module disassembly. The monitor-info answers would then have to agree with what libSceVideoOut reports for the host display.
