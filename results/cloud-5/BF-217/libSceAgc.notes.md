# libSceAgc (10 todo: A 0, B-S 4, B-M 2, B-L 0, C-real 0, C-silent 0, D 4)

**What it is:** the PS5 graphics command-buffer library. It writes DCB (graphics) and ACB (asynchronous compute) packets, patches packets already written, hands out register defaults and builds shader helpers. The packets run on the libSceAgcDriver PM4 executor.

**Existing state:**
- 230 done, spread over the sources listed in `core/libs/prx/libSceAgc/CMakeLists.txt:1-57`.
- **`Export.cpp` is not built.** The source list (`CMakeLists.txt:1-57`) has `Unimplemented.cpp` and the subdirectory sources, but not `Export.cpp`.
  - Its only function, `sceAgcSuspendPointAndCheckStatus` (`Export.cpp:9`), is therefore not exported by libSceAgc.
  - The same name is exported, and throws, from libSceAgcVsh (`core/libs/prx/libSceAgcVsh/Export.cpp:500-504`, NID `b+fis+WZ3Ig`).
- **Workload streams:**
  - Registration lives in libSceAgcDriver: a bit mask for ids 1-31, with errors 0x8A6C0033, 0x8A6C0035 and 0x8A6C003A (`core/libs/prx/libSceAgcDriver/Resource/src/Registration.cpp:12-27`, `:54-95`). These match the console probe below (`workload2.console.log:7-20`).
  - DCB active and complete are done with KytyPS5's `IT_NOP` layout (`DcbState/src/Workload.cpp:30-56`, debt `:44`).
  - The four todo rows return null for a null buffer or an unregistered stream, and throw for a registered one (`Workload.cpp:58-62`; `Acb/src/Control.cpp:16-20`, `:85-98`; debt `:45`).
  - The driver's packet-size getters already give the console's sizes: 18, 12 and 9 dwords (`core/libs/prx/libSceAgcDriver/Submit/src/Dcb.cpp:31-34`, `:49-58`).
- **Console measurement.** Debt `:46` links a PS5 (firmware 13.60) probe. I read its revision `227a619401e22c04fb812d20506f215757ae9499`:
  - `https://gist.github.com/orezraey/befe7632b827421988b47a6c04e89e1d/raw/227a619401e22c04fb812d20506f215757ae9499/workload.console.log`
  - and `.../workload.c` in the same revision.
  - It records every word of all six workload calls for registered streams 1, 31 and 3 (`workload.console.log:55-86`, `:88-119`, `:120-151`). It also records every null case and the call signatures (`workload.c:17-19`, `:95-100`).
- **Executor** (`core/libs/prx/libSceAgcDriver/Execution/src/Pm4.cpp`):
  - Opcode 0x1E is unknown (`:238`). It is not named in `Execution/include/Pm4Opcodes.hpp`.
  - An unpredicated SET_UCONFIG_REG is a graphics-only packet (`:442`, check at `:260`). Only the predicated 0x342 tag marker passes on a compute queue (`:535-538`).
  - Header flags: WRITE_DATA allows only bit 0, while register writes allow bits 0-2 (`:290-293`).
  - A WRITE_DATA to a register may target only 0xC340-0xC347 (`:491-497`).
- **Patchers:**
  - The WRITE_DATA patchers validate the opcode and throw on a wrong one (`Patch/src/WaitRegMem.cpp:12-39`).
  - `sceAgcDcbAcquireMem` writes the engine at cmd[1] bit 31 (`DcbFlow/src/Memory.cpp:13`, `:24`).
  - The executor accepts either engine (`Pm4.cpp:419-423`).
- **Interpolant mapping:**
  - One helper, `CreateInterpolantMapping(fn, regs, gs, ps, valueTransform, fillIdentityTail)` (`Shader/src/InterpolantMapping.cpp:61`).
  - `Create_0100` uses the plain transform with the tail (`:123-125`).
  - The `_10000` and unversioned Create/Update pairs differ only in the tail flag (`:127-140`).
  - `Update_0100` alone sits in `Misc/src/ShaderFusion.cpp:372`.
- **Debt** (`docs/dev/debt/libSceAgc.md`): `:11` (SetSubmitMode), `:22` (Update_0100), `:39` (SuspendPointAndCheckStatus), `:44-46` (workload), `:50-51` (the `Unimplemented.cpp` rows).
- **Tests:** 25 registered. The ones these rows touch:
  - `agc_commands`: `tests/Commands.cpp`, with `testWorkloads` at `:224` and `testWorkloadStreams` at `:256`; executable at `CMakeLists.txt:72`, registered at `:79`;
  - `agc_write_data_patch_tests` (`CMakeLists.txt:384-397`);
  - `agc_interpolant_mapping` (`CMakeLists.txt:296-308`);
  - `agc_driver_pm4`: executable at `core/libs/prx/libSceAgcDriver/CMakeLists.txt:1973`, registered by the loop at `:2504-2510`.

## Families

- **Workload stream packets (4: B-S 2, B-M 2).** For a registered stream, the probe gives the console's packets.
  - `sceAgcDcbSetWorkloadStreamInactive` and `sceAgcAcbSetWorkloadStreamInactive` are **B-S**. Each writes 9 dwords:
    - a SET_UCONFIG_REG of 0x342 with value `0xCC000000 | stream` and then 0;
    - a WRITE_DATA `06010000 0000c343 0 0`.
    - The header's low byte is 0x04 on DCB and 0x00 on ACB (`workload.console.log:70`, `:86`, `:103`, `:119`). This is OpenAGC's inactive builder (`agc_workload_packet.c` lines 85-104 at OpenAGC `3c7e8072`), with ACB passing the control flag (`game_compat.c` lines 2317-2318).
    - Executor work is small:
      - DCB needs header flag 0x04 allowed on WRITE_DATA (`Pm4.cpp:290-293`);
      - ACB needs the unpredicated 0x342 write accepted on a compute queue (`Pm4.cpp:442`).
  - `sceAgcAcbSetWorkloadsActive` and `sceAgcAcbSetWorkloadComplete` are **B-M**. Their words are fully measured:
    - active is 18 dwords (`:71-73`, `:77`);
    - complete is 12 dwords (`:80-84`);
    - every null case is measured (`:74-76`, `:78-79`, `:85`).
    - Both end in a 9-dword opcode 0x1E packet: control 0x267 or 0x275, the stream's 64-bit slot address (`0xF_E003A000 + 8 * stream` on the console), then the mask or the clear mask.
    - The new component is a slot table owned by libSceAgcDriver plus an executor handler for 0x1E, which the debt says does not exist (`:46`).
  - Not **A**: the done DCB twins write KytyPS5's `IT_NOP` packets, not these. The header byte also differs between DCB and ACB (debt `:46`).
- **Packet patcher (B-S 1): `sceAgcAcquireMemSetEngine`.** It validates an ACQUIRE_MEM (0x58) packet and sets cmd[1] bit 31 to the engine.
  - That bit is where `sceAgcDcbAcquireMem` writes the engine (`DcbFlow/src/Memory.cpp:13`, `:24`).
  - OpenAGC uses the same encoding (`game_compat.c` lines 2626-2646 at `3c7e8072`). OpenAGC returns 0x8A6C000C on a wrong opcode, but the tree's patchers throw instead (`WaitRegMem.cpp:12-15`).
- **Interpolant mapping (B-S 1): `sceAgcUpdateInterpolantMapping_0100`.** It is `CreateInterpolantMapping` with the 0100 transform and no identity tail, so it writes one register per pixel-shader input.
  - Every other version pairs Update with Create this way (`InterpolantMapping.cpp:127-140`).
  - OpenAGC pairs its 10000 forms the same way (`game_compat.c` lines 1673-1689).
- **D (4):**
  - `sceAgcAcbInitializeDefaultHardwareState`: parameterless, and no source gives its packets (debt `:50`).
  - `sceAgcCbMemsetExclusive`: the signature is known but the packet is not (debt `:51`). The tree has no memset builder, and OpenAGC has none either.
  - `sceAgcSetSubmitMode`: mode values unknown (debt `:11`); mode 0 already returns 0 (`Misc/src/Suspend.cpp:16-18`).
  - `sceAgcSuspendPointAndCheckStatus`: parameterless, and the status it reports is unknown (debt `:39`). Not exported, because `Export.cpp` is not built.

## Batch proposal

1. **`feat(libSceAgc): write the measured workload stream packets`** (4 rows: both StreamInactive calls, ACB active and ACB complete).
   - Semantics: the probe's words. Use the measured WRITE_DATA order, not OpenAGC's (see Caveats).
   - The same PR should move the done DCB active and complete (`Workload.cpp:30-56`) to the measured layout, so the DCB and ACB families agree.
   - Driver and executor work:
     - a per-stream 64-bit slot table in libSceAgcDriver, whose address goes into the packet;
     - a 0x1E handler that applies the mask (active) or the clear mask (complete) to the slot. The operands imply this; the control words 0x267 and 0x275 are not decoded anywhere in the tree;
     - unpredicated 0x342 SET_UCONFIG_REG on compute queues;
     - header flag 0x04 on WRITE_DATA.
   - Tests:
     - extend `testWorkloadStreams` (`tests/Commands.cpp:256`; `agc_commands`, `CMakeLists.txt:72`, `:79`): register streams 1, 3 and 31, then compare the words with `workload.console.log:70`, `:86`, `:71-85`, `:103`, `:119` (stream-id fields and slot address adjusted);
     - check the null cases (all in `workload.console.log`):
       - duplicate, id 64, count 0 and 64, and null ids (`:74-79`, `:85`);
       - never registered (`:14-45`);
       - stream 2, whose null registration failed (`:152-183`);
       - after unregistering (`:191-222`);
     - extend `agc_driver_pm4` (`core/libs/prx/libSceAgcDriver/tests/Pm4.cpp`): the packets run on a graphics queue and a compute queue, and the slot holds the expected mask.
   - AGENTS.md requires, for AGC changes, the same focused `ctest` runs on main and on the change with `ANYPS5_REQUIRE_VULKAN=1`, `ANYPS5_NO_SHADER_CACHE=1` and the Khronos validation layer. The PR must show no new `VUID-` errors.
   - Claims:
     - PR 6128 and PR 9821 (closed) implemented all four;
     - PR 10654 (open) implements the three ACB rows.
     - Each should be checked against the measured words.
2. **`feat(libSceAgc): sceAgcAcquireMemSetEngine and sceAgcUpdateInterpolantMapping_0100`** (2 rows, small).
   - AcquireMemSetEngine:
     - move it out of `Unimplemented.cpp` beside the WRITE_DATA patchers;
     - add that source to `agc_write_data_patch_tests` (`CMakeLists.txt:384-397`, which lists its sources explicitly);
     - test that engine 0 and 1 set and clear bit 31 of a `sceAgcDcbAcquireMem` packet and leave the other words unchanged;
     - test that a WRITE_DATA packet throws, and that engine 2 throws (`CheckBits`).
     - Claims: PR 1060 (open) and PR 9821 (closed).
   - Update_0100:
     - move it to `Shader/src/InterpolantMapping.cpp`, since the helper is file-local there (`:61`);
     - in `agc_interpolant_mapping` (`CMakeLists.txt:296-308`), its first `num_input_semantics` registers must equal those of `sceAgcCreateInterpolantMapping_0100`;
     - a pixel shader without inputs writes nothing, and a null pixel shader throws.
     - Update debt `:22`.
     - Claims: PR 4826 and PR 9821 (closed), PR 10736 (open).

## Caveats

- **Stale debt line.** Debt `:45` says "the packet a registered stream gets is not measured". The probe that debt `:46` links measures exactly that.
- **Done functions that contradict the measurement.** `sceAgcDcbSetWorkloadsActive` and `sceAgcDcbSetWorkloadComplete` (`Workload.cpp:30-56`) are done, but they write `IT_NOP` packets that differ from the console words at `workload.console.log:55-69`.
- **OpenAGC's active layout is wrong in one place.** It puts the mask's high half in WRITE_DATA dword 7 and 0 in dword 8 (`agc_workload_packet.c` lines 36-40). The console writes 0 in dword 7 (the address high) and the mask's high half in dword 8 (`workload.console.log:56`: `00000000 80000000`). The executor's register-WRITE_DATA check (`Pm4.cpp:497`, `packet[3] == 0`) only accepts the console's order.
- **Slot address.** On the console it is a fixed GPU address. The pointer given to `sceAgcDriverRegisterWorkloadStream` is never written (`workload.console.log:47`, `:87`, `:190`). AnyPS5 must choose its own address.
- **Null command buffer.** The console faults on one (`workload.console.log:184`), while the tree returns null. That needs no change.
- **ACB signatures.** No public ACB declaration exists (debt `:45`). The probe called the ACB entry points with the DCB signatures (`workload.c:17-19`, `:98-100`), and the words it got back depend on every argument.
- **Update_0100 is an inference.** Its `vs` parameter name and its old placeholder name `sceAgcCreateInterpolantMappingVsPs` (renamed in commit 39191f905d) may hint at a different input. The pairing with Create should be checked against a title before relying on it.
- **SetSubmitMode.** It returns 0 for mode 0 without effect. The probe's driver log prints `submit.mode= 1` at initialization (`workload.console.log:2`), so 0 may not be the console default.
