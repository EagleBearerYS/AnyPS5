# libSceKeyboard (18 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 1, D 17)

**What it is:** USB/Bluetooth keyboard input for apps (init, open, read, state, key-to-character). The module also exports the device-service calls (ports, devices, device info, process focus and privilege, Mbus, pad emulation) and the `sceDbgKeyboard*` debug-keyboard forms.

**Existing state:**
- Built by `core/libs/prx/libSceKeyboard/CMakeLists.txt:1-4`, which compiles `Export.cpp` and `src/keyboard_impl.cpp`.
- 8 done (`Export.cpp:7-34`, `:51`): Init, Open, Close, Read, ReadState, GetConnection, GetKey2Char and DisconnectAll forward to `Keyboard::*` (`src/keyboard_impl.cpp:104-210`).
- State (`src/keyboard_impl.cpp`):
  - one modelled keyboard: `initialized`, `opened` and `connected` (`:14-16`);
  - a 16-entry history ring (`:11`);
  - modifiers and keys.
- Open accepts only `type 0, index 0` and does not keep the user id (`:110-120`). The single handle is `KEYBOARD_HANDLE` 1 (`include/keyboard_structs.h:14`).
- Errors: INVALID_ARG 0x80DA0001, INVALID_HANDLE 0x80DA0003, ALREADY_OPENED 0x80DA0004, NOT_INITIALIZED 0x80DA0005 (`include/keyboard_structs.h:9-12`). There is no no-handle error.
- Debt `docs/dev/debt/libSceKeyboard.md`:
  - `:5` GetKey2Char: still throws for JIS with the Kana LED;
  - `:9` Dbg forms: "no credible prototypes";
  - `:10` GetDeviceInfo (168 bytes) and SetProcessFocus: layouts unresolved;
  - `:11` Connect/DisconnectPort and DeviceOpen: widths only;
  - `:12` PadEmulate: signatures unresolved.
- No debt line names DebugGetDeviceId, DisconnectDevice, GetHandle, MbusInit or SetProcessPrivilege. Commit 8c411544eb typed them from the "supplied keyboard library" and dropped their old debt entry.
- Test: `keyboard_tests` (`core/libs/prx/libSceKeyboard/tests/Keyboard.cpp`), built at `CMakeLists.txt:10`, registered as `keyboard_input` at `:17`.
- Public source checked: Orbital `db_types.json` at b321fcdbd7 `#L587-L592` gives all six `sceDbgKeyboard*` prototypes. It has no `sceKeyboard*` entry.

## Families

- **sceDbgKeyboard* (6, D).** Orbital's prototypes have exactly the shapes of the done Init, Open, Close, Read, ReadState and GetKey2Char (`Export.cpp:7`, `:11`, `:16`, `:20`, `:24`, `:32`). Debt `:9` is stale on that point. They are still not A:
  - the data and character records are unnamed structs in Orbital;
  - nothing says whether the debug keyboard shares the sceKeyboard device, handle and state. Forwarding would make a title that opens both get ALREADY_OPENED;
  - nothing says whether a retail console answers at all.

  These are the strongest lead in this library: a source showing that the Dbg names reach the same code or record layout would make them A.
- **Device service (8, D):**
  - ConnectPort, DisconnectPort and DeviceOpen: debt `:11`;
  - DebugGetDeviceId and DisconnectDevice: no device-id space, since the host models one keyboard;
  - GetDeviceInfo and SetProcessFocus: debt `:10`;
  - MbusInit: no prototype source. scePadMbusInit and sceMouseMbusInit are todo, so no neighbour answer.

  The pad and mouse twins of these calls are all todo as well.
- **GetHandle (D):** by analogy with `scePadGetHandle` (`core/libs/prx/libScePad/Export.cpp:179`) it returns the open handle, or a no-handle error. The keyboard has no such error constant, the opener's user id is not kept, and the prototype has no recorded source.
- **PadEmulateOpen and PadEmulateClose (2, D):** debt `:12`.
- **SetProcessPrivilege (C-silent):** a no-op returning 0, as the done `scePadSetProcessPrivilege` (`core/libs/prx/libScePad/Export.cpp:246`). That pad stub is itself unmeasured and not listed under Silent stubs in `libScePad.md`. Needs a title (`CONTRIBUTING.md:10`).

## Batch proposal

None. The only non-D row is one silent stub, which waits for a title. If one appears:
- record it under Silent stubs in `docs/dev/debt/libSceKeyboard.md`;
- extend `keyboard_tests` with a check that `sceKeyboardSetProcessPrivilege(0)` and `(1)` return 0 and leave Open/Read behaviour unchanged.

## Caveats

- A done function still throws: `sceKeyboardGetKey2Char` reaches `NotImplemented_nid_no_patch` for arrangement 106 with the Kana LED (`src/keyboard_impl.cpp:182-185`, debt `:5`).
- The debt file has no entry for five typed-but-throwing rows (DebugGetDeviceId, DisconnectDevice, GetHandle, MbusInit, SetProcessPrivilege).
- No claims.
