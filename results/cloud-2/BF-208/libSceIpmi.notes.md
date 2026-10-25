# libSceIpmi (54 todo: A 0, B-S 0, B-M 0, B-L 0, C 0, D 54)

- **What it is:** IPMI, the console's inter-process message interface. It is a C++ client/server/session library over the kernel's IPMI manager. System libraries use it to call system services: they create an `IPMI::Client`, invoke sync or async methods and read message queues, and services run an `IPMI::Server` with an `EventHandler`. Five C exports, `sceIpmiMgr*`, are manager-side debugging calls. Titles do not call it directly. Its callers are the system modules that AnyPS5 replaces with its own libraries.
- **Existing state:** 0 done.
  - `core/libs/prx/libSceIpmi/PendingExports.cpp` holds the 49 mangled C++ exports and `Export.cpp` the 5 `sceIpmiMgr*`. All are `(void)` bodies calling `NotImplemented_nid_no_patch`. There are no types.
  - `CMakeLists.txt` builds only the library and registers no test.
  - The kernel side, `ipmimgr_call` (`libkernel_sys/Export.Discovery.cpp:110`), is todo as well; PM-088 classed it D, "Sony IPMI manager IPC syscall".
- **Prototypes:** `docs/dev/debt/libSceIpmi.md` says the signatures and behaviour of all 54 are unverified, with names from the PS5 3.20 export table. The Itanium mangling (Itanium C++ ABI §5.1) gives the C++ parameter types, but not the return types, class layouts, vtable slot order, Config default values or size formulas. A caller reaches the client's methods through the object's vtable: `connect` and `invoke*` are not exported. Even `create` therefore has to reproduce the console's object layout.
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 54.
- **D families:**
  - Client (15): `Client::create(Client**, const Config*, void*, void*)`, the Client constructor and its three destructors, the constructors of Config and its MsgQueueConfig, SyncMethodInvocationConfig and AsyncMethodInvocationConfig (C1/C2 each), `estimateMsgQueueSize(unsigned long, unsigned int)` and `estimateClientMemorySize()`.
  - Server (16): `Server::create`, the Server constructor and three destructors, the Server::Config constructors, `estimateTempWorkingMemorySize() const`, and EventHandler's five default virtuals (`onSessionKilled`, two `onSyncMethodDispatch`, two `onAsyncMethodDispatch`) plus its three destructors.
  - Session and SessionImpl (18): the Session constructor and three destructors, `estimateSessionMemorySize()`, and SessionImpl's constructors and destructors, `destroy`, `getServer`, `isPeerPrivileged() const`, `sendMsg`, `getMsg`, `tryGetMsg` and two `respondToAsyncMethodRequest`.
  - `sceIpmiMgr*` (5): GetIpcBlockerPid, GetIpcBlockerPidByAppId, PauseDump, StartDump and RaiseExceptionToPid. They are parameterless with no public prototype and act on other processes.
- **Why not C:** a "no service" answer would need `create` and the session calls to fail in the console's way. No such error code is in the tree or a public source. AnyPS5 also has no system services for a client to reach.
- **Why not B:** the destructors and Config constructors look mechanical, but their effect depends on member layouts and defaults that no public source gives. Writing them would be a guess.
- **Batch proposal:** none (no A, B or C rows).
- **Caveats:**
  - The library only matters for a title that imports `IPMI::*` directly. No title is cited in the tree. Before any work, a call site should settle the ABI (`docs/dev/REVERSE_ENGINEERING.md`).
  - The vtable order of `Client`, `Server` and `Session`, the `DataInfo`/`BufferInfo` layouts and the Config field defaults are the prerequisites.
