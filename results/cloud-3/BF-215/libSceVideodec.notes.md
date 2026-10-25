# libSceVideodec (1 todo: D 1)

**What it is:** the PS4-style video decoder API (create, decode, flush, reset, delete, query resources). Here it is a front end over libSceVideodec2's FFmpeg H.264 decoder.

**Existing state:**
- 6 done, in `core/libs/prx/libSceVideodec/Export.cpp`.
- Decoders are kept under `decodersMutex` (`:51`), and `VideodecErrorApiFail` 0x80C10000 is defined at `:17`.
- Structures and errors follow shadPS4's PS4 library and are not measured on a PS5 (`docs/dev/debt/libSceVideodec.md:5`).
- Tests: `guest_videodec_tests` (`core/libs/tests/GuestVideodec.cpp`), registered at `libSceVideodec/CMakeLists.txt:11` and `:15`.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 1.

## Families

- **D (1):** `sceVideodecMapMemory` (`:281`) is a parameterless declaration whose signature and behaviour are unverified (debt `:15`). The similarly named exports in the tree also throw, so there is nothing to forward to:
  - `sceVideodec2MapMemory` (`core/libs/prx/libSceVideodec2/Unimplemented.cpp:19`);
  - `sceVdecwrapMapMemory` (`core/libs/prx/libSceVdecwrap/Export.cpp:34`);
  - `sceVdecCoreMapMemory` (`core/libs/prx/libSceVdecCore.native/Export.cpp:19`).

## Batch proposals

- None: there are no A, B-S, B-M or C-real rows. When a prototype is recovered for any of the MapMemory family, the others may become A rows against it.
