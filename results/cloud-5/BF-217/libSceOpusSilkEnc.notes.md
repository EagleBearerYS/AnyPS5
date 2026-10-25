# libSceOpusSilkEnc (14 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 14)

**What it is:** the Opus SILK-mode encoder over AJM. It has:
- context initialize and terminate;
- encoder create, with or without a context, and destroy, state size and control;
- 16-bit and float encodes: synchronous, with a priority, or asynchronous with a timeout and a batch id;
- batch cancel.

**Existing state:**
- No own `CMakeLists.txt`. `core/libs/prx/libSceOpusSilkEnc/Export.cpp` is built by `core/libs/CMakeLists.txt:159` and linked only to libkernel and libc (`:170`). libopus is not linked; libSceOpusDec links it at `core/libs/prx/libSceOpusDec/CMakeLists.txt:3`.
- 0 done. `OpusSilkEncoder` is opaque (`include/Api.hpp:5`).
- The signatures were restored in 45555f165f ("fix(opussilk): restore encoding and batch control signatures"). That commit also replaced the debt's "no credible prototypes" entry with one line about Terminate's return register (`docs/dev/debt/libSceOpusSilkEnc.md:5`). The other 13 rows now throw with no debt entry describing the gap.
- No tests.
- Neighbours:
  - libSceOpusCeltEnc has the same shapes for 11 names, all todo, with the same Terminate note (`docs/dev/debt/libSceOpusCeltEnc.md:5`).
  - libSceOpusDec and libSceOpusCeltDec are done decoders over `OpusDecoderLibrary` (`core/libs/prx/libSceOpusDec/include/OpusDecoder.hpp:19-123`):
    - context registry (`:23-43`);
    - 640-byte state (`:45-48`);
    - five creation rates (`:50-68`);
    - per-library error bases 0x81740000 and 0x81400000 (`:116-118`);
    - these were confirmed from the supplied module (`docs/dev/debt/libSceOpusDec.md:9`).
  - Tests `guest_opus_dec` and `guest_opus_celt_dec` (`libSceOpusDec/CMakeLists.txt:11`, `libSceOpusCeltDec/CMakeLists.txt:11`).
- Codec: the libopus submodule (`cmake/Opus.cmake:11`, at 22244de5). Forcing SILK mode needs `OPUS_SET_FORCE_MODE`, which is in libopus's private header ([`src/opus_private.h#L148`, `#L172-L173`](https://github.com/xiph/opus/blob/22244de5a79bd1d6d623c32e72bf1954b56235be/src/opus_private.h#L172-L173)), not in its public `include/opus_defines.h`.

## Families

- **Context (2): Initialize (`:69`) and Terminate (`:74`), D.**
  - The decoder registry pattern exists, but SilkEnc's error for a null pointer is unrecorded, and the decoders' error base is per library.
  - Terminate's return contract is not established (debt `:5`).
- **Encoder object (5): Create (`:14`), CreateEx (`:19`), Destroy (`:29`), GetSize (`:64`) and Ctl (`:24`), D.**
  - The encoder state size, the accepted rates, channels and applications, the errors and the Ctl request numbering are not recorded anywhere.
- **Encodes (6): Encode (`:34`), EncodeFloat (`:39`), the two WithPriority forms (`:44`, `:54`) and the two WithPriorityAndTimeout forms (`:49`, `:59`), D.**
  - libopus can encode, but the wrapper contract is unknown: frame sizes, the return value, errors and SILK forcing.
  - The asynchronous forms add AJM priority, timeout and batch semantics.
- **Cancel (`:9`), D:** it cancels a batch from the asynchronous forms.

## Batch proposal

None.
- What would unblock it: the encoder's state size, error base and accepted configurations, from the supplied module as libSceOpusDec's were.
- With those, Initialize, Terminate, GetSize, CreateEx, Destroy and the two synchronous encodes would be B-M: an encoder class next to `OpusDecoderLibrary`, plus an own `CMakeLists.txt` that links opus and registers a guest test.

## Caveats

- PR 10563 (open) claims all 14 rows: it implements Create, CreateEx, Destroy, Encode, EncodeFloat, GetSize, Initialize and Terminate, and changes the other six. Its source for the encoder contract should be checked against the gaps above before review.
