# libSceDiscId (2 todo: D 2)

**What it is:** reads the identifier of the inserted disc.

**Existing state:**
- 0 done. `core/libs/prx/libSceDiscId/Export.cpp:9-17` holds two declarations with real prototypes (`core/libs/prx/libSceDiscId/include/Api.hpp:11-12`):
  - `sceDiscIdGet(DiscId*)`;
  - `sceDiscIdGet2(DiscId2*, std::uint32_t flags)`.
- `DiscId` and `DiscId2` are incomplete types (`include/Api.hpp:5`, `:7`).
- No state, no CMakeLists.txt and no guest test.
- Debt `docs/dev/debt/libSceDiscId.md:5`:
  - both calls pass their pointer to a device ioctl without touching it;
  - `flags` must be 0;
  - the output sizes and fields are unresolved.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 2.

## Families

- **D (2): console answer unknown.**
  - The output layout is unknown.
  - The tree models no disc device, so it holds neither a disc answer nor an "absent disc" one.
  - The error for a non-zero `flags` is not recorded.
  - Orbital's type database has no entry for either call.

## Batch proposals

None.

## Caveats

These two rows become C-real only if a console measurement records:
- the answer with no disc inserted (the usual case for a title run from files);
- the error for a non-zero `flags`.
