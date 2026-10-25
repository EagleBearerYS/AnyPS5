# libSceTEEClient (10 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 10)

**What it is:** the client for the console's trusted execution environment, the AMD secure processor (sysmodule `0x800000a2`, `core/libs/prx/libSceSysmodule/ModuleTable.hpp:324`). It has three kinds of names:
- five GlobalPlatform TEE Client API calls (`TEEC_*`);
- two AMD extensions (`TEECI_*`);
- three AMD debug calls (`AMDTEE_DLM_*`).

**Existing state:**
- Built: no `CMakeLists.txt` of its own, so `core/libs/CMakeLists.txt:159` compiles `core/libs/prx/libSceTEEClient/Export.cpp`.
- 0 done. Ten `(void)` declarations with the `_nid_postfix` suffix (`Export.cpp:10-58`) after `APS5_DUMMY_FUN` (`:9`). There is no state, no error constant and no test.
- Debt (`docs/dev/debt/libSceTEEClient.md:5`): ten callable exports from the 3.20 table, cross-checked against 13.60; signatures and behaviour unverified.

## Families

- **D (5): GlobalPlatform calls** (CloseSession `:35`, InvokeCommand `:40`, OpenSession `:45`, ReleaseSharedMemory `:50`, RequestCancellation `:55`).
  - The GP API has public prototypes. For example, OP-TEE's `tee_client_api.h` at a9e91b2 (https://github.com/OP-TEE/optee_client/blob/a9e91b23e7de63897ec306e907107ccc28287764/libteec/include/tee_client_api.h#L455-L577) declares OpenSession, CloseSession (void), InvokeCommand, ReleaseSharedMemory (void) and RequestCancellation (void).
  - But the 3.20 export table for this module (https://github.com/DNNDHH/PS5-3.20_Libs/blob/b2a77c09cd71c01f34c790cfbe98c751a13262ad/libSceTEEClient.c) has no TEEC_InitializeContext, TEEC_FinalizeContext, TEEC_RegisterSharedMemory or TEEC_AllocateSharedMemory. A caller could not have obtained a GP context or shared memory from it, so the PS5 calls are not plain GP. They may take a context made by `TEECI_InitASD`.
  - The trusted applications behind the sessions are private, the tree defines no TEE error code, and the answer a game process gets is unmeasured.
- **D (2): AMD extensions** `TEECI_InitASD` (`:25`) and `TEECI_SetTimeout` (`:30`): no public prototype.
- **D (3): AMD debug calls** `AMDTEE_DLM_FetchDebugStrings` (`:10`), `GetDebugToken` (`:15`) and `StopTADebug` (`:20`): no public prototype; a debug-only interface.

## Batch proposal

None.

## Caveats

- The tree declares every row as returning `int`. Three GP calls return `void` in the GP API. If the PS5 follows GP there, the declarations are wrong as well as unimplemented.
- Claim: #8747 (closed) declared all ten.
