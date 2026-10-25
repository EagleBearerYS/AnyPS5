# libSceAudiodec.native (14 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 14)

**What it is:** the native-process build of the audio decoder (ATRAC9, MP3, M4AAC). Its own `Export.cpp` declares 14 extra names: the `Ex`, `Decode2` and `WithPriority` forms, plus library-wide Initialize, Terminate, GetContext and RegisterCodec.

**Existing state:**
- **Unbuilt Export.cpp.** `core/libs/prx/libSceAudiodec.native/CMakeLists.txt:1-2` includes `../libSceAudiodec/AudiodecLibrary.cmake` and calls `add_sce_audiodec_library(libSceAudiodec.native)`. That helper compiles `libSceAudiodec/Export.cpp` and `src/Codecs.cpp` (`AudiodecLibrary.cmake:3-5`), not the native directory's `Export.cpp`. So none of the 14 rows is exported. The `.native` module instead exports the six libSceAudiodec functions, which main-names does not list for it.
- Those six are the twins every row would be compared against (`core/libs/prx/libSceAudiodec/Export.cpp`):
  - InitLibrary `:173`, TermLibrary `:180`, CreateDecoder `:187`, DeleteDecoder `:213`, ClearContext `:221`, Decode `:229`;
  - state: a per-codec init count and 64 `Instance` slots (`:52-62`);
  - codec types 1-3 (`:39-41`), error constants `:15-37`.
- Debt `docs/dev/debt/libSceAudiodec.native.md:5`: the 14 names come from the PS5 3.20 export table, with signatures and behaviour unverified. It says they "throw through `NotImplemented_nid_no_patch`". In fact they are not exported at all, so a title importing them hits an unresolved import, not the throw.
- Tests: none for `.native`. `guest_audiodec` (`core/libs/tests/GuestAudiodec.cpp`, built at `libSceAudiodec/CMakeLists.txt:5`, registered at `:9`) links the non-native `libSceAudiodec`.
- Public source checked: Orbital `db_types.json` at b321fcdbd7 has only the base forms (`#L84-L93`: ClearContext, CreateDecoder, Decode, DeleteDecoder, InitLibrary, TermLibrary) and the Cpu forms. It has none of the 14.

## Families

- **Ex forms (4, D):** ClearContextEx, CreateDecoderEx, DeleteDecoderEx, DecodeEx. The base forms are done, but the Ex suffix means a different argument list (an extra argument or a caller-owned context), and no source says what. They are not exact twins, so not A.
- **Decode2 / WithPriority (6, D):** the second-generation decode record, the priority argument's type and range and its scheduling effect are all unknown.
- **Library lifecycle and registration (4, D):** Initialize, Terminate, GetContext, RegisterCodec.
  - The tree initialises per codec type (`:173`), and nothing says that Initialize/Terminate are aliases of InitLibrary/TermLibrary.
  - RegisterCodec would extend a codec table that is fixed at three types (`:64`).
- Every row also carries "(not exported: Export.cpp is not built)".

## Batch proposal

None: no A/B/C row.

## Caveats

- Fixing the build (compiling the native `Export.cpp` into the `.native` target) would only turn unresolved imports into throws. It needs care, because the target already exports the libSceAudiodec names from the shared helper.
- PR 8747 (closed) declared all 14. No open claim.
