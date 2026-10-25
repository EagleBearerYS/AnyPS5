# libSceVdecwrap (11 todo: D 11)

**What it is:** a video decoder wrapper:
- decoder create, delete, reset and flush;
- memory and direct-memory mapping;
- decoder memory and frame-buffer queries;
- decode input, output and sync.

**Existing state:**
- 0 done. `core/libs/prx/libSceVdecwrap/Export.cpp:9-62` holds 11 parameterless declarations.
- No state, no CMakeLists.txt and no guest test.
- Debt `docs/dev/debt/libSceVdecwrap.md:5`: no credible prototypes. Orbital `db_types.json@b321fcdb` has no entry for them.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 11.

## Families

- **D (11): unknown signature.**
  - Six rows have namesakes:
    - in libSceVideodec2: `CreateDecoder` (`core/libs/prx/libSceVideodec2/Export.cpp:513`), `DeleteDecoder` (`:526`), `QueryDecoderMemoryInfo` (`:499`), `Flush` (`:602`) and `Reset` (`:616`);
    - in libSceVdecsw: `SetDecodeInput` (`core/libs/prx/libSceVdecsw/Export.cpp:257`).
  - These parallels are by name only. No NID or documented behaviour is shared, so none is an A row.
  - The `MapMemory` and `MapDirectMemory` namesakes are themselves unimplemented (`core/libs/prx/libSceVideodec2/Unimplemented.cpp:19`, `:23`).
  - `GetDecodeOutput`, `QueryFrameBufferInfo` and `SyncDecode` have no counterpart in the tree.

## Batch proposals

None.

## Caveats

With prototypes and structures (from a caller or a binding), most rows could forward to the existing Videodec2 FFmpeg path, as A or B-M. Without them, any mapping would be a guess.
