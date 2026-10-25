# libSceM4aacEnc (17 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 17)

**What it is:** the MPEG-4 AAC encoder: create and delete an encoder, encode and flush, and clear its context, each with `Ex` and `WithPriority` forms, plus Initialize, Terminate and GetContext (sysmodule `0x000000bc`, `core/libs/prx/libSceSysmodule/ModuleTable.hpp:77`).

**Existing state** (`core/libs/prx/libSceM4aacEnc/Export.cpp`):
- Built: `libSceM4aacEnc/CMakeLists.txt:1` compiles `Export.cpp`.
- 0 done. Every row throws. There is no encoder, no state and no error constant.
- Debt (`docs/dev/debt/libSceM4aacEnc.md`):
  - `:5`: there is no encoder, the names come from aerolib, and PPSA03332 imports them and fails to load when the library is absent;
  - `:6`: five signatures come from Orbital; the other twelve are declared without parameters.
- Orbital's five, at cc408367 (https://github.com/AlexAltea/orbital/blob/cc408367da6a6ebbf5ac1ac559fefeadc73b2831/tools/ida/db_types.json), lines 1261-1265: ClearContext, CreateEncoder, DeleteEncoder, Encode and Flush. They match `include/Api.hpp:7`, `:9`, `:11`, `:13`, `:17`.
- No AAC encoder anywhere in the tree's code: AJM's `sceAjmBatchJobEncode` throws (`core/libs/prx/libSceAjm.native/Export.cpp:56-66`).
- Test: `guest_m4aac_enc_tests` (`core/libs/tests/GuestM4aacEnc.cpp`), built at `CMakeLists.txt:6` and registered at `:10` (`add_test(NAME guest_m4aac_enc ...)`). It asserts all seventeen throw `std::runtime_error` and leave the outputs untouched (`GuestM4aacEnc.cpp:27-46`).

## Families

- **D (5): the Orbital-prototyped calls** (CreateEncoder `:19`, DeleteEncoder `:29`, Encode `:39`, Flush `:59`, ClearContext `:9`).
  - The argument lists are known. The semantics are not:
    - the `inputFormat` and `outputFormat` codes (PCM layout, ADTS or raw output);
    - the accepted channel counts, rates and bitrates;
    - the handle values;
    - the `pInternalError` codes and the function error codes;
    - how many bytes Encode consumes and emits per call;
    - whether `sceM4aacEncInitialize` (no prototype) must run first.
  - Without them, an implementation would guess every answer.
  - With them, these five would be **B-L**: the encoder itself is a missing subsystem (ISO/IEC 14496-3 AAC).
- **D (12): no prototype.**
  - ClearContextEx, CreateEncoderEx, DeleteEncoderEx, EncodeEx, EncodeWithPriority, EncodeWithPriorityEx, FlushEx, FlushWithPriority and FlushWithPriorityEx are not twins of their base calls, because their extra arguments are unknown.
  - GetContext, Initialize and Terminate have no source at all (debt `:6`).

## Batch proposal

None.

## Caveats

- Whether the `3rdparty/ffmpeg-core` submodule (`.gitmodules`, built at `CMakeLists.txt:326`) builds an AAC encoder was not checked: the submodule is not checked out in this tree. Even if it does, the Sony API semantics above are the blocker.
- PPSA03332 needs only the library to exist so it can load (debt `:5`). That is already met. No title hit names a function here.
