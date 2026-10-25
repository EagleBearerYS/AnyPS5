# libSceAppChecker (3 todo: A 0, B-S 0, B-M 0, B-L 0, C 0, D 3)

Upstream main 2742004ed2.

- **What it is (by export names only):** an application checker with Execute, ExecuteEx and Abort. Nothing in the tree says what it checks.
- **Existing state:** 0 done.
  - `core/libs/prx/libSceAppChecker/Export.cpp:9-22`: three parameterless declarations that throw. No state, constants or error codes.
  - Debt `docs/dev/debt/libSceAppChecker.md:5`: the names and NIDs come from the aerolib symbol database and are attributed to this module by symbol prefix; the library is absent from the PS5 3.20 export inventory, "so the pairing is not firmware-verified and no credible prototypes were found".
  - No CMakeLists.txt (generic rule, `core/libs/CMakeLists.txt:158-161`) and no guest test.
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 3.
- **B and C families:** none. There is no prototype, no constant and no described service, so no answer can be given, even a plain return.
- **D (3):** Abort, Execute, ExecuteEx, for the reason above.
- **Batch proposal:** none, because there is no A, B or C family.
- **Caveats:** even the module pairing is unverified, so a title importing these NIDs may expect a different library.
