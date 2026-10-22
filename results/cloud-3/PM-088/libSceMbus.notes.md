# libSceMbus (PM-088 classification)

- **What it is:** the client of the system "multimedia bus" service on the ShellCore side. It covers the device database and routing, control arbitration between applications, handle registration for the pad, audio, camera and HMD libraries, user and device binding, BGM, chat and audio routing, an event queue, and a large `sceMbusDebug*` and `sceMbusDump*` surface.
- **Done so far:** the same module exports libSceDeviceService. Its 5 done functions (Initialize, Terminate, GetEventState, QueryDeviceInfo_, GetGeneration) follow the no-device path. `sceMbusAcquireControlList` and `sceMbusTerm` are also done, returning constants read from the module. 7 done, 113 todo.
- **Counts:** A 0, B-S 1, B-M 0, B-L 0, C 0, D 112.
- **B-S:** `sceMbusEventCallbackFuncsInit_`. The debt says it only clears memory and has no defined return value. It clears the 16-byte `MbusEventCallbacks` table (`PendingExports.cpp:23`).
- **D:** everything else.
  - Most of it is private IPC with the mbus service. The debt lists the control values, registration fields, the 256-, 264- and 396-byte records, the event layout and the process and priority info as unresolved.
  - Debug-only calls: Debug* (27) and Dump* (11).
  - The largest remaining families are the device database (16), audio routing (12) and handle registration (11).
  - `sceMbusInit`'s answer in a game process is unmeasured. The libScePad debt records a ShellCore request over Mbus returning `0x809200ff` in a non-game process.
- **No C rows:** no "service absent" error constant for sceMbus* is in the tree or a public source.
- **On this PC** there is no mbus service. The host libraries (pad, HMD, HMD2, audio) never call into libSceMbus, so these exports are reached only by a title or title-shipped module that imports them directly. None is known in the tree.
- **Dependencies:** none.
- **Batches:** none. There is a single B row, and on its own it is useless without `sceMbusEventCreate_`/`EventReceive`. If a title is found importing a sceMbus* family, first probe the console's no-device answers for that family with a payload, as was done for libSceHmd2 (hmd2n). That would turn its rows into C, and the batch would carry a guest test in `guest_mbus_tests` (`core/libs/tests/GuestMbus.cpp`, registered in `libSceMbus/CMakeLists.txt`).
