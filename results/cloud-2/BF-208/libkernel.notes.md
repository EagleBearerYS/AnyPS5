# libkernel (BF-208 classification)

- **What it is:** the PS5 kernel interface a title links: FreeBSD 11 syscall wrappers and libthr, plus the SCE event queue, memory, module, file and thread calls. 683 done, 37 todo (upstream main e7e3e9c198).
- **Counts:** A 0, B-S 0, B-M 2, B-L 6, C 0, D 29.
- **Existing state:** the 37 rows are of three kinds (paths under `core/libs/prx/libkernel/`).
  - **22 parameterless declarations in `Unimplemented.cpp`:** the 17 `sceKernelUnknown03`–`19` NIDs (`:19-119`), `physhm_open`/`physhm_unlink` (`:9`, `:14`), `sceKernelPrintBacktraceWithModuleInfo` (`:121`), `sceKernelGetProcessAbiType` (`:126`) and `__Ux86_64_setcontext` (`:135`).
  - **4 whole-function throws with real signatures:** `execvp`/`execv`/`execve` (`System/src/Process.cpp:477`, `:484`, `:491`) and `nmount` (`File/src/Stdio.cpp:713`).
  - **11 partial functions** sit next to working code. progress.py counts each as todo because one branch reaches `NotImplemented_nid_no_patch`, directly or through a helper in the same file. Each row describes only that branch.
    - `__tls_get_addr` (`Module/src/Tls.cpp:28`): the macOS branch.
    - `_umtx_op` (`SyncOnAddress/src/SyncOnAddress.cpp:359`): the mutex, CV, rwlock, SEM2, SHM and robust-list ops; the waits and wakes work over the waiter buckets.
    - `cpuset_getaffinity`, `cpuset_setaffinity` and `rtprio_thread` (`Pthread/Posix/Thread.cpp:43`, `:73`, `:110`).
    - `sysarch` (`System/src/Process.cpp:591`).
    - `kevent` (`Equeue/src/Kqueue.cpp:565`, read/write/vnode/timer/user filters over the event queue).
    - `ktimer_create` (`Time/Ktimer.cpp:92`, timer table `g_ktimers`, ids 3–31, SIGEV_NONE only).
    - `sceKernelAddFileEvent` (`Equeue/src/FileEvent.cpp:755`, host file watchers).
    - `sceKernelAddReadEvent` and `sceKernelAddWriteEvent` (`Equeue/Equeue.cpp:841`, `:849`, through `AddDescriptorEvent` `:820`).
- **No A:** no todo name or NID is implemented elsewhere in the tree (checked against `main-libraries.json`).
  - The closest is `__Ux86_64_setcontext`. Its name matches nongnu libunwind's `_Ux86_64_setcontext(ucontext_t*)`, which would be a forward onto `setcontext` (`System/src/UserContext.cpp:187`). The tree cites no such source and has no debt entry for it, so the row is D.

## B families

- **B-M (2):**
  - `ktimer_create` signal notification. FreeBSD 11 `kern_time.c` (`realtimer_expire`, `itimer_fire`, SI_TIMER siginfo, overrun counting) needs an expiry thread that delivers the guest signal asynchronously through `Signals.cpp` `MarkPending`/`DeliverPendingSignal` (`:94`, `:212`).
    - The same asynchronous delivery is missing for setitimer's ITIMER_REAL (`Signals.cpp:307`), so one helper closes both.
    - The thread interruption `sceKernelRaiseException` uses (`System/src/Exception.cpp:678`) is the in-tree mechanism to build on.
  - `sceKernelAddFileEvent`: the extra FreeBSD 11 vnode notes (`NOTE_REVOKE`, `NOTE_OPEN`, `NOTE_CLOSE`, `NOTE_CLOSE_WRITE`, `NOTE_READ`).
    - They need hints posted from libkernel's own open, close, read and revoke paths, because no host notifier reports them on every host.
    - Device vnodes need `vfs_kqfilter`'s answer.
- **B-L (6), each a different missing subsystem:**
  - process-image replacement for `execv`/`execve`/`execvp` (FreeBSD 11 `kern_exec.c`; `execvp` adds libc `gen/exec.c` PATH search);
  - a mount table in the guest path resolver for `nmount` (`vfs_mount.c`), the same one PM-088 named for libkernel_sys `mount`/`unmount`;
  - guest segment bases separate from host TLS for `sysarch` (`AMD64_SET_FSBASE`, I386 set operations, every operation on Windows and macOS), the same one PM-088 named for libkernel_sys `amd64_set_fsbase`;
  - a macOS guest TLS runtime for `__tls_get_addr`: `Aps5GuestTlsAllocate`/`Register`/`GetAddr_nid_no_patch`, which the macOS relinker binds (`MacOsImage.cpp:544`, `MacOsGuestModuleWriter.cpp:86`) and no library defines.
- **C:** none. No row is a device or service a PC lacks with a known console answer.

## D families and why

- **Unknown NIDs (17):** `sceKernelUnknown03`–`19` have no name or signature (debt `:104-120`). None of the NIDs appears anywhere else in the tree.
- **Parameterless declarations without a public prototype (5):** `physhm_open`, `physhm_unlink` (debt `:20`), `sceKernelPrintBacktraceWithModuleInfo` (`:125`), `sceKernelGetProcessAbiType`, where the tree refuses a guessed constant (`:293`), and `__Ux86_64_setcontext` (no debt entry).
- **Partial functions whose remaining branch is console state (6):**
  - `cpuset_get/setaffinity` ROOT/CPUSET levels and CPUSET/IRQ/JAIL/DOMAIN targets: the console's jail and root set masks, set ids and IRQ affinity.
  - `rtprio_thread` `RTP_SET`: the console departs from FreeBSD and no accepted value is measured.
  - `sceKernelAddRead/WriteEvent`: a size above 1, which the tree cannot map to `NOTE_LOWAT`, and other descriptor kinds.
  - `kevent`: Sony filters through kevent, and unmeasured timer re-adds where the console follows 11.0 rather than 11.4.
- **`_umtx_op`:** mostly FreeBSD 11 `kern_umtx.c`, which would be B-M over the existing waiter buckets. But `SEM2_WAIT`/`SEM2_WAKE` were measured to differ from FreeBSD (debt `:63`), so the row cannot close from public sources.

## Batch proposals

None. No A/B/C family reaches 6 functions; the 6 B-L rows are four unrelated subsystems.

The largest coherent unit is `exec*` (3 rows). If it is ever done, it extends `guest_execvp_tests` (`core/libs/tests/GuestExecvp.cpp`, `core/libs/prx/libkernel/CMakeLists.txt:1253`, `add_test(NAME guest_execvp …)` at `:1257`).

The two B-M rows fit existing executables:
- `ktimer_create` signals: `guest_kernel_sys_ktimer_tests` (`core/libs/tests/GuestKernelSysKtimer.cpp`, `core/libs/CMakeLists.txt:820`/`:824`). Cover a SIGEV_SIGNAL timer caught by sigwait, a handler run, `si_value` and the overrun count.
- `sceKernelAddFileEvent` notes: `guest_kqueue_tests` (`core/libs/tests/GuestKqueue.cpp`, `libkernel/CMakeLists.txt:1589`/`:1593`). Cover NOTE_OPEN/CLOSE/READ through the guest's own open, read and close.

Other existing executables for these rows:
- `guest_umtx_op` (`:1149`), `guest_rtprio_thread` (`:1228`), `guest_sysarch` (`:1296`), `guest_equeue_events` (`:1316`) and `guest_tls_get_addr` (`:568`/`:572`).
- `guest_available_cpumask` (`:1155`/`:1159`, `GuestAvailableCpumask.cpp`) for cpuset.

## Caveats

- `__Ux86_64_setcontext` has no entry in `docs/dev/debt/libkernel.md`, which CONTRIBUTING asks for.
- `_umtx_op` and `kevent` are D only because part of their remaining branch is console behaviour.
  - The FreeBSD-defined part of each could still be implemented. It would leave the row todo until the rest is measured.
- PM-088 classed libkernel_sys `cpuset`/`cpuset_getid`/`cpuset_setid` as B-S on FreeBSD set ids. The ROOT/CPUSET levels here are D because their answer is the console jail's set, which is not measured.
  - If a set-hierarchy model is accepted for libkernel_sys, these two rows should follow the same decision.
- The `exec*` and `nmount` B-L rows assume FreeBSD semantics, as the rest of libkernel does. Whether a title process may exec or mount on the console is unmeasured, and may be a refusal.
