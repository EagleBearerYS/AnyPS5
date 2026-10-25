# libSceConvertKeycode (1 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 1)

**What it is:** conversion of USB keyboard keycodes to virtual keycodes and UTF-16 characters for each keyboard layout, plus the IME keyboard type of a user (sysmodule `0x000000d2`, `core/libs/prx/libSceSysmodule/ModuleTable.hpp:98`).

**Existing state** (`core/libs/prx/libSceConvertKeycode/Export.cpp`):
- Built: `libSceConvertKeycode/CMakeLists.txt:1` compiles `Export.cpp` and links libSceUserService (`:3`).
- 3 done: `sceConvertKeycodeGetImeKeyboardType` (`:2416`), `sceConvertKeycodeGetVirtualKeycode` (`:2423`) and `sceConvertKeycodeGetCharacter` (`:2438-2444`).
- Helpers: `CharacterLayout` (`:2305`), `CharacterEntry` (`:2335`) and `ConvertCharacter` (`:2340-2401`: shift, caps, num lock, IME and alternate modes, Korean and Arabic special cases).
- Errors: `kErrInvalidAddress` 0x80BC0031 (`:16`) and `kErrInvalidKeyboardType` 0x80BC001D (`:228`). Layouts 31-39 throw until the guest SDK version is exposed (debt `docs/dev/debt/libSceConvertKeycode.md:5`).
- `KeyboardData` (`core/libs/SceTypes.hpp:1234-1244`): timestamp, intercepted, connected, length, led, modifier_key, `key_code[KEYBOARD_MAX_KEYCODES]`.
- Tests:
  - `guest_convert_keycode_tests` (`core/libs/tests/GuestConvertKeycode.cpp`), built at `CMakeLists.txt:7` and registered at `:11`;
  - `guest_convert_keycode_title_tests` (`core/libs/tests/GuestConvertKeycodeTitle.cpp`), built at `:16` and registered six times at `:35`, once per title case (`:21`).

## Families

- **D (1): `sceConvertKeycodeGetCharacterFromKeyboardData`** (`:2433-2436`, `(const KeyboardData*, int32_t index, uint32_t state, uint32_t keyboardType, char16_t*)`).
  - The signature was recovered in the tree (commit 96c0eeb46b, boykopovar, 2026-10-20, "recover keyboard data conversion signature"). The body still throws, and no debt line or source records its semantics.
  - Unknown:
    - whether `state` is used alone or merged with `modifier_key` and `led` from the record (`SceTypes.hpp:1240-1241`);
    - whether `index` is bounded by `length` (`:1239`), by `KEYBOARD_MAX_KEYCODES` (`:1242`) or by both, and what an index outside them returns;
    - whether a null `keyData` gives `kErrInvalidAddress`, as a null output does in GetCharacter (`:2439`).
  - No public source has the function. A GitHub code search finds only AnyPS5 forks and symbol lists.

## Batch proposal

None. With one measured or documented rule for the three unknowns, the row becomes **B-S**:
- check pointers and `keyboardType` as `sceConvertKeycodeGetCharacter` does (`:2439-2441`);
- check `index`;
- return `ConvertCharacter(keyData->key_code[index], state, keyboardType)` (`:2340`).

Tests would extend `guest_convert_keycode_tests`: a US layout record with two keycodes, both indices, an out-of-range index, null pointers, and type 0 and type 40.

Claims (7): #6797, #9822, #9826 and #10437 are open; #3223, #4261 and #7079 are closed.
- #10437 (head in `refs/pull/10437`) is the only one written against main's signature. It forwards `key_code[index]` exactly as above, and answers `kErrInvalidAddress` for a bad index. That error choice has no stated basis.
- #9822 and #9826 keep a `(void)` declaration that returns a constant.
- #6797 uses the GetCharacter argument list.
- Those three predate the recovered signature.

## Caveats

- The B-S path assumes "FromKeyboardData" is GetCharacter applied to one keycode of the record. The record also carries timestamp and connection fields, and the function may read modifier state from it, so that assumption needs checking before implementation.
