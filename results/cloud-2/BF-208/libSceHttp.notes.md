# libSceHttp (30 todo: B-S 6, B-M 2, C 12, D 10)

**What it is:** the HTTP/HTTPS client: contexts, templates, connections and requests; options, epoll waits, URI helpers and header parsing; cookies, auth cache and diagnostics.

**Existing state:**
- 85 done.
- No network is emulated (`Export.cpp:25`). Objects can be created, but every send, read or status call answers `ERROR_NETWORK` 0x80431063 (`:262-299`).
- Context lifecycle: `g_contexts` and `g_contextPoolSizes` (`:112-113`), with `BEFORE_INIT` 0x80431001 and `INVALID_ID` 0x80431100 checks as in `sceHttpCookieFlush` (`:153`).
- Per-object options: `HttpOptions` (`src/Options.hpp:7`), kept in `g_objectOptions` (`src/Options.cpp:43`) and inherited from the parent at creation.
- Epoll objects (`:104`, `:114`).
- URI parsing, building and merging are measured on a PS5 (`src/Uri.cpp`, debt `:15-19`).
- Error constants: `src/HttpErrors.hpp`.
- There is no cookie store: the flushes succeed on empty caches (debt `:40`).
- Tests: `guest_http_tests`, `guest_http_response_header_tests` and `guest_http_options_tests` (`core/libs/tests/GuestHttp*.cpp`), registered in `libSceHttp/CMakeLists.txt` at `:10`, `:15` and `:20`.

**Counts:** A 0, B-S 6, B-M 2, B-L 0, C 12 (all silent stubs), D 10.

## Families

- **B-M (2) and B-S (5): offline cookie store.** No network is needed: a title can add a cookie and read it back.
  - `sceHttpAddCookie` and `sceHttpGetCookie` need a new per-context store: parse the cookie (RFC 6265 section 5.2), store it (section 5.3), and build the cookie-string (section 5.4). URLs go through the existing `sceHttpUriParse` (`src/Uri.cpp:333`).
  - `sceHttpSetCookieMaxNum`, `MaxNumPerDomain`, `MaxSize` and `TotalMaxSize` set that store's limits, with eviction per section 5.3 step 12.
  - `sceHttpGetCookieStats` fills the declared `HttpCookieStats` (`include/Http.hpp:8`).
  - `sceHttpCookieFlush` and `sceHttpTerm` must then clear the store.
- **B-S (1):** `sceHttpGetRegisteredCtxIds` returns the `memid` and `ssl_ctx_id` given to `sceHttpInit` (`:278`), kept per context. The basis is in-tree only; no public source names the outputs.
- **C (12), all silent stubs; each needs a title:**
  - setters that change no request:
    - `AddQuery` and `AddRequestHeaderRaw`: return 0 and keep nothing, as `sceHttpAddRequestHeader` (`:138`);
    - `SetDelayBuildRequestEnabled`, `SetHttp09Enabled`, `SetPolicyOption` and `SetPriorityOption`: 0 for a live id, as `sceHttpCacheRedirectedConnectionEnabled` (`src/Options.cpp:161`);
  - callbacks that are never called: `SetCookieSendCallback` and `SetSocketCreationCallback`, as `sceHttpSetCookieRecvCallback` (`:601`);
  - print-only diagnostics: `DbgShowStat`, `DbgShowConnectionStat`, `DbgShowRequestStat` and `DbgShowMemoryPoolStat`, as `sceHttpDbgSetPrintf` (`:666`). The native calls take no arguments (debt `:28-31`).
- **D (10):**
  - Auth-cache and cookie Export/Import (4): Sony's blob format is not public.
  - `GetConnectionStat`, `DbgGetConnectionStat` and `DbgGetRequestStat` (3): 4- and 3-word records with incomplete fields (debt `:12`).
  - `Set/GetEpollId` (2): what the 32-bit epoll id names is unknown, and the getter dispatch is not recoverable (debt `:12`).
  - `RequestGetAllHeaders`: the output conventions are unverified, and no request headers are kept.

## Batch proposals

1. **"http: offline cookie store" (7):** AddCookie, GetCookie, GetCookieStats and the four SetCookieMax* calls. Touch `sceHttpCookieFlush` and `sceHttpTerm`.
   - Semantics: RFC 6265 sections 5.1.3 (domain match), 5.1.4 (path match), 5.2, 5.3, 5.4 and 6.1, plus the existing context checks.
   - Tests: extend `guest_http_options_tests` (`core/libs/tests/GuestHttpOptions.cpp`, `libSceHttp/CMakeLists.txt:16`, `:20`):
     - add `a=1; Path=/p` for `http://h/p/x`;
     - GetCookie returns `a=1` for `http://h/p/y` and nothing for `http://h/q`;
     - a `Secure` cookie is absent over http and present over https;
     - with MaxNumPerDomain 1, a second cookie evicts the first, and GetCookieStats counts the eviction;
     - CookieFlush empties the store;
     - BEFORE_INIT with no context, INVALID_ID for a terminated one.
   - Throw where the console answer is unmeasured: GetCookie's size/terminator convention, bad-URL errors, limit defaults.
2. **"http: offline setters, callbacks and diagnostics" (12, the C rows):** only when a title needs them.
   - Tests in `guest_http_options_tests`:
     - each setter returns 0 for a live template, connection and request;
     - the `Options.cpp:161`-style ones return INVALID_ID for a deleted id and BEFORE_INIT with no context;
     - the Dbg calls return 0.

## Caveats

- Debt `:14` says enabling cookies throws, but `sceHttpSetCookieEnabled` (`Export.cpp:314`) stores the flag and returns 0, so the debt line is stale. The cookie batch should make the flag gate the store.
- Unmeasured details of the cookie store:
  - console default limits (RFC 6265 section 6.1 gives only minimums);
  - whether `GetCookieStats`' `id` must be a context;
  - `usedBytes`, which cannot match the console's pool accounting.
- The C silent stubs accept any value: policy and priority meanings are unknown.
