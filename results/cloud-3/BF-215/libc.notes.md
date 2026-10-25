# libc (11 todo: B-S 1, B-M 2, D 8)

**What it is:** the PS5 C library and the Dinkumware C++ runtime parts linked into it: stdio, time, threads, locale facets, exceptions and unwinding. Of the 11 todo rows, 7 are partial (one branch throws) and 4 are whole functions.

**Existing state:**
- 1337 done. The files these rows touch, all under `core/libs/prx/libc/`:
  - `src/exception/Unwind.cpp`: the libunwind-style cursor table, with `CursorRegister` at `:38` and `CursorSlots` at `:77-93`;
  - `src/LocaleSupport.cpp` with `include/GuestLocale.hpp`: the facets; `IosBase` is at `GuestLocale.hpp:95`, and the num_put and time_put vtables at `LocaleSupport.cpp:292-293`;
  - `src/Threads.cpp`: `_Thrd_*`;
  - `Export.cpp`: `_Atomic_assert`;
  - `src/Process.cpp`: `daemon`;
  - `src/Formatting.cpp`: the scanf family, with the in-process wide scanner `ScanGuest` at `:128` and its wide branch at `:185-234`;
  - `src/ScanWideField.cpp` and `include/ScanWideField.hpp:9-11`: UTF-8 field helpers;
  - `src/ScanningWide.cpp`: the wide scanner over `fgetwc`, `WideInput` at `:133`;
  - `src/Time.cpp`: `strftime`;
  - `src/StdioExtras.cpp`: `_Assert` at `:78`.
- Debt `docs/dev/debt/libc.md` covers eight of the eleven rows: `:130` (unwind cursor), `:141` and `:164` (num_put and time_put), `:144` (wide scanf), `:150` (strftime), `:154` (daemon), `:166` (`_Thrd_id`). `_Atomic_assert` has no debt entry, only the code comment at `Export.cpp:20-21`.
- Tests, all registered in `core/libs/prx/libc/CMakeLists.txt` as `add_test_executable` / `add_test`:

  | Test | Source file | Lines |
  | --- | --- | --- |
  | `exception_runtime_tests` | `ExceptionRuntime.cpp` | `:947` / `:950` |
  | `guest_locale_tests` | `GuestLocale.cpp` | `:1086` / `:1093` |
  | `guest_libc_threads_tests` | `GuestLibcThreads.cpp` | `:499` / `:503` |
  | `guest_daemon_tests` | `GuestDaemon.cpp` | `:650` / `:654` |
  | `guest_stdio_variadic_tests` | `GuestStdioVariadic.cpp` | `:440` / `:444` |
  | `file_streams_tests` | `FileStreams.cpp` | `:748` / `:752` |
  | `guest_scan_count_tests` | `GuestScanCount.cpp` | `:275` / `:279` |
  | `guest_wide_stream_scan_tests` | `GuestWideStreamScan.cpp` | `:431` / `:435` |
  | `guest_strftime_tests` | `GuestStrftime.cpp` | `:698` / `:702` |

  `ExceptionRuntime.cpp` checks the cursor slots at `:282-296`.

**Counts:** A 0, B-S 1, B-M 2, B-L 0, C-real 0, C-silent 0, D 8.

## Families

- **B-M (2), wide conversions in a stream scan on Linux (partial):** `fscanf` and `vfscanf`.
  - What throws: `%ls`, `%lc`, `%l[`, `%S` and `%C` in a stream scan on Linux (`Formatting.cpp:83`, reached from `:380` and `:660`). Windows already passes them to its 16-bit `wchar_t` host.
  - Semantics: C17 7.21.6.2 p11-12, plus the rule the tree already applies to `sscanf` on Linux: UTF-8 input, one 16-bit unit per character, widths count characters (debt `:144`, `Formatting.cpp:185-234`).
  - Missing piece: a scanner driven by a stream, which reads one character at a time with pushback, as the wide family does over `fgetwc` (`ScanningWide.cpp:133`).
- **B-S (1), `std::_Atomic_assert`:**
  - The Itanium C++ ABI mangling (section 5.1) gives `(const char*, const char*)`; the declaration has no parameters (`Export.cpp:22`, `include/Api.hpp:107`).
  - Report and abort as the tree's Dinkumware `_Assert(message, location)` does (`src/StdioExtras.cpp:78`).
- **D (8):**
  - **Unwind cursor (3):**
    - `CursorGetRegister` and `CursorSetRegister` (partial): only register numbers outside -2 to 16 throw (`Unwind.cpp:51`, `:57`). libunwind callers check `validReg` first (`:45`).
    - `CursorUnsupported`: fills the other ten slots (`:78-92`).
    - The table copies LLVM libunwind's `AbstractUnwindCursor`, not the console's (debt `:130`), and the tree pins no libunwind source for those answers or prototypes.
  - **num_put and time_put `do_put` (2):** the virtuals write through a by-value `ostreambuf_iterator` into a guest `basic_streambuf`.
    - The tree models `ios_base` (`GuestLocale.hpp:95`), but not the streambuf, the virtual order or the console's `do_put` bodies (debt `:141`, `:164`).
    - The C++ standard gives the semantics, not this ABI.
  - **`_Thrd_id`:** the console's return type and width are unknown (debt `:166`). The C++ `std::__sce_v2::_Thrd_id` (`Threads.cpp:663`) returns `_Thrd_current`, but nothing says the C export is the same.
  - **`daemon`:** FreeBSD 11 `daemon(3)` forks. Child processes are not modelled (debt `:154`; `fork` throws at `core/libs/prx/libkernel_sys/Export.cpp:321`, `docs/dev/debt/libkernel_sys.md:27`).
  - **`libc_strftime` (partial):** only `%G`, `%g` and `%V` throw, for a negative `tm_yday`, a `tm_yday` past the year or a `tm_wday` below -6 (`Time.cpp:300-302`). The console was measured there and its answers fit no rule (debt `:150`).

## Batch proposals

1. **"libc: 16-bit wide conversions in fscanf and vfscanf on Linux" (2):** `fscanf_nid_postfix` and `vfscanf_nid_postfix`.
   - Files:
     - `core/libs/prx/libc/src/Formatting.cpp`: when `HasGuestWideScan` (`:57`) is true on Linux, replace `RejectGuestWideScan` with a stream path. Narrow directives go to the host `vfscanf` one segment at a time, with `%n` to count consumption. Wide directives read UTF-8 characters with `getc` and `ungetc`, check them with `InWideScanset`, and store 16-bit units.
     - `core/libs/prx/libc/src/ScanWideField.cpp`: a stream reader for one UTF-8 character, next to `ReadWideScanCharacter` (`include/ScanWideField.hpp:9`).
     - `docs/dev/debt/libc.md:144`: drop "wide conversions in fscanf throw" and keep the rest.
   - Semantics:
     - C17 7.21.6.2 p11-12, and p9 for input-item and failure rules;
     - the tree's UTF-8 rule (debt `:144`);
     - FreeBSD's per-character storing, which debt `:144` already cites for `%lc`.
   - Keep throwing, as the in-process path does: non-UTF-8 input, characters outside the BMP, scanset members outside ASCII, and positional arguments next to a wide conversion (`RequireNoPositionalWideScan`, `include/ScanWideField.hpp:11`).
   - Tests, extending `guest_stdio_variadic_tests` (`core/libs/tests/GuestStdioVariadic.cpp`; `CMakeLists.txt:440`, `:444`) and `file_streams_tests` (`core/libs/tests/FileStreams.cpp`; `:748`, `:752`):
     - `"%d %ls %d"` over `42 héllo 7`;
     - `%3lc` stores three units without a terminator;
     - `%l[a-z]` stops at the first other character, and the next `fgetc` sees it;
     - `%S` and `%C` behave like `%ls` and `%lc`;
     - a wide field at end of file returns EOF when nothing was converted;
     - the stream position after a failed field matches the `sscanf` result over the same text.
   - Smaller than 10. Merge it with batch 2 and any other libc stdio rows.
2. **"libc: std::_Atomic_assert" (1):** `_ZSt14_Atomic_assertPKcS0__nid_postfix`.
   - Files: `core/libs/prx/libc/Export.cpp:20-25` and `include/Api.hpp:107`. Give it the mangled prototype, then report and abort through the same path as `_Assert` (`src/StdioExtras.cpp:78`). Remove the code comment (CONVENTIONS: no comments).
   - Semantics: Itanium C++ ABI section 5.1 for the prototype; the in-tree `_Assert` for the behaviour.
   - Test: a death test in `exception_runtime_tests` that uses the fork pattern of `CheckTerminate` (`core/libs/tests/ExceptionRuntime.cpp:142`; `CMakeLists.txt:947`, `:950`) and expects SIGABRT.
   - One function. Merge with batch 1.

## Dependency notes

- No new subsystem for the B rows.
- The D rows would each need one of:
  - a pinned libunwind source (cursor);
  - a guest `basic_streambuf` and `ostreambuf_iterator` model plus the PS5 `do_put` slot order (num_put and time_put), which would be a new subsystem;
  - a console measurement (`_Thrd_id`, `strftime`);
  - child-process support (`daemon`).
