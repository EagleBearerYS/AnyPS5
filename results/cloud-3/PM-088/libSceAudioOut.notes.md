# libSceAudioOut (86 todo: B-S 1, D 85)

What it is: the legacy `sceAudioOut*` port API (open, output, volume, HDMI bitstream ports) plus AudioOut2, the PS5 object/port mixer. Both play through SDL2, and 59 exports are done (`src/AudioOut.cpp`, `AudioOut2*.cpp`).

The todo rows are mostly the system-shell side of the audio service: volume and routing setters, Mbus bus routing, arbitration, process focus, 3D audio, HRTF, TV and VR controls, debug calls, and Ex/Pt variants of implemented calls. `docs/dev/debt/libSceAudioOut.md` lists every `src/Unimplemented.cpp` entry as "unknown signature".

Families (all D unless noted):
- System setters (12), Mbus (10), arbitration/3D/HRTF/TV/VR (13 + 2): private audio-service IPC. The "device absent" errors (HMD, VR controller) are not in the tree.
- Ex/Pt open and time variants (8), Ex create/init (4), `LoPortGetStateEx`, `GetSystemStateEx`: the extra layouts are unknown. Once signatures exist they can reuse `sceAudioOutGetLastOutputTime` (`AudioOut.cpp:548`), `sceAudioOut2LoPortGetState` (`AudioOut2Low.cpp:65`) and `sceAudioOut2GetSystemState` (`AudioOut2System.cpp:24`).
- Terminate, LoTerminate and ArbitrationInitialize (3): their effect on open objects is unknown. Also D: port registry/IPC/report hooks (6), debug (3), status queries (7), legacy `sceAudioOutMastering*` (4).
- HDMI mode/system/monitor info (9): the records and mode bits are unknown, and the implemented neighbours only zero their records.
- `sceAudioOutDeviceIdOpen`: the signature is known, but the device-ID space and the host mapping are undocumented.
- `sceAudioOut2GetAmbisonicsEncodeCoefficients`: close. The SN3D/ACN math exists (`Sn3d`, `AudioOut2SpeakerArray.cpp:39`); only the argument list is missing.
- B-S (1): `sceAudioOut2MasteringInit`. The debt file already records the PS5 module: 0x80268006 before `sceAudioOut2Initialize`, 0 when already initialized, 0x80268201 for nonzero flags.
  - It needs an AudioOut2-initialized flag (`sceAudioOut2Initialize` keeps none) and a mastering flag that `sceAudioOut2MasteringTerm` clears.
  - Term returns 0x80268203 without init, also from the debt.

Third-party: none. SDL2 is present, and the ambisonics math is in the tree.

Batch proposals: no batch of 10 exists among the A/B/C rows.
- One small PR, "AudioOut2 mastering lifecycle": `sceAudioOut2MasteringInit`, plus touching the implemented Term and Initialize.
  - Test: a unit test executable `audio_out2_mastering`, registered in `core/libs/prx/libSceAudioOut/CMakeLists.txt` like `audio_out_vibration_timeline`.
  - It covers init before Initialize, nonzero flags, a repeated init, and Term without Init.
- Further progress needs call-site signatures (`docs/dev/REVERSE_ENGINEERING.md`), most of all for the Ex/Pt variants, the Ex getters and the ambisonics encoder.
