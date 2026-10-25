# libSceIme (46 todo: C 5, D 41)

**What it is:** the in-game IME:
- a panel session: open, update, text, caret, close, filter;
- the IME keyboard (USB keyboard through the IME);
- plus system-only families: the VSH context API, the user dictionary, configuration, candidates and the module init/finalize exports.

**Existing state** (`core/libs/prx/libSceIme/Export.cpp`):
- 16 done.
- One `Session` (`:69`, `g_session` at `:87`) holds the parameters, text, caret, an event queue and host inputs. The SDL text input forwarded by `ImeTextInput` (debt `:5`) fills it.
- `sceImeUpdate` (`:532`) drains the queue and runs the filter.
- The keyboard is a per-user open set (`g_openKeyboards`, `:85`) that never reports a device (debt `:22-23`).
- Error constants are at `:26-49`, `PanelSize` is at `:91`, and the event ids follow shadPS4 (debt `:13`).
- Tests: `guest_ime_parameters_tests`, `guest_ime_keyboard_tests`, `guest_ime_filter_text_tests` and `guest_ime_open_tests` (`core/libs/tests/GuestIme*.cpp`). They are registered in `libSceIme/CMakeLists.txt` at `:10`, `:20`, `:30` and `:41`.

**Counts:** A 0, B-S 0, B-M 0, B-L 0, C 5 (all silent stubs), D 41.

## Families

- **C (5), silent stubs. They return 0 and keep nothing; each needs a title.**
  - `sceImeDisableController`: the IME already ignores the controller.
  - `sceImeSetConsecutiveClauseConversionEnabled`, `sceImeSetVoiceDictationEnabled` and `sceImeSetEditEventWithRemotePlayEnabled`: no conversion engine, no dictation and no Remote Play edits on this PC.
  - `sceImeKeyboardUpdate`: no keyboard event, with null-handler and empty-queue answers as `sceImeUpdate` (`:533`, `:568`). Whether a console first sends a keyboard-open event is unknown, so this is not "real behaviour".
- **D (41):**
  - VSH context API (25): `ImeVshContext` and the 256-byte `ImeVshParameter` are incomplete types (debt `:9`); the NotifyCurrentTextInfo pair semantics are open (debt `:26`).
  - User dictionary (6): 224-byte records, an 11200-byte GetWords output and an unidentified 64-bit selector (debt `:9`).
  - ConfigGet and ConfigSet (2): key semantics unresolved (debt `:15`).
  - CheckFilterText (contract unknown, debt `:12`), plus CheckUpdateTextInfo and CheckRemoteEventParam (repair rules not public, debt `:9`).
  - SetCandidateIndex and ConfirmCandidate (2): there is no candidate list.
  - KeyboardOpenInternal: how its separate handler relates to `KeyboardParam.handler` is unknown.
  - InitializeImeModule and FinalizeImeModule (2): the "pending exports" with no-parameter placeholder declarations (debt `:17`).

## Batch proposal

None: no A, B or C family reaches 6. The 5 silent stubs could go in one PR once a title needs them. Its tests would extend `guest_ime_open_tests` and `guest_ime_keyboard_tests`: each call returns 0, and `sceImeKeyboardUpdate` returns 0 without calling the handler while a keyboard is open.

## Caveats

- The VSH family is for system software: an app process does not normally reach it.
- If a title needs `sceImeKeyboardUpdate` to deliver a keyboard-open event, that event's layout (resource-id array, `SceTypes.hpp:1334`) is in the tree, but its id and timing are not.
