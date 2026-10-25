# libSceAcm (4 todo: A 0, B-S 0, B-M 0, B-L 0, C 0, D 4)

Upstream main 2742004ed2.

- **What it is:** the audio co-processor (ACM) job library: contexts, batches of DSP commands built in a caller buffer (`AcmBatchInfo` = buffer, offset, size, `core/libs/SceTypes.hpp:477`), and command encoders for FFT, panner and convolution reverb, plus batch priority and notification.
- **Existing state:** 8 done.
  - Contexts and batch ids are host counters (`core/libs/prx/libSceAcm/Export.cpp:17-19`); `sceAcmBatchStartBuffer(s)` and `sceAcmBatchWait` (`:25`, `:39`, `:53`) validate and return without running anything. Debt `docs/dev/debt/libSceAcm.md:5`: batches execute as silent no-ops, and `sceAcm_ConvReverb_SharedInput` (`:83`) returns success without encoding.
  - `sceAcm_FFT` (`:87`) and `sceAcm_Panner` (`:163`) are the tree's only command encoders, with `0x81940009` (no space) and `0x8194000A` (alignment).
  - Test: `guest_acm_tests` (`core/libs/tests/GuestAcm.cpp`), built at `core/libs/CMakeLists.txt:884` and registered at `:888` (`add_test(NAME guest_acm COMMAND guest_acm_tests)`).
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 4.
- **B and C families:** none. ACM is a private DSP command stream; no public source describes it, and the debt records the tree's descriptors as recovered from native accesses (`:9`, `:14`).
- **D families (4):**
  - Batch job control (2): JobPriority (declared `(AcmBatchInfo*, priority)`) and JobNotification (parameterless, debt `:12`). Their command words, ranges and errors are not recovered.
  - Transforms (2): IFFT and ConvReverb_SharedIr, both parameterless with no credible prototype (debt `:10`).
- **Batch proposal:** none, because there is no A, B or C family.
- **Caveats:**
  - Because batches never execute here, JobPriority could become a silent stub that returns 0 and encodes nothing, as ConvReverb_SharedInput already does. That needs a title that calls it, and the null-batch and out-of-space answers would still be guesses.
  - IFFT probably mirrors FFT's argument list, but nothing in the tree records that.
