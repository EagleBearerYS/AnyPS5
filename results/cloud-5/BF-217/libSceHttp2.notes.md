# libSceHttp2 (26 todo: A 0, B-S 4, B-M 2, B-L 0, C-real 0, C-silent 6, D 14)

**What it is:** the HTTP/2-capable HTTP client: contexts, templates, requests, cookie boxes, options, async send and wait, and WebSocket requests.

**Existing state** (`core/libs/prx/libSceHttp2/Export.cpp`, built by `libSceHttp2/CMakeLists.txt:1`):
- 36 done.
- No network is emulated (`:17-19`). Requests can be created, but sends and response getters answer `ERROR_NETWORK` 0x80436063.
- Handles:
  - one table for every object kind: `Http2Handle` (`:25-30`), `g_handles` (`:34`), `FindHandle` (`:36`), `HandleIsLive` (`:61`);
  - Term and DeleteTemplate release everything below them (`ReleaseOwnedHandles` `:46`, `:303`, `:143`).
- Error constants:
  - `ERROR_NETWORK` (`:19`);
  - `ERROR_INVALID_ID` 0x817B1100 (`:20`);
  - `ERROR_NULL_POINTER` 0x817B1225 (`:21`);
  - the cookie-pool minimum error 0x817B121D (`:349`).
- Cookie boxes:
  - real objects with a pool rounded to 16 KiB (`:348-357`), but nothing is stored in them;
  - `sceHttp2SetCookieBox` keeps no binding (`:366-370`);
  - `sceHttp2CookieFlush` returns 0 (`:343-346`, debt `:11`).
- Setters return 0 and keep nothing: SetAuthEnabled (`:214`), SetAutoRedirect (`:220`), the callbacks (`:250`, `:275`). Only the SSL setters check that the id is live (`:288-301`, debt `:5`).
- Async: SendRequestAsync queues an offline ERROR_NETWORK completion and triggers the kqueue user event, and WaitAsync pops it (`:74-99`, `:315-324`, debt `:17`).
- WebSocket:
  - six exports with recovered ABIs (`include/Http2.hpp:12-18`, `:60-65`), all of which throw (`:383-411`);
  - the ping ABI is confirmed (debt `:19`).
- The 18 `(void)` declarations (`:412-500`, `include/Http2.hpp:66-83`) are the debt's "pending exports" with unknown signatures (debt `:14`).
- Test: `guest_http2_tests` (`core/libs/tests/GuestHttp2.cpp`), built at `libSceHttp2/CMakeLists.txt:7` and registered at `:11`.

**Sources read:**
- SharpProspero `Http2.cs` at 5dfdb4b0b166 (`https://github.com/SvenGDK/SharpProspero/blob/5dfdb4b0b166e83dc9ae275001afb695f12c7174/src/SharpProspero/Interop/Net/Http2.cs`), which the tree already cites (debt `:9`).
  - It gives prototypes for 14 of the 18 `(void)` rows.
  - It has none for GetAuthEnabled, GetAutoRedirect, GetCookieBox or SetPreSendCallback.
- prosper `hle_http2.cpp` at b1bec57186a0 (`https://github.com/mattias800/prosper/blob/b1bec57186a0/prosper/src/hle/net/hle_http2.cpp`), cited at debt `:5` and `:9`. It is an HLE model that takes its shapes from libSceHttp v1 and rates its own confidence. It is not a console measurement.

## Families

- **B-M (2) and B-S (4): an offline cookie store per box.** This is the split BF-208 used for libSceHttp's cookie calls.
  - **AddCookie (B-M):**
    - parses a Set-Cookie string (RFC 6265 section 5.2) and stores it (section 5.3) under the box id;
    - needs a URL parser, which libSceHttp2 does not have (libSceHttp's is `src/Uri.cpp`, in another module).
  - **GetCookie (B-M):** builds the cookie-string for a URL (sections 5.1.3 domain match, 5.1.4 path match, 5.4), with SharpProspero's two-call size query (`Http2.cs#L226-L232`).
  - **GetCookieStats (B-S):** fills SharpProspero's record (`#L82-L101`) from the store's counters.
  - **SetCookieMaxNum, SetCookieMaxNumPerDomain and SetCookieMaxSize (B-S):** limits on the store, with eviction per section 5.3 step 12.
- **C-silent (6); each needs a title:**
  - AuthCacheFlush and RedirectCacheFlush:
    - no auth challenge or redirect ever happens offline, so they return 0 with nothing to flush;
    - this matches `sceHttp2CookieFlush` (`:343`) and libSceHttp's flushes (`libSceHttp/Export.cpp:146`, `:459`).
  - RemoveRequestHeader: AddRequestHeader keeps nothing (`:103-109`), so removal is a 0 no-op.
  - SetAuthInfoCallback, SetCookieRecvCallback and SetCookieSendCallback: callbacks that are never called, as with SetRedirectCallback and SetSslCallback (`:250`, `:275`).
- **D (14):**
  - **CookieExport and CookieImport (2):** prototypes exist (`Http2.cs#L191`, `#L199`), but Sony's serialised box format is not public.
  - **GetAuthEnabled, GetAutoRedirect and GetCookieBox (3):**
    - there is no Http2 prototype; the `(id, int*)` shape comes from libSceHttp v1 or from prosper's guesses;
    - the setters store nothing to read back;
    - the defaults disagree: authEnabled is false in `libSceHttp/src/Options.hpp:9` but 1 in prosper (`#L76`).
    - GetAutoRedirect is the closest to B-S, because both sources default it to true. That default is PS4 shadPS4 behaviour (`docs/dev/debt/libSceHttp.md:11`), not a measurement.
  - **SetPreSendCallback (1):** no prototype anywhere, and no v1 twin.
  - **SendRequestAsync and WaitAsync (2):**
    - both are done for the offline path;
    - they still throw for: an extra option (layout unknown), a kqueue whose user event is gone, a null result, and a wait with nothing completed (the timeout answer is unknown);
    - the last case also covers an invalid request id (`GuestHttp2.cpp:133`). There the library's INVALID_ID is the obvious lead, but WaitAsync's answer is unmeasured.
  - **WebSocket (6):**
    - the ABIs are recovered but the semantics are unknown;
    - CreateRequest throws, so the other five never see a WebSocket request;
    - what the ping setters answer for a plain request or an unknown id is unmeasured.

## Batch proposal

1. **"http2: offline cookie store" (6 rows: AddCookie, GetCookie, GetCookieStats, SetCookieMaxNum, SetCookieMaxNumPerDomain, SetCookieMaxSize).**
   - Semantics:
     - RFC 6265 sections 5.1.3, 5.1.4, 5.2, 5.3 (with step 12), 5.4 and 6.1;
     - prototypes and the stats record from SharpProspero `Http2.cs` at 5dfdb4b0b166.
   - Also change `sceHttp2CookieFlush` to empty the box, and `sceHttp2DeleteCookieBox` to drop the box's cookies.
   - Tests: extend `guest_http2_tests` (`core/libs/tests/GuestHttp2.cpp`, `CMakeLists.txt:7`, `:11`):
     - AddCookie `a=1; Path=/p` for `http://h/p/x` into a box;
     - GetCookie with a null buffer reports the size, then returns `a=1` for `http://h/p/y` and nothing for `http://h/q`;
     - a `Secure` cookie is absent over http and present over https;
     - with MaxNumPerDomain 1, a second cookie evicts the first, and GetCookieStats counts one removal;
     - CookieFlush on the box empties it;
     - a deleted box id gives 0x817B1100.
   - Throw where the answer is unmeasured: the GetCookie size and terminator convention, bad-URL errors, the default limits.
   - Claiming PR: 10534 (open) implements all six.
2. **"http2: offline flushes, header removal and callbacks" (6 rows, the C-silent ones).** Only when a title needs them.
   - Tests in `guest_http2_tests`:
     - each call returns 0 for a live template and a live request;
     - no callback is called after SendRequest or SendRequestAsync.
   - Record each under Silent stubs in `docs/dev/debt/libSceHttp2.md`.
   - Claiming PR: 10536 (open) implements AuthCacheFlush, RedirectCacheFlush, RemoveRequestHeader and SetAuthInfoCallback. It also implements GetAuthEnabled, GetAutoRedirect and SetPreSendCallback, which stay D here.

## Caveats

- Debt `:14` is partly stale. SharpProspero, which the same file cites at `:9`, gives prototypes for 14 of its 18 "unknown signature" exports.
- The CookieFlush argument is ambiguous:
  - the tree's test flushes cookies with a context id (`GuestHttp2.cpp:57`);
  - SharpProspero calls the argument a cookie box id (`Http2.cs#L195`).
  - A cookie store must decide what a context id flushes, and that answer is unmeasured.
- SharpProspero's prototypes are an SDK binding, not a console measurement. Nothing in this library is measured.
- Other claims:
  - PR 10167 (open) changes SendRequestAsync and WaitAsync;
  - PRs 3223 and 4447 (closed) touched the WebSocket rows;
  - PR 9963 (closed) claimed GetAuthEnabled, GetAutoRedirect and GetCookieBox.
