# libSceAjmi (2 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 2)

**What it is:** a second audio-job-manager client module (sysmodule `0x8000007e`, `core/libs/prx/libSceSysmodule/ModuleTable.hpp:291`). Here it carries only two raw-buffer batch builders: InlineBuffer and RunBufferRa.

**Existing state:**
- Built: no `CMakeLists.txt` of its own, so `core/libs/CMakeLists.txt:159` compiles `core/libs/prx/libSceAjmi/Export.cpp`.
- 0 done. The two rows are `(void)` declarations (`Export.cpp:11`, `:16`) after `APS5_DUMMY_FUN` (`:9`). There is no state, no error constant and no test.
- Debt (`docs/dev/debt/libSceAjmi.md:5`): "5 named callable exports" from the 3.20 table, with signatures and behaviour unverified.
- Related code in other libraries:
  - the done AJM batch API is in libSceAjm.native: `sceAjmBatchInitialize` (`core/libs/prx/libSceAjm.native/src/Ajm.cpp:1787`), `sceAjmBatchJobRun` (`:1835`) and `sceAjmBatchStart` (`:1893`). Its job records are private to the library (`:29-31`) and appended into an `AjmBatchInfo` with a capacity check (`Append`, `:234-252`);
  - the raw-buffer forms ControlBufferRa, RunSplitBufferRa and StartBuffer are todo in libSceAjm (`core/libs/prx/libSceAjm/Export.cpp:9`, `:14`, `:19`).

## Families

- **D (2): raw-buffer batch builders.**
  - Prototypes exist only for PS4 libSceAjm, in shadPS4 at 15abf68 (the commit `docs/dev/debt/libSceAjm.native.md:6` cites):
    - `void* sceAjmBatchJobInlineBuffer(void* p_buffer, const void* p_data_input, size_t data_input_size, const void** pp_batch_address)` (https://github.com/shadps4-emu/shadPS4/blob/15abf68c9b52c4b04d094f2837719fdb871021f3/src/core/libraries/ajm/ajm.h#L196-L198);
    - `void* sceAjmBatchJobRunBufferRa(void* p_buffer, u32 instance_id, u64 flags, void* p_data_input, size_t data_input_size, void* p_data_output, size_t data_output_size, void* p_sideband_output, size_t sideband_output_size, void* p_return_address)` (`ajm.h#L199-L203`).
  - Both write the console's chunk layout into a raw buffer and return the next write position: InlineBuffer copies the input and reports where it landed (https://github.com/shadps4-emu/shadPS4/blob/15abf68c9b52c4b04d094f2837719fdb871021f3/src/core/libraries/ajm/ajm_batch.cpp#L406-L420), and RunBufferRa writes a job with return-address, input and flag chunks (`ajm_batch.cpp#L422-L462`).
  - RunBufferRa is not an A twin of the done `sceAjmBatchJobRun`. It takes a raw buffer instead of `AjmBatchInfo`, adds a return address and returns a pointer instead of an error code.
  - D, because:
    - the PS5 Ajmi signatures are unverified (debt `:5`); SharpProspero's PS5 `Ajm.cs` (https://github.com/SvenGDK/SharpProspero/blob/5dfdb4b0b166e83dc9ae275001afb695f12c7174/src/SharpProspero/Interop/Audio/Ajm.cs) has no buffer forms;
    - their only reader, `sceAjmBatchStartBuffer`, is todo (`libSceAjm/Export.cpp:19`) and not declared in this module;
    - which layout to write is undecided: the console's chunks (shadPS4) or the tree's private records. A raw batch carries no capacity, so a private encoding must still fit whatever the guest reserved for the console's layout.

## Batch proposal

None. These rows belong with the libSceAjm raw-buffer family. Promoting them to B-M needs three decisions together:
- accept shadPS4's PS4 prototypes for PS5;
- choose the encoding;
- implement `sceAjmBatchStartBuffer` to read it.

Claims: #8747 (closed) named both in libSceAjm. #9637 (open) moves `sceAjmBatchJobRunBufferRa` into libSceAjm.native.

## Caveats

- **Stale debt count.** The debt says 5 exports, but this `Export.cpp` declares 2. The other three of the five (ControlBufferRa, RunSplitBufferRa, StartBuffer) are now declared in `libSceAjm/Export.cpp:9-22`. The 3.20 stub file the debt cites (https://github.com/DNNDHH/PS5-3.20_Libs/blob/b2a77c09cd71c01f34c790cfbe98c751a13262ad/libSceAjmi.c) lists 21 Ajm names for this module, so how the 5 were picked is not recorded.
