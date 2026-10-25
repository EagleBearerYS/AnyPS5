# libScePlayGo (2 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 2)

**What it is:** PlayGo, the play-while-installing chunk service: chunk ids, loci, progress, the to-do list, install speed, language masks and optional chunks.

**Existing state** (`core/libs/prx/libScePlayGo/Export.cpp`, built by `CMakeLists.txt:1`):
- 19 done.
- The game is fully installed on the host, so every chunk is local and nothing is pending (`:16`).
- One handle (`:19`).
- Error constants: BAD_HANDLE, BAD_POINTER, BAD_SIZE, BAD_CHUNK_ID and BAD_LOCUS (`:17-18`, `:31-33`).
- Chunk table from `playgo-chunkdefs.xml` or `playgo-chunk.dat` (debt `:7`).
- Debt `docs/dev/debt/libScePlayGo.md:5`: RequestNextChunk and Snapshot have no credible prototypes.
- Test: `guest_playgo` (`core/libs/tests/GuestPlayGo.cpp`), built at `CMakeLists.txt:6` and registered at `:10`.

## Families

- **D (2): RequestNextChunk and Snapshot.**
  - These look like the debug-PlayGo controls. shadPS4 has `sceDbgPlayGoRequestNextChunk` and `sceDbgPlayGoSnapshot` as parameterless stubs that return OK, registered in libSceDbgPlayGo (`https://github.com/shadps4-emu/shadPS4/blob/8d50471b7dc141dd700b51276b8edd1f293bce26/src/core/libraries/playgo/playgo.cpp#L18-L26`, `#L391-L392`). That is not a console answer, and those are different names in a different module.
  - Orbital's type database lists neither.
  - What RequestNextChunk does when nothing is pending, and what Snapshot captures, are unknown.

## Batch proposal

None. PR 9963 (closed) claimed RequestNextChunk.

## Caveats

- If a title calls either, the answer for a fully installed package must be measured. A constant 0 would be C-silent at best, and would need that title.
