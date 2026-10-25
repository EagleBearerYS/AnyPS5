# libSceVdecCore (20 todo: D 20)

**What it is:** a video decoder core interface:
- decoder create, delete and reset, and instance size;
- compute resources;
- memory blocks and frame-buffer info;
- stream analysis;
- decode input and output, including software output (`...OutputSw`);
- blocking and try-sync calls, including write-pointer forms (`...Wptr`).

**Existing state:**
- 0 done. `core/libs/prx/libSceVdecCore/Export.cpp:9-107` holds 20 parameterless declarations.
- No state, no CMakeLists.txt and no guest test.
- Debt `docs/dev/debt/libSceVdecCore.md:5`: no credible prototypes. Orbital `db_types.json@b321fcdb` has no entry for them.
- `libSceVdecCore.native` lists the same 20 NIDs and leaves them to these declarations (`docs/dev/debt/libSceVdecCore.native.md:8`).
- Done decoders elsewhere:
  - `libSceVideodec2` decodes through FFmpeg (`core/libs/prx/libSceVideodec2/Export.cpp:513`);
  - `libSceVdecsw` decodes through `libSceVideodec2` (`core/libs/prx/libSceVdecsw/Export.cpp:238`).

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 20.

## Families

- **D (20): unknown signature.**
  - Six rows have namesakes in libSceVdecsw:
    - `CreateDecoder` (`:238`), `DeleteDecoder` (`:249`), `ResetDecoder` (`:392`) and `SetDecodeInput` (`:257`);
    - `SetDecodeOutputSw` with `SetDecodeOutput` (`:293`), and the two `...OutputSw` sync calls with `SyncDecodeOutput` (`:339`) and `TrySyncDecodeOutput` (`:301`).
  - These parallels are by name only. No NID or documented behaviour is shared, so none is an A row.
  - Compute resources, memory blocks, stream analysis and the `Wptr` sync calls have no counterpart in the tree.

## Batch proposals

None.

## Caveats

- With prototypes and structures, the decode calls could forward to the libSceVdecsw/Videodec2 FFmpeg path, sized B-M to B-L.
- The compute-resource and memory-block calls look tied to the hardware decoder, and would likely stay D.
