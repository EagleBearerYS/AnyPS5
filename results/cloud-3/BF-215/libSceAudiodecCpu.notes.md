# libSceAudiodecCpu (8 todo: D 8)

**What it is:** the CPU audio-decoder front end: query memory size, init decoder, decode, clear context, and their `Internal` variants. Codec-specific modules (`libSceAudiodecCpuDdp`, `M4aac`, `Lpcm` and others) sit beside it.

**Existing state:**
- 0 done. All eight exports are parameterless pending declarations that throw (`core/libs/prx/libSceAudiodecCpu/Export.cpp:9-47`).
- The names come from the PS5 3.20 export table and were cross-checked against firmware 13.60. Their signatures and behaviour are unverified (`docs/dev/debt/libSceAudiodecCpu.md:5`).
- There is no CMakeLists or test. The library is built by the generic loop in `core/libs/CMakeLists.txt:159`.
- Related state: `libSceAudiodec` decodes through handles and `AudiodecCtrl` (`core/libs/prx/libSceAudiodec/Export.cpp:173-229`, codecs in `src/Codecs.cpp`). That is a different interface, with no shared NID and no documented equivalence to a CPU work-area API.
- The sibling `libSceAudiodecCpu*` codec directories declare no exports.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 8.

## Families

- **D (8): unknown signature.** These are parameterless pending-export declarations whose debt says signatures and behaviour are unverified: ClearContext, Decode, InitDecoder, QueryMemSize and the four `Internal*` variants.

## Batch proposals

- None: there are no A, B-S, B-M or C-real rows.

## Dependency notes

- Once prototypes are known, the decode path could reuse libSceAudiodec's codecs (`src/Codecs.cpp`) instead of adding a decoder. The codec-type ids and work-area layout would still need measuring.
