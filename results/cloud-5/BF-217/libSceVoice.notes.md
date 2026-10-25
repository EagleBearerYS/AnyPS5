# libSceVoice (6 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 2, D 4)

**What it is:** the voice-chat port library: initialization and start/stop, PCM, encoded-voice and device ports, port connections, read/write of port data, volume, mute, bitrate and voice-activity settings (sysmodule `0x0000001a`, `core/libs/prx/libSceSysmodule/ModuleTable.hpp:16`).

**Existing state** (`core/libs/prx/libSceVoice/Export.cpp`):
- Built: `libSceVoice/CMakeLists.txt:1` compiles `Export.cpp`.
- 30 done. All of them work on a software model: `Voice` (`:44-53`: initialized, started, app type, mute-all, a free-port queue, a port map and a connection set) behind `State()` (`:55`), and `Port` (`:34-42`: type, volume, bitrate, buffer size, frame size, muted, paused).
- Helpers: `RequirePort` (`:68`, throws for an uninitialized library or an unknown port), `IsInput` (`:75`, device, PCM and voice input types) and `VoiceFrame` (`:77-90`, the encoded-voice bitrates and their frame sizes).
- Error constants (`:25-29`): `NotInitialized` 0x804E0801, `InvalidPort` 0x804E0804, `InvalidArgument` 0x804E0805, `PortNotFound` 0x804E0807. Also 0x804E0802 (already initialized, `:99`), 0x804E0806 (memory size, `:130`), 0x804E0808 (resource full, `:171`), 0x804E080B (started, `:114`).
- No data flows. `sceVoiceReadFromOPort` (`:321-328`) always returns 0 bytes (`:326`), chat enable/disable throw (`:384-389`, `:424-429`), device attributes throw (`:318`), and Start does not run audio workers (debt `:23`). The debt says the host has no buffered voice processing (`:5`).
- Debt (`docs/dev/debt/libSceVoice.md`): the five unknown-signature rows are `:13-17`. Behaviour sources already accepted for this library: prosper (`:10`), shadPS4 at a808e76 and c1b2655 (`:11`, `:12`), RPCS3 and VitaSDK (`:12`, `:18`) and Orbital (`:18`).
- Test: `guest_voice_tests` (`core/libs/tests/GuestVoice.cpp`), built at `libSceVoice/CMakeLists.txt:6` and registered at `:10` (`add_test(NAME guest_voice ...)`). It asserts the zero-byte read at `GuestVoice.cpp:217`.

## The title row: `sceVoiceWriteToIPort` (I#9601, PPSA34502)

- **Declaration:** `Export.cpp:330-337`, `include/Api.hpp:22`: `(uint32_t inputPortId, const void* data, uint32_t* size, int16_t frameGaps)`. It throws today for every call, so PPSA34502 stops at its first write.
- **The prototype is settled.** Three public sources agree with the tree:
  - Orbital `db_types.json` line 2460 at cc408367 (https://github.com/AlexAltea/orbital/blob/cc408367da6a6ebbf5ac1ac559fefeadc73b2831/tools/ida/db_types.json): `int sceVoiceWriteToIPort(uint32_t ips, const void *data, uint32_t *size, int16_t frameGaps)`;
  - shadPS4 at c1b2655 (https://github.com/shadps4-emu/shadPS4/blob/c1b26558352b863f80142256e4df38585aa7d727/src/core/libraries/voice/voice.cpp#L565-L590);
  - VitaSDK `voice.h` (https://github.com/vitasdk/vita-headers/blob/ae5808f3105c42fa3373ba4ab64ab54e2e3ca848/include/psp2/voice.h#L446-L467), the Vita's documented contract.
- **What the contract says** (VitaSDK, Vita, not PS5):
  - it takes a PCM or encoded-voice input port only (`#L455-L456`);
  - `size` is in/out: requested bytes in, accepted bytes out (`#L449`, `#L458`);
  - `frame_gap` is ignored for PCM; for encoded input, a nonnegative value inserts missing frames and a negative one backfills (`#L449-L453`);
  - on a failure after argument validation, Vita FW 3.60 sets `size` to 0 (`#L462-L463`).
- **Why the real behaviour is out of reach:**
  - accepted bytes have to go somewhere: a port buffer, routed through connections to output ports and devices. The tree runs no voice workers and keeps no port data (debt `:5`, `:23`);
  - encoded-voice input needs a decoder for the bitrates in `VoiceFrame` (`:77-90`). No tree file or cited source has that codec;
  - the PS5's accepted count, and when it accepts less than `size`, are unmeasured.
  - So the full behaviour would be B-L for PCM ports (buffering, routing, mixing) and D for encoded ports.
- **What can be written now is a silent stub, so the row is C-silent:**
  - checks as the done twin `sceVoiceReadFromOPort` (`:321-328`): a null `data` or `size` throws `APS5_INVALID_ARG_EX` (`:322`); `RequirePort` throws for an uninitialized library or an unknown port (`:68-73`); an output port throws;
  - then return 0, leaving `*size` unchanged (all bytes accepted) and dropping the data.
  - That is what shadPS4 does (`voice.cpp#L583-L589`: a PCM or voice input port returns OK without touching `size`), and what both open claiming PRs do: #6090 (`refs/pull/6090` head 3d5d924cb2, `Export.cpp:199-206`) and #9829 (head c381981ab3, `Export.cpp:313-320`).
  - It agrees with the tree's own model: no output port yields data (`:326`), so nothing a title can read changes.
- **Stricter than the PRs, where the answer is unknown:**
  - a device input port: `IsInput` (`:75`) counts it as an input, but VitaSDK limits the call to PCM and voice inputs and shadPS4 answers ARGUMENT_INVALID (`voice.cpp#L583-L588`). The console's code is unknown, so it should throw. Both PRs accept it;
  - the not-initialized, out-of-range and missing-port codes: the neighbours that return codes (`GetBitRate` `:239-251`, `SetMuteFlag` `:339-348`) differ from the twin that throws (`ReadFromOPort`). Prosper gives module addresses only for Init, Start, CreatePort and GetPortInfo (debt `:10`), so throwing, as the twin does, is the honest choice.
- **Claims:** #6090 (open), #9826 (open) and #9829 (open) implement it; #3223, #3620, #4302, #7566 and #9753 are closed.

## Families

- **C-silent (2):**
  - `sceVoiceWriteToIPort`: see above. Title PPSA34502 (I#9601).
  - `sceVoiceVADAdjustment`: the debt says unknown signature (`:16`). But shadPS4 at c1b2655, the commit the debt already cites at `:12`, declares `(float, float)` and returns 0 with no checks (`voice.cpp#L551-L554`). Open #10702 changes the declaration to that.
    - No voice-activity detection runs (debt `:11`), so the only answer is a no-op 0 that reads no argument.
    - Needs a title.
- **D (4): no prototype.** `sceVoiceGetMemorySize` (`:435`, debt `:13`), `sceVoiceDeviceInCustomerData` (`:440`, `:14`), `sceVoiceQAEnableRecording` (`:445`, `:15`) and `sceVoiceVADPVTAdjustment` (`:455`, `:17`).
  - None is in shadPS4's `voice.cpp` at c1b2655 or in Orbital's `db_types.json` (its `sceVoice*` entries are lines 2415-2460).
  - GetMemorySize may report the mode-dependent sizes `sceVoiceStart` checks (`:129`: 0x40000 or 0xC0000), but with no argument list that is a guess.
  - #9753 (closed) removed all five unknown-signature rows.

## Batch proposal

There are no A, B or C-real rows. The two C-silent rows qualify only with a title:
- **`fix(libSceVoice): accept writes to input ports` (1 row, `Unblocks: PPSA34502`, I#9601)**: the stub above.
  - Record it under Silent stubs in `docs/dev/debt/libSceVoice.md`: data written to an input port is discarded and the full count reported as accepted.
  - Tests: extend `guest_voice_tests` (`GuestVoice.cpp`, next to the read at `:217`):
    - a write to the voice input port and to a PCM input port returns 0 and leaves `size` at the requested count;
    - a write to the PCM output port throws;
    - a write to a device input port throws;
    - null `data` and null `size` throw;
    - an unknown port and a call before `sceVoiceInit` throw;
    - `sceVoiceReadFromOPort` on the connected output port still returns 0 bytes after the write.
- `sceVoiceVADAdjustment` stays a throw until a title calls it.

## Caveats

- CONTRIBUTING allows a silent stub only when it unblocks a title and only affects the UI (`CONTRIBUTING.md:10`). Dropping voice frames affects voice chat, not the UI. The case for allowing it: voice output already does not exist here (zero-byte reads, throwing chat and device calls), so the stub changes nothing a title can observe except `size`. Reviewers should decide this explicitly.
- Leaving `size` at the requested count tells the title every byte was accepted. A console with a full port buffer would report fewer, and a title that paces itself on `size` may behave differently.
- The issue I#9601 itself could not be read from this session (GitHub access to `boykopovar/AnyPS5` is not enabled; the web page answers 403). The title, the port type it writes to and its arguments come only from the BF-188 and BF-208 title tables (`results/cloud-5/BF-188.md:140`, `results/cloud-2/BF-208.md:160`). Whether PPSA34502 writes PCM or encoded voice is not known here.
- `VADAdjustment`'s `(float, float)` comes from a PS4 emulator's declaration, not from a PS5 measurement.
