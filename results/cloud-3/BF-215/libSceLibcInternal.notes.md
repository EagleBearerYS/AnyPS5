# libSceLibcInternal (4 todo: D 4, two of them partial)

**What it is:** the console's internal libc companion module. It holds the filesystem primitives behind `std::filesystem`, `posix_spawn` and its attributes, `getopt`, `err`/`warn`, `inet_*`, `statvfs`, `makecontext` and thread-exit destructors.

**Existing state:**
- 72 done.
- Filesystem primitives: `core/libs/prx/libSceLibcInternal/Filesystem.cpp`. `_Close_dir` is at `:171`. The two `_Xfilesystem_error` overloads are at `:175` and `:179`.
- Spawn objects and the program search:
  - `PosixSpawnAttributes` and `PosixSpawnFileActions` (`include/Spawn.hpp`);
  - `ProgramFound` (`Spawn.cpp:39`) and `Spawn` (`:53`);
  - `posix_spawn` (`:201`) and `posix_spawnp` (`:208`).
- Guest C++ exceptions are thrown through libc's `LibcException`:
  - `std::system_error`'s guest type and vtable are at `core/libs/prx/libc/src/exception/StandardExceptions.cpp:467-468`;
  - the `_X*` throwers are at `:517-526`;
  - there is no `filesystem_error`.
- Tests (all in `core/libs/prx/libSceLibcInternal/CMakeLists.txt`):
  - `guest_posix_spawn_tests` (`core/libs/tests/GuestPosixSpawn.cpp`), at `:75` and `:79`;
  - `guest_libc_filesystem_tests` (`core/libs/tests/GuestLibcFilesystem.cpp`), at `:168` and `:172`;
  - and 15 other guest tests.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 4.

## Families

- **D (2), `std::filesystem::_Xfilesystem_error`, both overloads:**
  - On the console they throw `std::filesystem::filesystem_error` (debt `docs/dev/debt/libSceLibcInternal.md:10`). Its guest type information is not in the tree.
  - Its layout is not in the tree either: a `system_error` base, two `path` members and a cached `what` string.
  - Nothing public gives the console STL's layout for `filesystem_error` or `std::filesystem::path`.
  - Both declarations are parameterless (`Filesystem.cpp:175`, `:179`).
- **D (2), partial, `posix_spawn` and `posix_spawnp`:**
  - Done: the FreeBSD 11.4 `ENOENT` path for a missing program (`Spawn.cpp:57`), the `PATH` search (`:39-51`) and the attribute and file-action objects.
  - Left: the branch where the program exists (`:58`). It needs child-process creation, which the tree does not model.
  - Whether the console's application sandbox lets a title create a child is also unverified (debt `:16`).

## Batch proposals

None. No row is A, B or C-real.

## Dependency notes

- `_Xfilesystem_error` needs guest `filesystem_error` and `path` type information and layouts. These come from disassembly of the console's `libSceLibcInternal` or `libc` (the typeinfo name and the object size and offsets). After that, it is a small B-M on top of `LibcException::ThrowCppError`, which `std::system_error` already uses (`StandardExceptions.cpp:536`).
- `posix_spawn` needs a subsystem that launches a second guest executable (a new process with its own relinked image), or a console measurement showing that titles are refused.
