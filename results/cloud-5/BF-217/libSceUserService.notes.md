# libSceUserService (2 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 2)

**What it is:** console users: the login list, login and logout events, names, colours, age level, game presets, and accessibility and keyboard settings.

**Existing state** (`core/libs/prx/libSceUserService/Export.cpp`; built by `CMakeLists.txt:1`):
- 24 done.
- State (`:15-24`): `initialized`, `eventQueue`, and `userConnected` for four users, of which only the first is logged in.
- `ReportAccessibilitySetting` (`:42-51`) answers NOT_LOGGED_IN 0x80960009 before INVALID_ARGUMENT 0x80960005, then writes a value.
  - Six getters use it with the values a PS5 (13.60) reports for a user at defaults: 0, 0, 1, 1, 0 and 1 (`:127-159`; debt `docs/dev/debt/libSceUserService.md:11`).
- Errors: `UserService.hpp:14-25`.
- Tests:
  - `guest_user_service`: executable at `CMakeLists.txt:7`, registered at `:11`;
  - `guest_user_service_user_name`: `:19`, `:22`;
  - `guest_user_service_lifecycle`: `:30`, `:33`.
  - The six measured getters are checked at `core/libs/tests/GuestUserService.cpp:114-135`, and the two todo rows are asserted to throw at `:146-160`.

## Families

- **Accessibility getters with no measured default (2): D.**
  - Rows: GetAccessibilityKeyremapEnable (`UserService.hpp:38`) and GetAccessibilityZoom (`UserService.hpp:42`).
  - The `(userId, int32*)` declarations match the siblings.
  - But the 13.60 measurement (debt `:11`) covers only the six siblings. ZoomEnabled and ZoomFollowFocus (`Export.cpp:153`, `:157`) are different settings from Zoom.
  - A value would be a guess.

## Batch proposal

None.
- Claims:
  - PR 9641 and PR 9649 (closed) declared the header;
  - PR 9826 and PR 9828 (open) implement both rows. They must bring a console measurement of the default values, as debt `:11` did for the siblings.

## Caveats

- With a measured default, each row becomes a one-line B-S through `ReportAccessibilitySetting` (`:42`). The not-logged-in answer would then match the siblings, and the throw assertions at `GuestUserService.cpp:146-160` would turn into entries in the settings table at `:118-125`.
