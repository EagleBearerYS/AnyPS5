# libSceAudio3d (7 todo: D 7)

**What it is:** 3D audio ports: initialize, open a port, reserve objects and set their attributes, write beds, advance and push mixed frames to AudioOut, and helper AudioOut ports and speaker arrays.

**Existing state** (`core/libs/prx/libSceAudio3d/Export.cpp`):
- 25 done. One port, `g_port` (`:77`, `Audio3dPort` `:61`), with an object set, a bed queue and a mixed-frame queue; `g_initialized` is at `:76`.
- Beds are mixed to stereo (`Mixer.hpp`) and pushed to a type-126 AudioOut port. Speaker arrays forward to AudioOut2 (`:474-495`).
- Object attributes are validated but not interpreted, a silent stub with a debt entry (`docs/dev/debt/libSceAudio3d.md:5`).
- Errors are at `:22-27`.
- Tests (`core/libs/prx/libSceAudio3d/CMakeLists.txt`): `guest_audio3d_parameters` (`:7`/`:11`), `guest_audio3d_port` (`:16`/`:20`), `guest_audio3d_speaker_array` (`:25`/`:29`) and `guest_audio3d_mixer` (`:34`/`:39`).

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 7.

## D families

All seven are `(void)` declarations recorded as "unknown signature" (debt `:16-22`, `Export.cpp:437-470`).
- **Port variants (4):** PortOpenEx, PortCreate, PortDestroy, PortGetStatus.
  - With prototypes, the first three would likely sit on `sceAudio3dPortOpen` (`:213`) and `sceAudio3dPortClose` (`:174`).
  - But how they differ is not in the tree, and the library models only one port.
- **Report handlers (2):** the handler prototype and the reported events are unknown.
- **StrError (1):** the console's strings are not in any source.

## Batch proposal

None.

## Caveats

None beyond the missing prototypes.
