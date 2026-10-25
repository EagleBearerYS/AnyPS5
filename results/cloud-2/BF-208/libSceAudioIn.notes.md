# libSceAudioIn (28 todo: D 28)

**What it is:** microphone capture ports (open, input, close, gain and state), plus the system side of audio input: device-id opens, routing and port statuses, gain/mute/mode controls, Spark headset processing, and the virtual microphone (Vmic).

**Existing state** (`core/libs/prx/libSceAudioIn/Export.cpp`):
- 14 done.
- 8 ports (`Port` at `:35`, `g_ports` at `:46`). Each opens its own SDL capture device (`openDevice` `:60`, `openPort` `:90`), and `sceAudioInInput` paces and fills blocks (`:130`).
- Argument checks are at `:82`. HqOpen and AsyncOpen use the measured PS5 wrapper checks (debt `:43`).
- Errors are at `:17-24`.
- `sceAudioInSetCompressorPreGain` and `sceAudioInSetNoiseGateThreshold` return 0 as the PS5 12.00 module does (debt `:23-24`).
- Test: `guest_audio_in_tests` (`core/libs/tests/GuestAudioIn.cpp`), built at `libSceAudioIn/CMakeLists.txt:8` and registered at `:12` (capture) and `:13` (no device).

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 28.

**D families.** Every todo row is a parameterless declaration recorded as "unknown signature" (debt `:9-38`), and no public prototype is in the tree:
- device-id and device opens (5): DeviceIdOpen, DeviceIdAsyncOpen, DeviceIdHqOpen, DeviceHqOpen, DeviceOpenEx. With a signature they could reuse `openPort` the way `sceAudioInOpenEx` forwards (`:262`), but the device-id space and its host mapping are undocumented;
- routing and status (7): SetConnections, SetPortConnections, SetPortStatuses, SetDevConnection, GetHandleStatusInfo, CountPorts, IsSharedDevice;
- gain, mute and mode (8): SetUsbGain, SetMicLevel, SetDeviceMuteStatus, SetUserMute, SetMode, SetMode2, ExtCtrl, ChangeAppModuleState;
- Spark (2), Vmic (5), and Inputs (a multi-port Input whose argument list is unknown).

**Batch proposal:** none. Progress needs call-site signatures. The device-id opens come first, because they map onto the existing port table.

**Caveat:** some setters may also be constant-success in the module, as CompressorPreGain and NoiseGateThreshold were. That holds only after the same module measurement, so none is classed C here.
