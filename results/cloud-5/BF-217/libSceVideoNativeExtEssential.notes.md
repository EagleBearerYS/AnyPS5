# libSceVideoNativeExtEssential (5 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 5)

**What it is:** a native video-extension module that initializes and finalizes itself and creates or destroys decoder instances.

**Existing state** (`core/libs/prx/libSceVideoNativeExtEssential/Export.cpp`; there is no CMakeLists, so `core/libs/CMakeLists.txt:159` builds `Export.cpp`):
- 0 done. Five parameterless bodies throw (`:9-32`).
- No state and no error constants.
- Debt (`docs/dev/debt/libSceVideoNativeExtEssential.md:5`): the names are in the 3.20 export inventory, and "no credible prototypes were found".
- No tests. No other library has these names.

## Families

- **Lifecycle (3): D.** Initialize, Finalize and IsInit. There is no prototype. Even IsInit's return encoding is unknown.
- **Decoder instances (2): D.** CreateDecInstance and DestroyDecInstance. There is no prototype, and the decoder is a missing subsystem.

## Batch proposal

None. No claims.

## Caveats

- None beyond the missing prototypes.
