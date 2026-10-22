# libSceComposite (78 todo: D 78)

What it is: the client side of the PS5 system compositor. It has 75 `sceCompositor*` exports plus 3 misspelled `sceCompsoitor*` ones, and system processes (shell UI, overlays) use it to build compositor command buffers, manage canvases and render targets, and map compositor-owned memory.

`libSceCompositeExt` is an `APS5_DUMMY_FUN` skeleton that defers to this library. Every export here is declared without parameters, and `docs/dev/debt/libSceComposite.md` says no credible prototypes were found.

Families (all D):
- Command builders (`Set*Command`, `ResetZoomCommand`, GPU perf and thread-trace markers; 29): the compositor command format is private.
- Session and command-buffer control (Init, Quit, Flush, Reserve, Lock, Release, Wait*, HandleProcessEvents, CheckCrash; 17): private system IPC.
- Memory and process mapping (system, video and dummy addresses or sizes, another process's mapping, DmemOffset, MemoryPoolDecommit; 13): compositor-owned memory layout.
- Canvas, render-target and system configuration (14): a private service with no prototypes.
- Status and timing queries (`IsDebugCaptureEnabled`, `IsReady4K`, GPU clock, rendering times; 5): the return convention and console values are unknown. So even the boolean queries are not C.

No row is A, B or C. No other library in the tree implements a compositor client, and VideoOut presents directly.

Third-party: none.

Batch proposals: none. Leave the library alone until a title imports one of these exports. Then take the signature from its call sites (`docs/dev/REVERSE_ENGINEERING.md`). Expect most such calls to come from system modules, not games.
