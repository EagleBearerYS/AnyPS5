# libSceNpWebApi (15 todo: B-M 15)

**What it is:** the PS4-era PSN Web API client. Library contexts, push-event handles and filters are done. User contexts, requests, push-event callbacks and the NpId parser are still to do.

**Existing state:**
- 8 done, all in `core/libs/prx/libSceNpWebApi/Export.cpp`:
  - `sceNpWebApiInitialize` (`:131-140`) and `sceNpWebApiTerminate` (`:162-165`);
  - `CreateHandle` and `DeleteHandle` (`:51-58`, `:90-95`);
  - `Create/DeletePushEventFilter` (`:60-68`, `:97-102`);
  - `Create/DeleteServicePushEventFilter` (`:75-83`, `:109-114`).
- State:
  - `LibraryContext` (`:20-25`) holds the libHttp context id, the pool size, and the handle and filter sets.
  - Contexts live in `contexts` under `contextsMutex` (`:27-28`). Library ids run from 1 to 0x7fff (`:133-134`). Handles and filters come from `nextObject`, below 0xf0000000 (`:32-35`).
- Errors (`:10-16`):
  - `INVALID_ARGUMENT` 0x80552902, `LIB_CONTEXT_NOT_FOUND` 0x80552904 and `LIB_CONTEXT_MAX` 0x8055291A;
  - `PUSH_EVENT_FILTER_NOT_FOUND` 0x8055290B, `HANDLE_NOT_FOUND` 0x8055290D and `SERVICE_PUSH_EVENT_FILTER_NOT_FOUND` 0x8055290E;
  - `SIGNED_IN_USER_NOT_FOUND` 0x80552910.
- Offline model:
  - No PSN user is signed in, so a service filter answers `SIGNED_IN_USER_NOT_FOUND` (`:82`, debt `docs/dev/debt/libSceNpWebApi.md:6`).
  - Elsewhere in the tree, offline Web API requests answer `NOT_SIGNED_IN` 0x80552907 (`core/libs/prx/libSceNpCppWebApi/Export.cpp:10`, debt `docs/dev/debt/libSceNpCppWebApi.md:13`).
- Semantics source: shadPS4's np_web_api module, linked at `main` (debt `:5`).
- Test: `guest_np_web_api_tests` (`core/libs/tests/GuestNpWebApi.cpp`), registered at `core/libs/prx/libSceNpWebApi/CMakeLists.txt:6` and `:10`.
- All 15 todo rows are parameterless declarations (`core/libs/prx/libSceNpWebApi/include/Api.hpp:7-29`).

**Counts:** A 0, B-S 0, B-M 15, B-L 0, C-real 0, C-silent 0, D 0.

## Families

- **B-M (14): user contexts, requests and push-event callbacks.**
  - **The new state:** under each library context, a user-context table with ids `(libCtxId<<16)|n`. Each user context holds requests, with ids `(userCtxId<<32)|n`, and two callback tables.
  - **Prototypes:** Orbital `db_types.json@b321fcdb` (cited at `docs/dev/debt/libSceNpFriendListDialog.md:5`) gives:
    - L1666 AbortRequest, L1676 CreateRequest, L1677 DeleteContext, L1680 DeleteRequest;
    - L1682-L1683 the header value and its length, L1686 ReadData.

    The other six (CreateContext, GetHttpStatusCode, SendRequest and the four callback calls) come only from shadPS4's `np_web_api.cpp` wrapper.
  - **Behaviour:** shadPS4 `np_web_api_internal.cpp` at `main`, as linked:
    - user contexts: createUserContext L255-L303, deleteUserContext L379-L436;
    - requests: createRequest L438-L514, sendRequest L670-L965, abortRequest L967-L1009, deleteRequest L1016-L1069;
    - callbacks: push L1300-L1368, service L1461-L1544;
    - responses: header value L1852-L1895, status L1897-L1931, readData L1976-L2065.
  - **Errors:** shadPS4 `np_error.h@a808e76` (cited at `docs/dev/debt/libSceNpParty.md:5`), L450-L480:
    - `INVALID_LIB_CONTEXT_ID` 0x80552903, `USER_CONTEXT_NOT_FOUND` 0x80552905, `REQUEST_NOT_FOUND` 0x80552906;
    - `NOT_SIGNED_IN` 0x80552907, `INVALID_CONTENT_PARAMETER` 0x80552908, `ABORTED` 0x80552909;
    - `USER_CONTEXT_ALREADY_EXIST` 0x8055290A and `USER_CONTEXT_MAX` 0x8055291B;
    - `PUSH_EVENT_CALLBACK_NOT_FOUND` 0x8055290C and `SERVICE_PUSH_EVENT_CALLBACK_NOT_FOUND` 0x8055290F.
  - **Offline answers:**
    - `SendRequest`: after the id checks, `ABORTED` for an aborted request, else `NOT_SIGNED_IN`. shadPS4 does this (L771-L777), and so does the tree's CppWebApi.
    - `GetHttpStatusCode` and `GetHttpResponseHeaderValue(Length)`: after the argument and id checks, shadPS4 returns libHttp's answer for the request's HTTP id (L1872-L1877, L1917-L1928). In this tree libHttp answers `ERROR_NETWORK` 0x80431063 for every id (`core/libs/prx/libSceHttp/Export.cpp:258-263`, `:272-276`; `core/libs/prx/libSceHttp/src/HttpErrors.hpp:6`), so these return 0x80431063.
    - `ReadData`: after the checks, `ABORTED` for an aborted request; otherwise shadPS4 reads through `sceHttpReadData` and counts a negative answer as 0 bytes (L2015-L2023). The tree's `sceHttpReadData` answers `ERROR_NETWORK` (`core/libs/prx/libSceHttp/Export.cpp:452-457`), so the result is 0. The timeout branch (L2034) needs a request timeout the tree does not keep, so it throws.
      - Spot-check correction (cloud-3): the pass first answered `NOT_SIGNED_IN` here, after the tree's CppWebApi (`core/libs/prx/libSceNpCppWebApi/Export.cpp:427`, `:708`, `:759-761`). shadPS4 does not do that for these four, and the tsv follows shadPS4.
    - `RegisterServicePushEventCallback`: from SDK 2.50, every filter id gets `SERVICE_PUSH_EVENT_FILTER_NOT_FOUND`, since no service filter can be created.
    - Callbacks are kept only so that Unregister can find them; nothing calls them offline.
- **B-M (1): `sceNpWebApiUtilityParseNpId`, a parser.**
  - Prototype: Orbital L1696, `(const char* jsonNpId, SceNpId*)`.
  - shadPS4's wrapper:
    1. base64-decodes the string;
    2. splits it as `<online id>@<segments>`;
    3. drops '/' and '.' and puts the rest into `opt[8]`;
    4. sets `reserved[0]` to 1.
  - It needs the 36-byte NpId.

## Batch proposals

1. **"feat(libSceNpWebApi): offline user contexts, requests and push-event callbacks" (15, all rows).**
   - **Functions:** AbortRequest, CreateContext, CreateRequest, DeleteContext, DeleteRequest, GetHttpResponseHeaderValue, GetHttpResponseHeaderValueLength, GetHttpStatusCode, ReadData, RegisterPushEventCallback, RegisterServicePushEventCallback, SendRequest, UnregisterPushEventCallback, UnregisterServicePushEventCallback and UtilityParseNpId.
   - **Files:**
     - `core/libs/prx/libSceNpWebApi/Export.cpp`;
     - `core/libs/prx/libSceNpWebApi/include/Api.hpp`, for the real prototypes;
     - `docs/dev/debt/libSceNpWebApi.md`: pin the shadPS4 commit, add the Orbital link, and fix the SDK-version sentence in `:6`;
     - `core/libs/tests/GuestNpWebApi.cpp`;
     - a 36-byte NpId, either in `core/libs/SceTypes.hpp` (`:1505` declares 24 bytes) or as a local struct.
   - **Semantics:**
     - the shadPS4 module at a pinned commit (today's `main` `np_web_api_internal.cpp` gives the line ranges above);
     - Orbital L1666-L1696;
     - `np_error.h@a808e76` L450-L480;
     - the tree's `NOT_SIGNED_IN` offline model for `SendRequest`, and libSceHttp's `ERROR_NETWORK` for the response readers.
   - **SDK gates:**
     - shadPS4 branches on the compiled SDK version (1.00, 2.50 and 4.00).
     - The tree can read that version (`core/libs/prx/libkernel_sys/Export.cpp:314-318`), as `core/libs/prx/libSceUsbStorageDialog/Export.cpp:36-38` does.
     - Map PS5 SDK numbers onto these gates, or throw where a branch is unclear.
   - **Tests:** extend `guest_np_web_api_tests` (`core/libs/prx/libSceNpWebApi/CMakeLists.txt:6`, `:10`):
     - `CreateContext`:
       - 0x8000 gives 0x80552903;
       - a null online id gives 0x80552902;
       - an unknown library context gives 0x80552904;
       - a live context returns an id whose high half is that context;
       - a second call gives 0x8055290A.
     - `CreateRequest`:
       - a null API group or path gives 0x80552902;
       - a content length without a type gives 0x80552908;
       - an unknown user context gives 0x80552905;
       - a live user context writes an id.
     - `SendRequest` gives 0x80552907, and 0x80552909 after `AbortRequest`.
     - `ReadData`, status and header calls:
       - their argument errors;
       - 0x80552906 for an unknown request;
       - for a live one, 0x80431063 from the status and header calls and 0 from `ReadData` (0x80552909 after `AbortRequest`).
     - Deletion and termination:
       - `DeleteRequest` twice gives 0, then 0x80552906;
       - `DeleteContext` twice gives 0, then 0x80552905;
       - `Terminate` drops both.
     - Push callbacks:
       - registering on a live filter returns an id;
       - unregistering it twice gives 0, then 0x8055290C;
       - service registration gives 0x8055290E;
       - unregistering an unknown service callback gives 0x8055290F.
     - `ParseNpId`:
       - null input gives 0x80550003;
       - `YWJjQGI3LnVz` (base64 of `abc@b7.us`) gives handle `abc`, opt `b7us` and `reserved[0]` 1.
   - **Size:** 15 rows, one PR. No merge needed.

## Dependency notes

None. Base64 decoding is a few lines, and no third-party code is needed.

## Caveats

- **CreateContext model (unsure).**
  - shadPS4 binds the new context to the initial user and never reads the online id.
  - The tree elsewhere says no user is signed in (debt `:6`), and `sceNpGetOnlineId` answers `SIGNED_OUT` (`core/libs/prx/libSceNpManager/Export.cpp:815-819`). So no title can hold a valid online id.
  - **Alternative:** `CreateContext` answers `SIGNED_IN_USER_NOT_FOUND` after its argument and context checks (C-real, from `:82`).
    - Then no user context can ever exist.
    - The other 13 table rows shrink to B-S id checks on the existing `contexts` map.
    - `ParseNpId` stays B-M.
  - Decide before the batch. The tsv follows shadPS4.
- **The live-request answer of the four response readers is not measured.**
  - The tsv follows shadPS4, which forwards to libSceHttp; that answers `ERROR_NETWORK` here (0 bytes for `ReadData`).
  - The tree's CppWebApi answers `NOT_SIGNED_IN` for its own readers instead. If a measurement settles the console's answer, both libraries should agree.
- **shadPS4 is linked at `main`, not at a commit** (debt `:5`). This census read today's `main` `np_web_api_internal.cpp`; the batch should pin it. `ParseNpId` and the argument checks are in `np_web_api.cpp`, beside the linked file.
- **Debt `:6` is stale.** It says AnyPS5 has no compiled-SDK version for libraries, but the accessor above exists.
- **NpId size.** The tree's `NpId` (`core/libs/SceTypes.hpp:1505`) is 24 bytes. shadPS4 and the NpScore tests (`core/libs/tests/GuestNpScoreOffline.cpp:141`) use 36.
- **Title:** Limbo (CUSA01664) creates a context, a handle and filters when its game starts (debt `:5`).
