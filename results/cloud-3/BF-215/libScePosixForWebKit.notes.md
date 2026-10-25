# libScePosixForWebKit (13 todo: A 5, D 8)

**What it is:** the POSIX shim the console's WebKit links against. It has `--wrap` forwarders (`__wrap_<name>`), resolver calls (`getaddrinfo`, `gethostbyname`, `getservbyname`), `fnmatch`, `strcasestr`, `arc4random`, `mkstemp` and `isatty`, plus Sony `_np` extensions.

**Existing state:**
- 19 done:
  - `Export.cpp`: the six `__wrap_*` forwarders (`core/libs/prx/libScePosixForWebKit/Export.cpp:196-228`);
  - `Resolver.cpp`: `gethostbyname` (`:164`) and the guest `HostEntry` (`GuestResolver.hpp:6`);
  - `Filesystem.cpp`: `mkstemp` and `isatty`;
  - `pthread_setname_np` (`Export.cpp:287`).
- No library state. The done `__wrap_*` calls forward to libc and libkernel, which the generic build links (`core/libs/CMakeLists.txt:170`).
- The target link line is `core/libs/prx/libScePosixForWebKit/CMakeLists.txt:3`. Today it links libSceNet only.
- Tests, in `core/libs/prx/libScePosixForWebKit/CMakeLists.txt`:
  - `guest_posix_filesystem_tests` (`core/libs/tests/GuestPosixFilesystem.cpp`), at `:10` and `:14`. It already compares the done `__wrap_*` calls with their targets (`GuestPosixFilesystem.cpp:132-175`).
  - `guest_fnmatch_tests` (`:20`, `:24`), `guest_resolver_tests` (`:29`, `:33`), `guest_service_database_tests` (`:39`, `:43`) and `guest_posix_thread_name_tests` (`:48`, `:52`).

**Counts:** A 5, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 8.

## Families

- **A (5), `__wrap_*` forwarders:**
  - Debt `docs/dev/debt/libScePosixForWebKit.md:17` gives the convention: GNU ld `--wrap`, under which `__wrap_<name>` takes `<name>`'s POSIX signature and forwards.
  - The targets:
    - `fchown`, `link`, `readlink` and `symlink` are in libkernel_sys (`core/libs/prx/libkernel_sys/Export.cpp:711`, `:465`, `:352`, `:529`);
    - `statvfs` is in libSceLibcInternal (`core/libs/prx/libSceLibcInternal/PathStatistics.cpp:142`).
  - Debt `:7` names the only obstacle: no prx links those two libraries yet.
  - The targets' own gaps carry over: `fchown` throws for a non-guest owner, and `symlink` throws for absolute and `..` targets.
- **D (7), Sony extensions with unknown signatures:**
  - The rows: `clock_gettime_np`, `getenv_np`, `setenv_np`, `mmap_np`, `msetname`, `pthread_setschedparam_np` and `sysinfo`.
  - Debt `:6` says no credible prototype is known, and none is FreeBSD or POSIX.
  - How the `_np` calls relate to the wrappers (for example a separate environment that `setenv_np` fills) is unknown (debt `:17`).
- **D (1), `gethostbyname_r`:**
  - Debt `:6` says its prototype is unknown.
  - Unsure: FreeBSD 11.4's `gethostnamadr.c`, the file debt `:5` already cites for `gethostbyname`, defines `gethostbyname_r(name, he, buffer, buflen, result, h_errnop)`. If a WebKit call site confirms that signature for NID `dt8ermYp-Wc`, this becomes B-M:
    - pack the done `gethostbyname` result (`Resolver.cpp:164`) into the caller's buffer;
    - return `ERANGE` when it is too small;
    - set `*result` and `*h_errnop`.

## Batch proposals

1. **"posixforwebkit: forward the remaining --wrap exports" (5):** `__wrap_fchown`, `__wrap_link`, `__wrap_readlink`, `__wrap_statvfs` and `__wrap_symlink`.
   - This is below 10, so merge it with libSceSpSysCallWrapper's `sceSpPthreadMutexattrSetprioceiling` (1 A) into one "forwarders" task of 6.
   - Files:
     - `core/libs/prx/libScePosixForWebKit/Export.cpp:204`, `:217`, `:230`, `:235` and `:240`: give each its target's signature, with `readlink` returning `std::int64_t`, and forward;
     - `include/Api.hpp:14`, `:17`, `:20`, `:21` and `:22`: the declarations;
     - `core/libs/prx/libScePosixForWebKit/CMakeLists.txt:3`: add `libkernel_sys` and `libSceLibcInternal` to `target_link_libraries`;
     - debt `:7`: replace the entry with a line under "Functional" that extends the `--wrap` assumption at `:17` to these five.
   - Semantics: debt `:17` (the `--wrap` convention, unverified on a console, as for the six done wrappers) and the targets' own FreeBSD-derived behaviour.
   - Tests: extend `guest_posix_filesystem_tests` (`core/libs/tests/GuestPosixFilesystem.cpp`, `core/libs/prx/libScePosixForWebKit/CMakeLists.txt:10`, `:14`), and add the two libraries to its link line (`:12`). Each wrapper's result and `errno` must equal its target's on the same inputs:
     - `link` of an existing file, and `ENOENT` for a missing one;
     - `symlink` with a relative target, then `readlink` returns it, and `EEXIST` on a second `symlink`;
     - `statvfs("/")` returns 0, and `EFAULT` for a null output;
     - `fchown(fd, -1, -1)` returns 0 for an open descriptor and `EBADF` for a closed one.

## Dependency notes

- Linking `libkernel_sys` and `libSceLibcInternal` into another prx is new: no prx does it today. libSceSpSysCallWrapper (`core/libs/prx/libSceSpSysCallWrapper/CMakeLists.txt:3`) and libSceVideoOut (`core/libs/prx/libSceVideoOut/CMakeLists.txt:26`) link other prx libraries, so the pattern exists. Check that the NID patcher (`core/libs/CMakeLists.txt:196-202`) and the Windows `PATH` test setup still work.
