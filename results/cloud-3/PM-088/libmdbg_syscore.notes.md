# libmdbg_syscore (143 todo: A 0, B-S 0, B-M 0, B-L 0, C 0, D 143)

libmdbg_syscore is the SysCore-side client of the console's memory debugger (mdbg). Debug agents (Target Manager, SDBGP, the coredump service) use it to control and inspect other processes. Titles do not call it. All 143 exports in `core/libs/prx/libmdbg_syscore/Export.cpp` are parameterless stubs. The library's debt file says the names and NIDs come only from the PS5 3.20 export inventory and that no credible prototypes exist.

Families (all D):
- Process and thread control (19): attach, detach, kill, spawn, suspend, resume, single step, stop-on-DLL-load. These act on other processes through the private mdbg service; AnyPS5 runs one guest process.
- Memory and register access (9): `sceDebug{Read,Write}{Process,Thread}{Memory,Register}` and the SDBGP variants. Target selection and buffer layouts are not public.
- Introspection lists (29): process, thread, module, file, sync-object, VM, application and resource-stat lists. These return private struct layouts.
- Runtime views (23): ULT, job manager, fiber and Mono VM. They expose the internal layouts of runtimes the title links itself.
- IPMI views (25): debugger dumps of system IPC clients, servers, sessions and channels. This is private service state.
- Coredump (14), event stream (8), scratch areas (10), debug registers (3), init (2) and VR capture (1): debug-only interfaces with no prototype.

No row is A: nothing else in the tree implements a `sceDebug*` name or its behaviour. libkernel's coredump exports (`System/src/Coredump.cpp`) are the title-side handler API, not the debugger side. No row is C either, because no error code for "no debugger service" is in the tree or a public source.

Dependencies: none.

Batch proposals: none. The library has no A, B or C row. A useful first step would be prototypes taken from a title or agent that imports these functions, following `docs/dev/REVERSE_ENGINEERING.md`. That is out of scope for this read-only pass.
