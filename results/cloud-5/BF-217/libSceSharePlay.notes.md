# libSceSharePlay (16 todo: A 0, B-S 1, B-M 0, B-L 0, C-real 0, C-silent 1, D 14)

**What it is:** Share Play, where a visitor watches or plays the host's game over the network. Apps use init/term, connection info, events, prohibition and controller control. The module also exports system-side session, server and common-dialog notification calls.

**Existing state** (`core/libs/prx/libSceSharePlay/Export.cpp`, built by `CMakeLists.txt:1`):
- 4 done:
  - Initialize (`:17-23`) and Terminate (`:25-30`);
  - GetCurrentConnectionInfoA (`:32-40`);
  - ProhibitController (`:42-47`).
- State: `g_initialized` under `g_mutex` (`:11-12`). No service connection is modelled.
- Error constants (`:8-10`): INVALID_ARGS 0x810E0001, ALREADY_INITIALIZED 0x810E0003, NOT_INITIALIZED 0x810E0004.
- The A record: 72 bytes (`core/libs/SceTypes.hpp:1494-1504`).
- Debt:
  - `docs/dev/debt/libSceSharePlay.md:5` and `:6`: no credible prototypes for the 16 todo rows.
  - `:10-12`: init, the A record and ProhibitController are recovered from the native module. The A record starts zeroed with both user ids -1, and init is checked before the pointer.
- Test: `guest_share_play` (`core/libs/tests/GuestSharePlay.cpp`), built at `CMakeLists.txt:6` and registered at `:10`.

## Families

- **B-S (1): GetCurrentConnectionInfo.**
  - shadPS4 gives the non-A prototype and record (`https://github.com/shadps4-emu/shadPS4/blob/15abf68c9b52c4b04d094f2837719fdb871021f3/src/core/libraries/share_play/shareplay.h#L20-L30`): status, mode, two online ids, two user ids.
  - That is the A record without its two account ids: 56 bytes, with the user ids at 48 and 52.
  - The answer is the A form's recovered disconnected state:
    - NOT_INITIALIZED first;
    - then INVALID_ARGS for null;
    - else zeros with both user ids -1.
  - shadPS4 itself only zeroes the record, sets the status to DORMANT and returns OK (`shareplay.cpp#L17-L22`).
- **C-silent (1): SetProhibition.**
  - `(int mode)` from Orbital (`https://github.com/AlexAltea/orbital/blob/b321fcdbd700cc02da1cb1ad25412781d5ec73c6/tools/ida/db_types.json#L2100`).
  - Check init as the done siblings do (`:27`, `:34`, `:44`), then return 0 and drop the mode, since no Share Play service runs.
  - The valid modes and the service's answer are unmeasured, so this is a silent stub and needs a title.
- **D (14):**
  - Events and info (2): GetEvent and GetCurrentInfo. The records are unknown and there is no event queue.
  - Common-dialog notifications (3): NotifyDialogOpen, NotifyForceCloseForCdlg and ResumeScreenForCdlg.
  - Server lock and unlock (2).
  - Session control (6): SetMode, SetProhibitionModeWithAppId, Start/StopStandby and Start/StopStreaming.
  - CrashDaemon (1).
  - None has a prototype, and shadPS4 has only parameterless OK stubs for all of them, which are not console answers.

## Batch proposal

The one B-S row and the C-silent row are too few for a family PR. They fit one small change, `feat(libSceSharePlay): legacy connection info`, adding SetProhibition only if a title needs it.
- Semantics:
  - the A form and debt `:11`;
  - shadPS4's non-A layout;
  - Orbital's SetProhibition prototype.
- Tests: extend `guest_share_play_tests` (`core/libs/tests/GuestSharePlay.cpp`):
  - before init, GetCurrentConnectionInfo gives 0x810E0004 and leaves a 0x5a canary untouched, as `:50-52` checks for the A form;
  - after init, null gives 0x810E0001;
  - a filled record has status, mode and online ids 0 and user ids -1 at offsets 48 and 52, and byte 56 onward of a larger buffer is untouched;
  - SetProhibition gives 0x810E0004 before init and 0 after.
- Update debt `:5` and `:6`: GetCurrentConnectionInfo and SetProhibition now have cited prototypes.
- Claiming PR: 10526 (open) implements GetCurrentConnectionInfo.

## Caveats

- Debt `:5` and `:6` are stale for two names:
  - shadPS4 has a GetCurrentConnectionInfo prototype;
  - Orbital has SetProhibition's.
- The non-A layout is shadPS4's PS4 record. It matches the module-recovered A record, but the PS5 non-A form itself is not confirmed.
