# libSceTextToSpeech (10 todo: D 10)

**What it is:** the first text-to-speech interface, with `*Impl` exports to initialize and terminate, open and close, speak or speak continuously, pause, resume and cancel, and get the speech status.

**Existing state:**
- 0 done. Every export is a parameterless declaration that throws (`core/libs/prx/libSceTextToSpeech/Export.cpp:9-57`, one every 5 lines).
- There is no `CMakeLists.txt` in the library: it is built by the generic loop in `core/libs/CMakeLists.txt:159`. There are no tests, state objects or error constants.
- Debt `docs/dev/debt/libSceTextToSpeech.md:5`: the names and NIDs come from the PS5 3.20 export inventory, and no credible prototypes were found.
- Neighbour: `libSceTextToSpeech2` has typed exports that all return the kernel's EOPNOTSUPP 0x8002002D (`core/libs/prx/libSceTextToSpeech2/Export.cpp:9`, `:20-51`).
  - Its debt records this as a silent stub (`docs/dev/debt/libSceTextToSpeech2.md:5`).
  - It also says the console's error for an unavailable speech engine is unknown (`docs/dev/debt/libSceTextToSpeech2.md:9`).

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 10.

## Families

- **D (10), unknown signature, console answer unknown:**
  - All ten are parameterless declarations with no credible prototype (debt `:5`).
  - Most would need only a fixed failure. But the only "no engine" answer in the tree is libSceTextToSpeech2's EOPNOTSUPP, which that library's own debt calls a guess (`:9`), not a console result. So it is not C-real.
  - `GetStatusImpl` also writes an output through an argument whose position is unknown.
  - These are not A: the `*Impl` names and NIDs differ from the TextToSpeech2 exports, and no document says they behave the same.

## Batch proposals

None. No row is A, B or C-real.

## Dependency notes

- If a title imports these and fails without them, the cheapest step is to reuse libSceTextToSpeech2's EOPNOTSUPP convention. That needs either a console measurement or an explicit decision that the TextToSpeech2 debt entry covers both libraries. Even then, only the rows whose prototypes turn out to need no output (Open could return a handle) would become C-real. `GetStatusImpl` stays D until its output argument is known.
- A real speech engine would be a new third-party dependency, added as a submodule.
