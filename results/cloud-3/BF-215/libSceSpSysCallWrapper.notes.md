# libSceSpSysCallWrapper (1 todo: A 1)

**What it is:** thin `sceSp*` wrappers over libkernel pthread, time and select calls and libSceNet socket, epoll and resolver calls.

**Existing state:**
- 47 done. Each one forwards to the libkernel or libSceNet function of the same name, for example:
  - `sceSpPthreadMutexattrInit` calls `scePthreadMutexattrInit` (`core/libs/prx/libSceSpSysCallWrapper/Export.cpp:182`);
  - `sceSpPthreadMutexattrSetprotocol` calls `scePthreadMutexattrSetprotocol` (`:191`).
- The library keeps no state of its own.
- The target is now implemented:
  - `scePthreadMutexattrSetprioceiling` is in `core/libs/prx/libkernel/Pthread/Posix/Mutex.cpp:203` and declared at `core/libs/prx/libkernel/include/Api.hpp:790`.
  - It wraps the POSIX `pthread_mutexattr_setprioceiling_nid_postfix` (`Mutex.cpp:146`). That returns `EINVAL` for a null or uninitialized attribute and for a protocol other than `PTHREAD_PRIO_PROTECT`, and the result goes through `PosixThread::ToSce`.
- Test: `guest_sp_syscall_wrapper_tests` (`core/libs/tests/GuestSpSysCallWrapper.cpp`), registered at `core/libs/prx/libSceSpSysCallWrapper/CMakeLists.txt:7` and `:11`. It already checks the other mutexattr wrappers at `:53-64`.

**Counts:** A 1, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 0.

## Families

- **A (1):** `sceSpPthreadMutexattrSetprioceiling`.
  - Debt `docs/dev/debt/libSceSpSysCallWrapper.md:5` read firmware 13.60's module: it only jumps to libkernel's `scePthreadMutexattrSetprioceiling`.
  - That function is now implemented, so the debt's "AnyPS5 does not implement yet" is stale.

## Batch proposals

1. **"sp: forward sceSpPthreadMutexattrSetprioceiling" (1).** This is far below 10 functions, so merge it with the libScePosixForWebKit `__wrap_*` forwarders (5 A) into one "forwarders" task.
   - Files:
     - `core/libs/prx/libSceSpSysCallWrapper/Export.cpp:186`: take `(PthreadMutexattr* attr, int prioceiling)` and return `scePthreadMutexattrSetprioceiling(attr, prioceiling)`;
     - `include/Api.hpp:52`: the declaration;
     - remove the debt entry (`docs/dev/debt/libSceSpSysCallWrapper.md:5`).
   - Semantics: libkernel's implementation (`Mutex.cpp:146`, `:203`) and the firmware disassembly that debt `:5` records.
   - Tests: extend `guest_sp_syscall_wrapper_tests` (`core/libs/tests/GuestSpSysCallWrapper.cpp`, `core/libs/prx/libSceSpSysCallWrapper/CMakeLists.txt:7`, `:11`):
     - with protocol 0, the wrapper's result equals `scePthreadMutexattrSetprioceiling`'s (the libkernel `EINVAL` mapping);
     - after `sceSpPthreadMutexattrSetprotocol(&attr, 2)` it returns 0, and `scePthreadMutexattrGetprioceiling` reads the value back.
