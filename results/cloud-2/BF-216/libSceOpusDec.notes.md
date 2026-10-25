# libSceOpusDec (8 todo: D 8)

**What it is:** a standalone Opus decoder over AJM: initialize and terminate a context, size and create a decoder state, and decode packets to PCM. The variants add float output, a priority and a timeout, plus Ctl, Cancel and a non-Ex Create.

**Existing state:**
- 6 done (`core/libs/prx/libSceOpusDec/Export.cpp:15-29`): Initialize, Terminate, GetSize, CreateEx, Decode and Destroy.
- They forward to `OpusDecoderLibrary` (`core/libs/prx/libSceOpusDec/include/OpusDecoder.hpp:19`), shared with libSceOpusCeltDec (`core/libs/prx/libSceOpusCeltDec/Export.cpp:9`).
  - It runs bundled libopus 1.6.1 for mono/stereo s16 at 8, 12, 16, 24 and 48 kHz, with host-side `contexts` and `decoders` maps (`:112-113`) and synchronous `Decode` (`:70`).
  - Argument lists, validation errors and their order were recovered from the module (`docs/dev/debt/libSceOpusDec.md:9`).
- Test: `guest_opus_dec_tests` (`core/libs/tests/GuestOpusDec.cpp`), `core/libs/prx/libSceOpusDec/CMakeLists.txt:7`, `add_test` `:11`.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 8.

## D families

All eight are parameterless declarations from the PS5 3.20 export inventory, with no credible prototype (`docs/dev/debt/libSceOpusDec.md:5`).
- **Float decode (3):** DecodeFloat, and DecodeFloat with priority, and with priority and timeout.
  - With a prototype, plain DecodeFloat is B-S: libopus `opus_decode_float` beside `Decode` (`OpusDecoder.hpp:70`), reusing its checks.
  - The capacity unit (bytes or samples) and the return value cannot be assumed from Decode.
- **Priority and timeout decode (2+2):** what the priority and timeout do on the AJM-backed console decoder, and the timeout result, are unknown. Decodes here are synchronous.
- **Create, Ctl and Cancel (3):**
  - Create differs from CreateEx (`Export.cpp:21`) in an unknown way.
  - Ctl's request numbering is not established.
  - Cancel targets an asynchronous decode this library does not model.

## Batch proposal

None. The first step is the prototypes.
- libSceOpusCeltDec has the same five-name family as todo (Create, Ctl, DecodeFloat, DecodeFloatWithPriority and DecodeWithPriority, `core/libs/prx/libSceOpusCeltDec/Export.cpp:31-54`), and both libraries use one `OpusDecoderLibrary`.
- So recovering a prototype closes the pair, and the combined family would be 13 rows.

## Caveats

None beyond the missing prototypes.
