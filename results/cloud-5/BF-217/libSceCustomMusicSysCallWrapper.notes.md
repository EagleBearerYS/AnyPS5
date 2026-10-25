# libSceCustomMusicSysCallWrapper (1 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 1)

**What it is:** the system-call wrapper of the custom game music (user BGM) feature. It has one export, `sceCustomMusicSelect`.

**Existing state:**
- No CMakeLists. `Export.cpp` is built by `core/libs/CMakeLists.txt:159`.
- Nothing is done. The only row is a parameterless throw (`core/libs/prx/libSceCustomMusicSysCallWrapper/Export.cpp:9`).
- Debt `docs/dev/debt/libSceCustomMusicSysCallWrapper.md:5`: the name comes from the PS5 3.20 inventory, with no credible prototype. The link text is malformed (`[ and sceCustomMusicSelect]`), so a leading name is missing.
- No tests.
- Neighbours: every libSceCustomMusicCore (18) and libSceCustomMusicAudioOut (5) name is todo in main-names.
- Orbital `db_types.json` at b321fcdbd7 has no `sceCustomMusic` entry.

## Families

- **sceCustomMusicSelect (D):** a system-side selection call with no prototype and no in-tree or public answer.

## Batch proposal

None.

## Caveats

- PR 8747 (closed) declared it.
- The debt line's broken link text should be fixed the next time the file is touched.
