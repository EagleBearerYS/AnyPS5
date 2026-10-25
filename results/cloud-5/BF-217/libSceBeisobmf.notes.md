# libSceBeisobmf (18 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 18)

**What it is:** Sony's ISO base media file format (MP4 family) parser. It provides box accessors (`isobmf_box_*`), a decoder object (`isobmf_decoder_*`) and an allocator hook (`isobmf_memmanager_set`). The exports have plain C names, kept apart from the libc names by the `_nid_postfix` suffix.

**Existing state:**
- No CMakeLists. `Export.cpp` is built by `core/libs/CMakeLists.txt:159`.
- Nothing is done. All 18 are parameterless throws (`core/libs/prx/libSceBeisobmf/Export.cpp:10-98`). `APS5_DUMMY_FUN` at `:9` adds `DummyFunction_nid_no_patch` (`core/libs/prx/libc/include/General.hpp:98`).
- Debt `docs/dev/debt/libSceBeisobmf.md:5`: the names come from the PS5 3.20 export table, with signatures and behaviour unverified. The same entry is duplicated at `:7`.
- No tests. Module id 0xc2 in `core/libs/prx/libSceSysmodule/ModuleTable.hpp:82`.
- Public sources: Orbital `db_types.json` at b321fcdbd7 has no `isobmf_` entry. No ISOBMF parser exists anywhere in the tree.

## Families

- **Box accessors (9, D):** getchildbox, getnextbox, gettype, getstate, getflags32b, getdiskoffset, getmember_ptr/_length/_type.
  - The box format itself is public (ISO/IEC 14496-12). This API's box handle, member model and parse states are not.
  - With prototypes these would be B-M (a box parser), but no source gives them.
- **Decoder object (8, D):** create, decode, clear, destroy, query, getquerydata, get_decoder_tree, next_disk_offset. The creation parameters, the query keys, the decoder-tree layout and the disk-offset paging contract are all unknown.
- **memmanager_set (1, D):** the allocator callback table is unknown.

## Batch proposal

None.

## Caveats

- PR 8747 (closed) declared all 18. No open claim.
- The debt file repeats its only entry (`:5` and `:7`).
