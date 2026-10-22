# libSceNpTus (104 todo: C 95, D 9)

What it is: Title User Storage (TUS: per-user variables and data slots on PSN) and Title Small Storage (TSS). Each call comes in several forms: plain (NpId target), `A` (account id), `VUser` (virtual-user online id) and `ForCrossSave`, each with an `Async` twin.

Offline model in the tree (debt Functional): title contexts and requests are tracked locally (`Export.cpp:21-78`). Every implemented call whose arguments pass the checks finishes its request with `SCE_NP_ERROR_SIGNED_OUT` 0x80550006:
- through `StartOffline` (`:54`), the `*Async` call returns 0 and poll/wait report SIGNED_OUT;
- through `Complete` (`:72`), the blocking call returns SIGNED_OUT.

Community errors in the tree: INVALID_ARGUMENT 0x80550704, TOO_MANY_OBJECTS 0x80550706, ABORTED 0x80550707, INSUFFICIENT_ARGUMENT 0x8055070C, INVALID_ID 0x8055070E, TOO_MANY_SLOTID 0x80550718.

Classes:
- C (95), real behaviour: every remaining TUS variable/data/status call and `sceNpTssGetDataAsync` reaches the same signed-out result through StartOffline/Complete.
  - Only the request id, always the first argument, is needed for that result.
  - Full argument lists and null/size checks come from shadPS4 `np_tus` and RPCS3's PS3 `sceNpTus`; they stay unmeasured, as the debt says for the multi-slot calls. Outputs are never written.
  - `sceNpTusSetThreadParam` returns 0: no worker thread exists offline.
- D (9):
  - TSS GetSmallStorage/GetStorage and their Async forms (4): PS5-only, no public signature.
  - TssCreateNpTitleCtx/A (2): signature unknown, and whether TSS contexts feed `sceNpTusCreateRequest` is unknown.
  - TusCreateTitleCtx (legacy form unsettled), ChangeModeForOtherSaveDataOwners (semantics unknown), SetTimeout (PS3 2-argument vs NP 6-argument form).

Third-party: none.

Batch proposals: each adds cases to `guest_np_tus_tests` (`core/libs/tests/GuestNpTus.cpp`, `libSceNpTus/CMakeLists.txt:6`). Each case checks: Async returns 0 then poll/wait report 0x80550006; the blocking form returns 0x80550006; a deleted request gives INVALID_ID 0x8055070E; an aborted one gives ABORTED; the shadPS4 null checks hold.
1. "tus: offline GetMultiUserVariable, GetFriendsVariable and AddAndGetVariable" (29).
2. "tus: offline TryAndSetVariable, DeleteMultiSlotData, SetData, GetData and TssGetDataAsync" (35).
3. "tus: offline data-status calls" (31): GetMultiSlotDataStatus, GetMultiUserDataStatus and GetFriendsDataStatus families, plus SetThreadParam.
