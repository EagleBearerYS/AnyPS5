# libSceDbgThreadSanitizer (2 todo: C-silent 2)

**What it is:** the console's ThreadSanitizer runtime hooks. `__tsan_acquire(addr)` and `__tsan_release(addr)` annotate happens-before edges for a race detector.

**Existing state:**
- 0 done. Two throwing declarations: `core/libs/prx/libSceDbgThreadSanitizer/Export.cpp:7` and `:11`.
- Debt `docs/dev/debt/libSceDbgThreadSanitizer.md:5`:
  - the `void (void* addr)` signatures are LLVM compiler-rt's `tsan_interface.h`, linked there at a pinned commit, whose names hash to the imported NIDs `XyX1104ACcU` and `IkoI44e0OXc`;
  - AnyPS5 runs no race detector;
  - PPSA23226's bundled `libSceJobManager_nosubmission.prx` calls them only after `sceKernelIsThreadSanitizerEnabled` reports the sanitizer enabled.
- `sceKernelIsThreadSanitizerEnabled` always returns 0 (`core/libs/prx/libkernel/System/src/Sanitizer.cpp:10-12`), so the known importer never calls them.
- No state, no `CMakeLists.txt` (built from the glob at `core/libs/CMakeLists.txt:146`, `:159`) and no guest test.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 2, D 0.

## Families

- **C-silent (2): race-detector annotations without a race detector.**
  - Both would return without effect. The edge they record has no consumer in the tree, and they return nothing.
  - Silent stub, needs a title. CONTRIBUTING.md:10 allows one only when it unblocks a title; today none reaches them.

## Batch proposals

1. **"dbgthreadsanitizer: no-op annotations" (2, C-silent):** only when a title calls them, for example one that runs with the sanitizer enabled.
   - Files: `core/libs/prx/libSceDbgThreadSanitizer/Export.cpp` (empty bodies) and `docs/dev/debt/libSceDbgThreadSanitizer.md`, where the entry moves under Silent stubs.
   - Tests: none exist. A new `guest_dbg_thread_sanitizer_tests` would need a `CMakeLists.txt` in the library directory, as `core/libs/prx/libSceAudioPropagation/CMakeLists.txt:1-15` does. Two calls that return would add little, so leaving it untested is reasonable.
   - Size: 2 functions. Merge it with any other small debug-library batch.

## Dependency notes

- None.
