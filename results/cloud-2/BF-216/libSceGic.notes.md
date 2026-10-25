# libSceGic (1 todo: A 0, B-S 0, B-M 0, B-L 0, C 0, D 1)

Upstream main 2742004ed2.

- **What it is:** unknown beyond its one export name, `sceGicGetValue`. The sysmodule table lists it as module 0x107 (`core/libs/prx/libSceSysmodule/ModuleTable.hpp:148`).
- **Existing state:** 0 done.
  - `core/libs/prx/libSceGic/Export.cpp:9`: one parameterless declaration that throws. No state or constants.
  - Debt `docs/dev/debt/libSceGic.md:5`: the name and NID are in the PS5 3.20 export inventory, "but no credible prototypes were found", and the declaration is "not an ABI claim".
  - No CMakeLists.txt (generic rule, `core/libs/CMakeLists.txt:158-161`) and no guest test.
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 1.
- **D (1):** `sceGicGetValue`. No prototype, no described value, and no device or service in the tree that it reads.
- **Batch proposal:** none.
- **Caveats:** the debt entry's link text begins "[ and sceGicGetValue]", an editing leftover; it covers only this one export.
