# libSceJemspace (13 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 13)

**What it is:** an allocator that works on spaces the caller creates: create or destroy a space; malloc, calloc, realloc and the aligned forms; free; usable size, statistics and purge. Judging by the name, it is built on jemalloc. Its module id is 0x8000009e (`core/libs/prx/libSceSysmodule/ModuleTable.hpp:320`).

**Existing state** (`core/libs/prx/libSceJemspace/Export.cpp`; there is no CMakeLists, so `core/libs/CMakeLists.txt:159` builds `Export.cpp`):
- 0 done.
  - Thirteen `int f()` bodies throw (`:7-57`).
  - The declarations are the same in `include/Api.hpp:7-19`.
- No state and no error constants.
- Debt (`docs/dev/debt/libSceJemspace.md:5`):
  - each name hashes to a defined NID;
  - the module's firmware version is unverified;
  - the declarations establish no argument layout, return type, memory ownership, failure handling or statistics layout.
- No tests.
- Neighbour: libc's mspace allocator is done (`core/libs/prx/libc/src/Mspace.cpp:145-288`).
  - Its forms include `sceLibcMspaceCreate(name, base, size, flags)` (`:145`), `Malloc(handle, size)` (`:178`) and `Free(handle, pointer)` (`:183`).
  - Ten of the thirteen Jemspace names have a same-named mspace function.

## Families

- **Same-named mspace counterpart (10): D.**
  - Calloc, Create, Destroy, Free, Malloc, MallocUsableSize, Memalign, PosixMemalign, Realloc and Reallocalign.
  - These are not A. Nothing in the tree or a cited source shows that a Jemspace call takes the mspace argument list, handle or failure answers. For example, a jemalloc space may be created from extent hooks rather than a fixed base and size.
  - The declarations even return `int` where an allocation would return a pointer. That fits the debt's "no ABI claim".
- **No counterpart (3): D.**
  - GetStats: layout unknown; mspace has `MallocStats` with `MallocManagedSize` instead (`Mspace.cpp:280`).
  - PrintStats: output format and destination unknown.
  - Purge: scope and result unknown.

## Batch proposal

None. A forward to the mspace allocator first needs a prototype source, such as a title's call sites with their register use, recorded in the debt.
- Claim: PR 9855 (open) implements all thirteen. Unless it brings such a source, its bodies are guesses.

## Caveats

- The mspace similarity is in the names only. Even with a known prototype, Create's sizing, alignment and flag checks would need a source of their own.
