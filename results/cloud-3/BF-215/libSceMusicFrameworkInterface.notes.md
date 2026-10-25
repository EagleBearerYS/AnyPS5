# libSceMusicFrameworkInterface (17 todo: D 17)

**What it is:** the client interface to the system music player: transport (play state, skip, loop, shuffle, current time), volume and mute, network state, sources, and synchronous or asynchronous messages.

**Existing state:**
- 0 done. All 17 exports are parameterless declarations that throw (`core/libs/prx/libSceMusicFrameworkInterface/Export.cpp:9-92`).
- Names and NIDs are in the PS5 3.20 export inventory, but no credible prototypes were found. The declarations only resolve imports (`docs/dev/debt/libSceMusicFrameworkInterface.md:5`).
- There is no CMakeLists or test. The library is built by the generic loop in `core/libs/CMakeLists.txt:159`.
- Related state: `sceSystemServiceDisableMusicPlayer` and `sceSystemServiceReenableMusicPlayer` (`core/libs/prx/libSceSystemService/Export.cpp:418`, `:445`) return `SYSTEM_SERVICE_OK` and keep no state; there is no music-player service.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 17.

## Families

- **D (17): unknown signature, and no service.**
  - Getters: GetCurrentTime, GetLoop, GetMuted, GetNetworkState and GetVolume.
  - State queries: IsEnded, IsPlaying and IsStopped.
  - Setters: SetCurrentTime, SetLoop, SetShuffle and SetVolume.
  - Transport: SkipNext and SkipPrevious.
  - Other: RemoveSrc and the two SendMessage calls.
  - The debt says no credible prototypes exist, and the tree models no music-player service whose state these would report.

## Batch proposals

- None: there are no A, B-S, B-M or C-real rows.

## Dependency notes

- Even with prototypes, the getters need a "no player / not playing" model, and the console's answers with no music playing would need measuring before any could become C-real.
