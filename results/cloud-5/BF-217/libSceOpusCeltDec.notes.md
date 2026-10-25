# libSceOpusCeltDec (5 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 5)

**What it is:** Sony's CELT-only Opus decoder wrapper over AJM codec 16: context init and term, decoder size and creation, and integer, float and priority decodes, plus a ctl.

**Existing state** (`core/libs/prx/libSceOpusCeltDec/Export.cpp`, built by `CMakeLists.txt:1`):
- 6 done:
  - Initialize, Terminate, GetSize, CreateEx, Decode and Destroy (`:15-29`);
  - all forward to the shared `OpusDecoderLibrary` (`core/libs/prx/libSceOpusDec/include/OpusDecoder.hpp`), instantiated at `:9`, over the bundled libopus.
- Debt:
  - `docs/dev/debt/libSceOpusCeltDec.md:5`: the five todo rows have no credible prototypes.
  - `:10`: the module confirms only the six done argument lists, a 640-byte state, mono and stereo, five rates, a byte-sized decode capacity and a byte-count return.
  - `:11`: SILK and hybrid packets still throw.
- Twin library libSceOpusDec has the same done six. Its Create, Ctl, DecodeFloat* and *WithPriority* calls are also unknown (`docs/dev/debt/libSceOpusDec.md:5`).
- Test: `guest_opus_celt_dec` (`core/libs/tests/GuestOpusCeltDec.cpp`), built at `CMakeLists.txt:7` and registered at `:11`.

## Families

- **D (5).**
  - **Create:** the done CreateEx takes caller-allocated state. Create's arguments, and whether and how it allocates, are unknown.
  - **Ctl:** Sony's request codes and argument passing are not public.
  - **DecodeFloat:** the wrapper does not follow libopus's contract (byte capacity and byte-count return, debt `:10`), so `opus_decode_float` cannot stand in for the prototype.
  - **DecodeWithPriority and DecodeFloatWithPriority:** the AJM priority argument's position and meaning are unknown. Without them, even DecodeWithPriority cannot forward to Decode (`:25`).

## Batch proposal

None.
- Claiming PRs: 9764 (open) implements Ctl; 9752 (open) implements DecodeFloat, DecodeFloatWithPriority and DecodeWithPriority; 6361 (closed) declared all five.
- Those rows need the module's argument lists, as debt `:10` records for the done six, before they leave D.

## Caveats

- Any answer recovered for these five likely applies to the libSceOpusDec twins too.
