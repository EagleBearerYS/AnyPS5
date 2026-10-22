# libSceGnmDriverForNeoMode (118 todo: C 2, D 116)

What it is: the PS4 Gnm graphics driver's exports kept for Neo (PS4 Pro) mode. PS5 titles render through AGC (libSceAgc, libSceAgcDriver). There is no other Gnm implementation in the tree: no `sceGnm` symbol exists outside this library's `Export.cpp`, and `libSceGnmDriverCompat1` is an empty `APS5_DUMMY_FUN` skeleton.

Knowledge in the tree: the PM4 opcodes and parser (`libSceAgcDriver/Execution/include/Pm4Opcodes.hpp`, `Execution/src/Pm4.cpp`) and the AGC packet builders (`libSceAgc/Command`, `DcbDraw`, `Cb`). These settle the DRAW_INDEX_2, DRAW_INDEX_AUTO, DISPATCH_* and WAIT_REG_MEM layouts. They do not settle the Gnm ABI:
- every export is declared `(void)`, and the debt file says the signatures are unverified;
- the Gnm modifier/predication bits, NOP padding sizes, Neo default-state tables and shader register blocks are not in the tree.

Even a correct packet writer would run nothing. The library has no plain Gnm submit path (only the `*ForWorkload` variants are listed), Neo command buffers reference GCN shaders, and the driver and recompiler target PS5 state. Hence all packet and submit families are D.

Families:
- Draw writers (11), dispatch/compute waits (5), default-state init (7), shader setup (9), register/marker writers (8), workload/submission (9): D. The ABI is not public and no execution path exists.
- Resource registration (20): D. On the console this would be a fixed "registration unavailable" (C), as AGC returns 0x8A6C9018 (`libSceAgcDriver/Resource/src/Registration.cpp:26`), but the Gnm error value is not in the tree or a public source. If a public source gives that value, all 20 turn C at once. This is the only real lever in this library.
- Validation (9), debug/PA/coredump (19), tool interfaces (6), hardware constants (5), SDMA (8): D.
- C (2): `sceGnmDriverTraceInProgress` and `sceGnmValidateOnSubmitEnabled` return false, the same answer as `sceAgcDriverIsTraceInProgress` and `sceAgcDriverIsSubmitValidationEnabled` (`libSceAgcDriver/State/src/Status.cpp:14`, `:18`). This is real behaviour, not a silent stub.

Third-party: none would help. What is missing is Gnm submission and a GCN shader path. Those would be in-tree subsystems, not dependencies.

Batch proposals: none reaches 10 functions.
- The only implementable rows are the 2 C rows. Ship them only when a title imports them, as a 2-function PR with a unit test that calls both and expects 0.
- Before any further work, get call-site evidence that a PS5 title imports this library at all. The tree's debt has none.
