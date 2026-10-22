# libSceBgft (72 todo: A 0, B-S 4, B-M 0, B-L 0, C 0, D 68)

What it is: the client of the background file transfer (bgft) daemon. It registers, starts, pauses, queries and removes download and install tasks (game, patch, PUP, add-on, PlayGo chunks) and upload tasks. Homebrew package installers use `sceBgftServiceIntDownloadRegisterTaskByStorageEx` to install a package from local storage.

Families and decisions:
- Init/Term and IntInit/IntTerm (4): B-S. The library keeps its state after init with the confirmed `BgftInitParam {heap, heapSize}`, in the same SCE pattern as `sceRemoteplayInitialize`. Null or double init throw because the error codes are not in the tree. These only help a title that calls Init and never registers a task.
- Download task control (Find, Get progress, Pause/Resume/Start/Stop, Register, Unregister, Query, Restart, *All) (30): D. Task IDs, the 88-byte progress layout (debt) and the not-found/busy errors are not public, and downloads need PSN.
- RegisterTaskByStorage(Ex) (2): D. Installing a package needs PKG decryption keys, which are not public, and an install service. The 112-byte parameter's scalars are still unresolved (debt).
- PlayGo Int* and Debug* (19): D. PlayGo install-state control inside the daemon, plus debug-only environment files.
- Upload, TaskInfo value and storage size exports (17): D (unknown signatures, debt).

New third-party dependencies: none. A real package installer would need PKG/PFS crypto keys, which no public library supplies. mbedtls could do the AES/RSA arithmetic, but not without the keys.

Batch proposal: 1. `feat(bgft): keep service init state` (4 B-S rows). Add a CMakeLists.txt to core/libs/prx/libSceBgft with a `guest_bgft_tests` executable: Init then Term succeeds, IntInit then IntTerm succeeds, Term before Init and double Init throw. It is below the 10-function batch size, so it should go in only together with a title that imports these exports.
