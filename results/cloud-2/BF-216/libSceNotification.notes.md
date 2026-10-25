# libSceNotification (4 todo: C 4)

- **What it is:** PS5 system notifications from a title: a notification by type and message, or by id with a JSON payload, and the persistent banner on the PS-button (control centre) screen.
- **Existing state** (`core/libs/prx/libSceNotification/Export.cpp`):
  - 0 done. Send (`:8`), SendById (`:16`) and ShowPsButtonPersistentBanner (`:25`) take typed arguments and throw. HidePsButtonPersistentBanner (`:31`) returns void and throws.
  - The declarations come from commit 8f21bcbc98, which records no source. There is no `docs/dev/debt/libSceNotification.md`.
  - No state. Built by the generic `Export.cpp` rule (`core/libs/CMakeLists.txt:159`).
  - Nearest done neighbour: the PS4 `sceKernelSendNotificationRequest` (`core/libs/prx/libkernel/System/src/Notification.cpp:38`). It validates the 3120-byte request and writes `[notification] <message>` to stderr instead of showing UI.
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 4 (all silent stubs), D 0.
- **C (4), silent stubs, UI only:**
  - Send, SendById and ShowPsButtonPersistentBanner return 0 without reading their arguments. Hide does nothing.
  - The host has no notification or control-centre UI, so nothing a title could observe changes.
  - CONTRIBUTING allows these only with a title they unblock. None is recorded.
- **Batch proposal:** none (4 rows). No guest test executable exists. If a title appears, the four rows can ship together with a new `docs/dev/debt/libSceNotification.md` listing them under Silent stubs and recording the signature source.
- **Caveats:**
  - Logging the message the way the kernel path does would need the payload encoding (a C string, or JSON as SendById suggests). That is unknown, so the rows read nothing.
  - The missing debt file is itself a gap: the declared signatures have no recorded source.
