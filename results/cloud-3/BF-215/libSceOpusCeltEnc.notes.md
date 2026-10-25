# libSceOpusCeltEnc (11 todo: B-S 2, B-M 8, D 1)

**What it is:** the AJM-backed Opus encoder in CELT mode. AJM context init and terminate; encoder state size, create (with or without a context), destroy and ctl; encoding of int16 or float PCM, with or without a priority.

**Existing state:**
- 0 done. Eleven typed declarations in `core/libs/prx/libSceOpusCeltEnc/Export.cpp:9-62` (`include/Api.hpp:9-19`). Their parameter lists match libopus' encoder API: `opus_encoder_get_size`, `opus_encoder_init`, `opus_encode`, `opus_encode_float` and `opus_encoder_ctl`.
  - The opaque state is `struct OpusCeltEncoder` (`include/Api.hpp:5`).
  - The only debt entry is `docs/dev/debt/libSceOpusCeltEnc.md:5`: Terminate's context pointer is confirmed, and its null path defines no return value.
- No `CMakeLists.txt` (built from the glob at `core/libs/CMakeLists.txt:146`, `:159`, without libopus) and no guest test.
- Neighbour: `OpusDecoderLibrary` (`core/libs/prx/libSceOpusDec/include/OpusDecoder.hpp:19-123`), used by libSceOpusCeltDec (`core/libs/prx/libSceOpusCeltDec/Export.cpp:9-29`) and libSceOpusDec. It keeps:
  - an AJM-context registry (`Initialize` `:23-33`, `Terminate` `:35-43`);
  - host-side libopus decoders keyed by the guest state pointer (`CreateEx` `:50-68`, `Destroy` `:94-99`);
  - error bases 0x81400000 (CeltDec) and 0x81740000 (`:116-118`).
- The decoder's sizes, units and errors were recovered from its module (`docs/dev/debt/libSceOpusCeltDec.md:10`):
  - a 640-byte state;
  - capacity counted in bytes;
  - a byte-count return.
- libopus is a submodule (`.gitmodules:25-27`), built in full, encoder included (`cmake/Opus.cmake:11`). `core/libs/prx/libSceOpusCeltDec/CMakeLists.txt:3` links it.
- Decoder tests: `guest_opus_celt_dec_tests` (`core/libs/tests/GuestOpusCeltDec.cpp`), registered at `core/libs/prx/libSceOpusCeltDec/CMakeLists.txt:7` and `:11`.

**Counts:** A 0, B-S 2, B-M 8, B-L 0, C-real 0, C-silent 0, D 1.

## Families

- **B-S (2): AJM context.** `Initialize` and `Terminate` reuse the context registry of `OpusDecoderLibrary` (`OpusDecoder.hpp:23-43`).
  - Terminate throws for an unknown context or live encoders.
  - The encoder library's error code for a null pointer is unmeasured: the decoder's 0x8140000D is its own. A null Initialize throws, and so does a null Terminate, whose return is undefined (debt `:5`).
- **B-M (8): libopus encoder.**
  - `Create` and `CreateEx` use `opus_encoder_init` semantics. The encoder is kept host-side, keyed by the guest state pointer, as the decoder does.
  - `Encode` and `EncodeFloat` use `opus_encode` and `opus_encode_float`. `EncodeWithPriority` and `EncodeFloatWithPriority` do the same; the priority orders only AJM jobs, which the host runs synchronously (`docs/dev/debt/libSceOpusCeltDec.md:11`).
  - `Ctl` forwards the public encoder CTL requests from libopus' `opus_defines.h`, with their documented argument types, to `opus_encoder_ctl`.
  - `Destroy` mirrors `OpusDecoderLibrary::Destroy`.
  - Output must be CELT-only packets: RFC 6716 section 3.1, TOC configurations 16-31. The CeltDec wrapper rejects anything else (`docs/dev/debt/libSceOpusCeltDec.md:11`).
  - Unmeasured, so they throw: encoder error codes, accepted rates/applications, the context `Create` binds, which CTLs the console accepts, and the priority range.
- **D (1): `GetSize`.** The guest state size is a console constant, as the decoder's 640 was (debt `libSceOpusCeltDec.md:10`). Nothing in the tree gives the encoder's.
  - Returning `opus_encoder_get_size` or any host-chosen number would be a guess, and a title that allocates a fixed SDK size could overrun it.
  - Measure it from the module first.

## Batch proposals

1. **"opuscelt: encoder over libopus" (10: the B-S and B-M rows):** `sceOpusCeltEncInitialize`, `Terminate`, `Create`, `CreateEx`, `Destroy`, `Ctl`, `Encode`, `EncodeFloat`, `EncodeWithPriority`, `EncodeFloatWithPriority`.
   - Files:
     - `core/libs/prx/libSceOpusCeltEnc/Export.cpp`;
     - a new shared header beside `core/libs/prx/libSceOpusDec/include/OpusDecoder.hpp`, with an encoder registry. Ideally it factors the context registry (`:23-43`) out of `OpusDecoderLibrary` for both;
     - a new `core/libs/prx/libSceOpusCeltEnc/CMakeLists.txt` modeled on `core/libs/prx/libSceOpusCeltDec/CMakeLists.txt:1-16`, linking `opus` (`:3`);
     - `docs/dev/debt/libSceOpusCeltEnc.md`.
   - Semantics:
     - libopus' encoder API (submodule `.gitmodules:25`);
     - RFC 6716 section 3.1, for the TOC of the CELT-only packets. Select CELT with the public `OPUS_APPLICATION_RESTRICTED_LOWDELAY` application, or libopus' private force-mode CTL;
     - `OpusDecoderLibrary`'s registry and validation order as the model.
   - Throw where unmeasured: error codes, frameSize units (the decoder counts in bytes), the rate/application sets, the CTL subset and the priority range.
   - Tests: a new `guest_opus_celt_enc_tests` (`core/libs/tests/GuestOpusCeltEnc.cpp`), registered in the new CMakeLists as `guest_opus_celt_dec_tests` is (`core/libs/prx/libSceOpusCeltDec/CMakeLists.txt:7`, `:11`). Cases:
     - Initialize/Terminate round trip; a second Initialize of the same pointer throws; Terminate with a live encoder throws;
     - CreateEx at 48 kHz stereo and mono;
     - encode a 20 ms tone (960 samples per channel): the TOC configuration is at least 16, `opus_packet_get_nb_samples` is 960, and libopus decodes it to the tone (reuse `RequireTone`, `GuestOpusCeltDec.cpp:55`);
     - the float variant matches;
     - the priority variants return the same length as the plain ones;
     - Ctl `OPUS_SET_BITRATE` then `OPUS_GET_BITRATE` round trips;
     - Destroy, then Encode on the destroyed state throws or errors.
   - Size: 10 functions. It could merge with libSceOpusSilkEnc, whose 14 declarations have the same shape (`core/libs/prx/libSceOpusSilkEnc/Export.cpp:9-77`), so one encoder registry serves both.
   - GetSize stays throwing until measured, which limits what titles can do with the batch.

## Dependency notes

- No new third-party code: libopus is already a submodule built from source (`cmake/Opus.cmake:1-19`). The library needs its own `CMakeLists.txt` to link it, because the glob build (`core/libs/CMakeLists.txt:159`) links no libopus.

## Caveats

- The declarations' parameter names follow libopus, but only Terminate's context pointer is confirmed in the debt. The CeltDec wrapper departs from libopus: byte capacity, byte-count return and no frame_size/decode_fec arguments. So frameSize units and Encode's return should be checked against the module before the batch is merged.
- Encoder output cannot be bit-exact to the console's encoder. Any valid CELT-only packet is a correct answer for a decoder, but not for a title that compares bytes.
