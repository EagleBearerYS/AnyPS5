# libSceAutoMounterClient (6 todo: A 0, B-S 0, B-M 0, B-L 0, C 3, D 3)

Upstream main 2742004ed2.

- **What it is:** the client of the system auto-mounter for USB storage: initialize a client (with a thread attribute), list the mounted USB devices by name, read one device's record, register and unregister for mount events, terminate.
- **Existing state:** 0 done.
  - `core/libs/prx/libSceAutoMounterClient/Export.cpp:17-43`: all six throw. No state and no error constants.
  - `include/Api.hpp:5`: the 944-byte `AutoMounterUsbDeviceInfo` (128-byte name, 22 opaque bytes, three 256-byte arrays with 64-bit lengths), pinned at `Export.cpp:7-13`; debt `docs/dev/debt/libSceAutoMounterClient.md:6`.
  - Debt `:5`: RegisterCallback, Term and UnregisterCallback have no credible prototypes and are declared without parameters only to resolve imports.
  - No CMakeLists.txt (generic rule, `core/libs/CMakeLists.txt:158-161`) and no guest test.
- **Neighbour:** libSceUsbStorage models the same absent device. "No USB mass-storage device is ever attached": `sceUsbStorageGetDeviceList` writes a count of 0 (`core/libs/prx/libSceUsbStorage/Export.cpp:89`, debt `docs/dev/debt/libSceUsbStorage.md:17`); `sceUsbStorageInit`/`Term` return 0 as silent stubs (`:11`, `:16`, debt `:5`); its callback registrations throw after validation (`:20`, `:32`, debt `:9`).
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 3 (1 real behaviour, 2 silent stubs), D 3.
- **C families:**
  - Real behaviour (1): GetUsbDeviceList writes `*count = 0` and returns 0, leaving the names untouched, as UsbStorage's list does. Null arguments have no known AutoMounter error code, so they keep throwing.
  - Silent stubs (2): Init returns 0 without its client thread; Term returns 0. Each needs a title that imports them; none is recorded.
- **D families (3):**
  - GetUsbDeviceInfo: the record is known, but every name is unknown here and the not-found error is not.
  - Register/UnregisterCallback: parameterless, no prototype; even the typed UsbStorage neighbours throw here.
- **Batch proposal:** none, because no family reaches 6 rows. The three C rows fit one small PR, `feat(libSceAutoMounterClient): report no USB storage`, once a title calls them.
  - It would need a new `core/libs/prx/libSceAutoMounterClient/CMakeLists.txt` with a guest test, registered as `libSceDiscMap/CMakeLists.txt:6,10` does: Init(nullptr) and Term return 0; GetUsbDeviceList sets a count of 7 to 0 and leaves a filled names buffer untouched.
- **Caveats:**
  - The real-behaviour row rests on the neighbour's model, which is itself the tree's no-passthrough choice, not a console measurement.
  - Whether `count` is also an input capacity is unknown; writing 0 is right either way.
  - Whether the console requires Init before GetUsbDeviceList, and its error if not, is unknown; the C row ignores the init state.
