# libSceVenc (14 todo: D 14)

**What it is:** the video encoder API: create and delete an encoder, query memory, presets and stream headers, set the configuration, picture, rate-control and reference-invalidation parameters, submit decoded frames, and start and stop encoding.

**Existing state:**
- No function is done. `core/libs/prx/libSceVenc/Export.cpp:9-77` holds 14 parameterless throwing declarations.
- It has no CMakeLists.txt; the generic rule builds it (`core/libs/CMakeLists.txt:159`). There is no test.
- Nothing in the tree encodes video. The only FFmpeg use besides decoding is libSceAvPlayer's demuxer (`core/libs/prx/libSceAvPlayer/src/Source.cpp:260`).

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 14.

## D families

All 14 rows share one reason. The names and NIDs come from the PS5 3.20 export inventory, with no credible prototype (`docs/dev/debt/libSceVenc.md:5`).
- Encoder lifecycle (4): Create, Delete, StartEncode, StopEncode.
- Configuration (6): Get/SetConfig, QueryPreset, SetPictureConfig, SetRateControlConfig, SetReferenceFrameInvalidationConfig.
- Memory (2): QueryMemorySize, MapMemory.
- Input and output (2): SetDecodedFrame, QueryHeader.

Even with prototypes, an implementation would be B-L: it needs an H.264/HEVC encoder as a new dependency, plus the console's record layouts.

## Batch proposal

None. Every row needs its prototype first.

## Caveats

None beyond the missing prototypes.
