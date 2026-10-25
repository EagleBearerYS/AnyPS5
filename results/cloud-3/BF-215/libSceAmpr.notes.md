# libSceAmpr (3 todo: B-M 2, D 1)

**What it is:** the AMPR/APR command-buffer library (AMM page mapping, APR file reads, counters and completion writes), which also re-exports pieces of the Dinkumware C/C++ runtime (threads, locale, Annex K strings).

**Existing state:**
- 131 done, in `core/libs/prx/libSceAmpr/Export.cpp` (declarations in `include/Api.hpp`).
- Command buffers use the emulator's own encoding (debt `docs/dev/debt/libSceAmpr.md:8`). The AMM page-table pool is not modelled: `sceAmprAmmGetUsageStatsData` (`Export.cpp:926-934`) reports `AmmPageTablePoolEntries` 512 (`:232`), none allocated and ring idle flags 0x7, constants from KytyPS5 (debt `:15`).
- Runtime re-exports forward to libc: `_Cnd_init_with_name` calls `_Cnd_init_nid_postfix` (`:838-840`) and `sceAmprLocaleconv` calls `SonyLocaleconv` (`:861-864`). The shared_ptr spin lock is `sharedPtrSpinLock` (`:229`).
- Error constants: the kernel's `SCE_KERNEL_ERROR_EINVAL`, `EPERM`, `ESRCH` and `EBUSY`.
- libc's locale support has the facet pattern the two time_get rows need: `time_put<char>::_Getcat` (`core/libs/prx/libc/src/LocaleSupport.cpp:505-513`, category 5, measured size 0xb8 at `:295`) with a vtable of throwing methods (`:288-290`, `:293`). libc has only `time_get<char>::id` (`:381`). Debt `docs/dev/debt/libc.md:169` notes that Ampr's qualified imports of these symbols fall back to libc.
- Tests:
  - `guest_ampr_tests` (`core/libs/tests/GuestAmpr.cpp`), registered at `libSceAmpr/CMakeLists.txt:7` and `:11`;
  - `guest_amm_placement_tests` at `:17` and `:21`;
  - `guest_ampr_threads_tests` at `:27` and `:31`;
  - `guest_ampr_cpp_error_tests` at `:39` and `:45`;
  - libc's `guest_locale_tests` (`core/libs/tests/GuestLocale.cpp`, which checks `time_put::_Getcat` at `:323`), registered at `core/libs/prx/libc/CMakeLists.txt:1086` and `:1093`.

**Counts:** A 0, B-S 0, B-M 2, B-L 0, C-real 0, C-silent 0, D 1.

## Families

- **B-M (2): `std::time_get<char, istreambuf_iterator<char>>` in the C locale.** Both move to libc (`LocaleSupport.cpp`) and leave Ampr.
  - A same-name forwarder in Ampr would fail the `duplicate-export` check (`tools/check_conventions.py:23`). `_Cnd_init_with_name` can forward only because its libc target has another name.
  - Ampr-qualified imports fall back to libc, as they already do for `time_get<char>::id` (`docs/dev/debt/libc.md:169`).
  - Spot-check correction (cloud-3): the pass first proposed Ampr forwarders.
  - `time_get::_Getcat`: a new `time_get<char>` facet and vtable, built like `time_put::_Getcat` (`LocaleSupport.cpp:505-513`), returning category 5.
  - `time_get::get(first, last, ios_base&, iostate&, tm*, fmt, fmtend)`: the format loop of ISO/IEC 14882:2011 22.4.5.1.1 [locale.time.get.members]. It skips white space, matches literals and calls `do_get` for each conversion. `do_get` and the other virtuals (22.4.5.1.2) parse the C-locale conversions of POSIX.1-2017 `strptime()`. They read the guest stream through Dinkumware's `istreambuf_iterator`/`basic_streambuf`, whose layouts come from crt.vc12 `f1d47126`, which the tree already cites (`docs/dev/debt/libc.md:157`, `:166`).
- **D (1):** `sceAmprAmmSetPageTablePoolOccupancyNotificationThreshold` is a parameterless declaration (`include/Api.hpp:132`) with no prototype in the tree. The tree models neither pool occupancy nor its notification. Once a prototype is known it becomes a C-silent candidate: the occupancy reported by `GetUsageStatsData` never changes, so the threshold could never fire.

## Batch proposals

1. **"libc: time_get<char> facet for the C locale" (2):** `time_get<char>::_Getcat` and `time_get<char>::get` (7-argument form), moved from Ampr into libc.
   - Files: `core/libs/prx/libc/src/LocaleSupport.cpp`, `core/libs/prx/libc/include/GuestLocale.hpp` (time_get vtable/facet), `core/libs/prx/libc/include/Api.hpp`, `core/libs/prx/libSceAmpr/Export.cpp` and `include/Api.hpp` (delete the two stubs at `Export.cpp:842` and `:847`), and `docs/dev/debt/libc.md` (extend `:169`).
   - Semantics:
     - ISO/IEC 14882:2011 22.4.5.1 [locale.time.get], 22.4.5.1.1 and 22.4.5.1.2;
     - POSIX.1-2017 `strptime()` for `%a %A %b %B %h %d %e %H %I %j %m %M %n %t %p %S %y %Y %D %T %R %%`;
     - Dinkumware vtable order and iterator/streambuf layout from crt.vc12 `f1d47126` (cited at `docs/dev/debt/libc.md:166`).
   - Tests:
     - extend `guest_locale_tests` (`GuestLocale.cpp`, next to the `time_put` check at `:323`): `_Getcat` returns 5 and installs a facet once;
     - `get` with `"%Y-%m-%d"` over a guest stream holding `2024-02-29` fills `tm_year` 124, `tm_mon` 1 and `tm_mday` 29 and sets `eofbit`;
     - a literal mismatch sets `failbit`;
     - `"%H:%M"` stops at the first non-matching character;
     - none in `guest_ampr_tests`: Ampr no longer exports the two names.
   - Throw for unmeasured cases: non-C locale names, `%E`/`%O` modifiers and `%Z`.
   - The batch is small and should merge with libc's own locale-facet rows (the throwing `time_put`/`num_put` virtuals, debt `libc.md:141`).

## Dependency notes

- No new third-party code. The time_get work is new state inside libc's guest-locale layer: a facet type, a vtable and a guest streambuf reader. Nothing in the tree reads a guest `basic_streambuf` yet.

## Caveats

- The console's `time_get<char>` facet size is unmeasured; `time_put`'s 0xb8 was read from a bundled `libc.prx` (`docs/dev/debt/libc.md:141`). The `_Getcat` row needs that size measured the same way, or must allocate the crt.vc12 layout and record the gap in the debt. If a reviewer requires the console size first, the row moves to D.
