# libSceNpEulaDialog (9 todo: D 9)

**What it is:** the common dialog that checks and shows the PSN end-user licence agreement. It has the lifecycle (Initialize, Open, UpdateStatus, GetStatus, GetResult, Close, Terminate) plus CheckVersion and CheckVersionAbort.

**Existing state:**
- 0 done. `core/libs/prx/libSceNpEulaDialog/Export.cpp:9-52` holds nine parameterless declarations.
- No state, no CMakeLists.txt and no guest test.
- Debt `docs/dev/debt/libSceNpEulaDialog.md:5`: no credible prototypes. Orbital `db_types.json@b321fcdb` (cited at `docs/dev/debt/libSceNpFriendListDialog.md:5`) has no `sceNpEulaDialog` entry either.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 9.

## Families

- **D (5): lifecycle** (`Initialize`, `Terminate`, `Close`, `GetStatus`, `UpdateStatus`).
  - The common-dialog twins give the shape, for example `core/libs/prx/libSceNetCtlApDialog/Export.cpp:30-65`.
  - But the error codes are per library:
    - the common-dialog codes are 0x80B8xxxx (`core/libs/prx/libSceNetCtlApDialog/Export.cpp:16-21`, `core/libs/prx/libSceCommonDialog/CommonDialog.hpp:252`);
    - LoginDialog uses its own 0x8134xxxx (`core/libs/prx/libSceLoginDialog/Export.cpp:16-17`).
  - So this library's `NOT_INITIALIZED`, `ALREADY_INITIALIZED` and `NOT_RUNNING` answers are unknown.
  - The status the two getters return comes from `Initialize` and `Open`, which stay unknown.
- **D (2): `Open` and `GetResult`.**
  - Their parameter and result layouts are unknown.
  - The twins' layouts differ, so none can be borrowed: 120-byte and 104-byte parameters (`CommonDialog.hpp:27`, `:96`), 16-byte and 40-byte results (`:79`, `:99`).
- **D (2): `CheckVersion` and `CheckVersionAbort`.**
  - A PSN EULA version check needs a signed-in user. The tree has none: `sceNpGetOnlineId` answers `SIGNED_OUT` (`core/libs/prx/libSceNpManager/Export.cpp:815-819`).
  - The request handle and result encoding are unknown.

## Batch proposals

None.

## Caveats

If a prototype source confirms two things:
- void lifecycle signatures;
- the 0x80B8xxxx common-dialog error base;

then the five lifecycle rows become B-S on the twin `g_status` state machine:
- `Initialize` gives INITIALIZED;
- `Close` gives `NOT_RUNNING`, since `Open` cannot run;
- `Terminate` gives NONE.

They would need a new `CMakeLists.txt` and guest test, modelled on `core/libs/prx/libSceSulphaDrv/CMakeLists.txt:1-15`.
