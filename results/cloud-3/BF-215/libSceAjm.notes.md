# libSceAjm (3 todo: B-S 1, B-M 2)

**What it is:** the AJM (audio job manager) module that titles import. It is built from libSceAjm.native's sources plus three raw-buffer batch exports: two job builders that write into a bare pointer and return the next address ("Ra" = return address), and a batch start that takes a bare buffer and its size.

**Existing state:**
- The library's own `Export.cpp` has no done functions. Its three exports are parameterless declarations that throw (`core/libs/prx/libSceAjm/Export.cpp:9`, `:14`, `:19`).
- It is built by `add_sce_ajm_library` (`core/libs/prx/libSceAjm/CMakeLists.txt:1-2`, `core/libs/prx/libSceAjm.native/AjmLibrary.cmake:1-15`). That function compiles libSceAjm.native's `Export.cpp` and `src/Ajm.cpp` into the same module, so the whole native AJM implementation is present.
- There is no `docs/dev/debt/libSceAjm.md`, so the three parameterless rows have no debt entry. The native library's debt is `docs/dev/debt/libSceAjm.native.md`.
- Batch machinery in `core/libs/prx/libSceAjm.native/src/Ajm.cpp`:
  - the internal job record `PackedJob` (`:221`, 32 bytes), written by `Append` (`:234`), which checks it against `AjmBatchInfo.size`;
  - `sceAjmBatchInitialize` (`:1787`);
  - the job builders, for example `sceAjmBatchJobControl` (`:1814`) and `sceAjmBatchJobRunSplit` (`:1827`);
  - `sceAjmBatchStart` (`:1893`), which walks the records from `info->p_buffer` up to `info->offset` and runs them synchronously;
  - `sceAjmBatchWait` (`:1930`) and `sceAjmBatchCancel` (`:1943`);
  - batch ids `g_nextBatch` (`:192`) and `g_batches` (`:194`).
- Types: `AjmBatchInfo {p_buffer, offset, size}` (`core/libs/SceTypes.hpp:490`) and `AjmBatchError`, whose `job_ra` field is at `:487` (struct at `:483`). `sceAjmBatchStart` zeroes the error.
- Error constants (`src/Ajm.cpp`): INVALID_CONTEXT 0x80930002 (`:34`), INVALID_BATCH 0x80930004 (`:36`), INVALID_PARAMETER 0x80930005 (`:37`), OUT_OF_RESOURCES 0x80930007 (`:38`).
- Tests: `guest_ajm_tests` (`core/libs/tests/GuestAjm.cpp`), registered at `core/libs/prx/libSceAjm.native/CMakeLists.txt:55` with `add_test` at `:59`. It links `libSceAjm.native` (`:57`), not `libSceAjm`, so it cannot reach these three exports. The vibration, WVorbis and teardown tests are registered at `:60`, `:65` and `:71`.
- Prototype source: shadPS4's `ajm.h` at `15abf68`. `docs/dev/debt/libSceAjm.native.md:6` cites that file at that commit (for `L222-L223`); I opened it from that link. It declares:
  - `void* sceAjmBatchJobControlBufferRa(void* p_buffer, u32 instance_id, u64 flags, void* p_sideband_input, size_t sideband_input_size, void* p_sideband_output, size_t sideband_output_size, void* p_return_address)` (`L191-L195`);
  - `void* sceAjmBatchJobRunSplitBufferRa(void* p_buffer, u32 instance_id, u64 flags, const AjmBuffer* inputs, size_t num_inputs, const AjmBuffer* outputs, size_t num_outputs, void* p_sideband_output, size_t sideband_output_size, void* p_return_address)` (`L204-L208`);
  - `int sceAjmBatchStartBuffer(u32 context, u8* batch, u32 batch_size, const int priority, AjmBatchError* batch_error, u32* out_batch_id)` (`L209-L210`).

**Counts:** A 0, B-S 1, B-M 2, B-L 0, C-real 0, C-silent 0, D 0.

## Families

- **B-S (1), `sceAjmBatchStartBuffer`:** this is `sceAjmBatchStart` over the range `[batch, batch + batch_size)` in place of an `AjmBatchInfo`.
  - It keeps the same context and null checks (`src/Ajm.cpp:1894`, `:1896`) and registers the batch in `g_batches` the same way.
  - Factor the record loop of `:1898-1919` into a helper that takes a byte range, and call it from both entry points.
- **B-M (2), the `BufferRa` builders:**
  - The bodies already exist: each builds the same job as `sceAjmBatchJobControl` (`:1814`) or `sceAjmBatchJobRunSplit` (`:1827`), writes it at `p_buffer` and returns `p_buffer + record size`.
  - The open part is the record size. These calls have no size argument, and SDK-built titles size the buffer for the console's job layout. The internal `PackedJob` record (32 bytes, plus 16 per buffer, plus parameters) must therefore be no larger than the console's job, or the writer must emit the console layout itself.
  - The tree has no console job sizes. The same shadPS4 commit's batch code is the public source to read them from.
  - `p_return_address` belongs in `AjmBatchError.job_ra`. The tree reports no job errors (the error is zeroed at `:1920`), so it can be kept with the record or dropped.

## Batch proposals

1. **"ajm: raw-buffer batch builders and start" (3):** `sceAjmBatchStartBuffer`, `sceAjmBatchJobControlBufferRa` and `sceAjmBatchJobRunSplitBufferRa`.
   - Files:
     - `core/libs/prx/libSceAjm/Export.cpp` and `include/Api.hpp`: real prototypes from shadPS4 `ajm.h` L191-L210;
     - `core/libs/prx/libSceAjm.native/src/Ajm.cpp`: expose a cursor-based `Append` and a range-based batch runner;
     - a new `docs/dev/debt/libSceAjm.md` for the unverified record sizes.
   - Semantics: shadPS4 `ajm.h` at `15abf68` (linked from `docs/dev/debt/libSceAjm.native.md:6`) for the signatures. The job bodies are the existing `sceAjmBatchJobControl`, `sceAjmBatchJobRunSplit` and `sceAjmBatchStart`.
   - Before landing the builders, compare the console job sizes from the same shadPS4 commit's batch code with `PackedJob`. If a job could be larger here, throw for it.
   - Tests:
     - add `guest_ajm_raw_buffer_tests` (`core/libs/tests/GuestAjmRawBuffer.cpp`), registered in `core/libs/prx/libSceAjm/CMakeLists.txt` under `if(BUILD_TESTING)` and linking `libSceAjm` and `libc`. `guest_ajm_tests` links `libSceAjm.native`, which lacks these exports, and linking both modules would duplicate the native symbols.
     - Cases:
       - a codec-23 (PCM) or ATRAC9 decode built with `RunSplitBufferRa` returns `p_buffer` plus the record size, and `StartBuffer(context, buf, end - buf)` then `sceAjmBatchWait` produce the same PCM and sideband as the same job through `sceAjmBatchJobRunSplit` and `sceAjmBatchStart`;
       - `ControlBufferRa` with INITIALIZE (0x4000) followed by a run decodes;
       - `StartBuffer` returns 0x80930002 for context 0 and an unknown context, and 0x80930005 for a null batch id;
       - a zero-size buffer starts an empty batch that `sceAjmBatchWait` accepts.
   - Smaller than 10. It merges naturally with libSceAjmi's `sceAjmBatchJobInlineBuffer` and `sceAjmBatchJobRunBufferRa` (`core/libs/prx/libSceAjmi/Export.cpp:11`, `:16`), which shadPS4 declares in the same header (`L196-L203`), to make a 5-function "AJM raw-buffer API" batch.

## Dependency notes

- No new subsystem and no new third-party code.
- The only open input is the console's per-job byte size, which the raw builders must not exceed. It comes from the same shadPS4 commit's batch code; nothing in the tree measures it.
