# libSceSystemService (535 todo: A 2, B-S 14, B-L 1, C 59, D 459)

What it is: the title's gateway to the console shell. Besides the game-facing `sceSystemService*` calls it exports the SysCore launcher (`sceLncUtil*`), ShellCore (`sceShellCoreUtil*`), power-state manager (`sceSystemStateMgr*`), app messaging, CSB, coredump/report handlers and aliases of libSceUsbStorage. Most of these are IPC to system daemons used only by system software; their argument meanings, enums and error codes are not public, so they are D.

Families and decisions:
- App identity (GetAppId, GetAppTitleId, GetAppLaunchedUser, GetAppIdListOfBigApp, IsAppSuspended, IsAppLaunched, IsSpeculativeLaunch, GetUserFocus, GetAppEnterButtonAssign; SystemService and LncUtil twins): B-S over state already kept: `SYSTEM_SERVICE_RUNNING_APP_ID`, `GetAppTitleId_nid_postfix`, `USER_SERVICE_INITIAL_USER_ID`, param 1000. Other app IDs throw. App type/category/status enums are D.
- A: `sceLncUtilGetAppIdOfRunningBigApp` and `sceLncUtilLoadExec` forward to the implemented SystemService twins (Export.cpp:434, :238).
- Partly implemented exports (LoadExec B-L: run-time relinking of unconverted executables; KillApp x3, LaunchApp, ParamGetInt: D for the remaining paths).
- C real behaviour (27): absent features with a bool/OK answer: live streaming, Remote Play, Share Play, social screen, screen saver, system BGM, PS VR eye distance, ShellUI foreground, PS2 emulator, impose overlay, app move, USB mass storage, BD drive, suspend notification/blocking (AnyPS5 never suspends). Null out-pointers throw.
- C silent stubs (32): UI-only or host-uncontrolled no-ops modelled on existing neighbours (PowerTick, ShowControllerSettings, OpenTournamentOccurrence, Change*Clock, SetOutOfVrPlayZoneWarning): screen saver off, auto power-off/power lock, playback ticks, closed caption/eye-distance/store/tournament UI, error dialogs, clock/pstate setters, telemetry. Per CONTRIBUTING each needs a title it unblocks and a Silent stubs debt entry.
- D: LncUtil daemon/ShellUI/cdlg/CPU budget/coredump, ShellCore crash report, config downloads, storage maintenance, IDU, PSN/cloud, mic, telemetry, codec activation (HEVC/MPEG2), local processes, AppMessaging, SystemStateMgr, CsbMgr, report/coredump handlers, the unknown-signature exports listed in the debt file (except the no-op `sceSystemStateMgrTickVideoPlayback`), and the 17 USB storage aliases (their libSceUsbStorage targets are todo; implement there first).

New third-party dependencies: none. Everything non-D uses the C++ standard library and existing libSceUserService/libkernel AppMetadata state.

Batch proposals:
1. `feat(systemservice): answer app identity queries for the running title` (14 B-S + 2 A = 16): the B-S rows above plus the two LncUtil forwarders. Tests: extend `guest_system_service_tests` (core/libs/tests/GuestSystemService.cpp): app 1 round-trips title ID, user, list of one; other IDs and title IDs throw; IsAppLaunched true/false.
2. `feat(systemservice): report absent streaming, VR and shell features` (27 C real-behaviour rows): bool/OK answers with null-pointer throws. Tests: a new `guest_system_service_status_tests` executable registered in libSceSystemService/CMakeLists.txt that checks every answer and the null throws.
3. Silent stubs (32 C rows) only on demand: one small PR per title that needs one, each with its Silent stubs debt entry and a test that the call returns 0 and writes nothing.
