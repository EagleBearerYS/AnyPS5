# libScePsmUtil (4 todo: D 4)

**What it is:** a small utility library whose exports are initialize, finalize, register callbacks and get the PSM ini file. Nothing in the tree says more about its purpose.

**Existing state:**
- 0 done. Every export is a parameterless declaration that throws (`core/libs/prx/libScePsmUtil/Export.cpp:9`, `:14`, `:19`, `:24`).
- There is no `CMakeLists.txt` in the library: it is built by the generic loop in `core/libs/CMakeLists.txt:159`. There are no tests, state objects or error constants.
- Debt `docs/dev/debt/libScePsmUtil.md:5`: the names and NIDs come from the PS5 3.20 export inventory, and no credible prototypes were found.
- No other library in the tree calls or implements these.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 4.

## Families

- **D (4), unknown signature:** all four have no credible prototype (debt `:5`).
  - `GetPsmIniFile` writes an output of unknown shape.
  - `RegisterCallbacks` takes an unknown callback record.
  - `Initialize` and `Finalize` have no known answer. A success would be a made-up value.

## Batch proposals

None. No row is A, B or C-real.

## Dependency notes

- An implementation needs prototypes and the console's answers from a title or a module disassembly.
