# libSceNpEntitlementAccess (4 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 3, C-silent 0, D 1)

**What it is:** PSN entitlements for the running title: add-on (DLC) ownership, SKU, trial and PFT flags, entitlement keys, and asynchronous request/poll calls for unified, service and consumable entitlements, including consumption.

**Existing state** (`core/libs/prx/libSceNpEntitlementAccess/Export.cpp`; no CMakeLists, built by `core/libs/CMakeLists.txt:159`):
- 21 done.
- Add-ons are read from `anyps5-entitlements.ini` next to the executable, or from `ANYPS5_ENTITLEMENTS` (`:28-52`, `OwnedAddons` `:54`). GetAddcontEntitlementInfo (`:85`) and InfoList (`:97`) answer from that file.
- Fixed answers:
  - GetSkuFlag writes FULL 3 (`:106`);
  - GetPftFlag and GetGameTrialsFlag write 0 (`:169`, `:176`);
  - GetEntitlementKey returns no-entitlement 0x817D0007 (`:112`);
  - Initialize returns 0 (`:122`).
- Every request and poll returns `SCE_NP_ERROR_SIGNED_OUT` 0x80550006 without reading its arguments:
  - polls `:141`, `:153`, `:157`, `:183`, `:187`;
  - requests `:145`, `:149`, `:161`, `:165`, `:191`, `:195`.
- AbortRequest and DeleteRequest count as done, but throw on every call (`:128-134`).
- Errors: PARAMETER 0x80558003, NOT_FOUND 0x80558007, SIGNED_OUT 0x80550006 (`:20-22`), plus the KytyPS5 range 0x817D0002/0x817D0007 (`:116-117`).
- Debt `docs/dev/debt/libSceNpEntitlementAccess.md`:
  - `:5` the four todo names: no credible prototypes;
  - `:7` the request/poll argument lists are restored, but "the existing signed-out responses do not reproduce native initialization, validation or asynchronous request handling";
  - `:11` trial and PFT flags.
- Test: `guest_entitlements_tests` (`core/libs/tests/GuestEntitlements.cpp`), built at `core/libs/CMakeLists.txt:572`, registered as `guest_entitlements` and `guest_entitlements_missing` at `:576-577`. The input lists none. It covers the flags and add-ons, not the SIGNED_OUT family.

## Families

- **Consumable / consume request and poll (3, C-real).** Each is the SIGNED_OUT answer that its done siblings give without reading an argument, so the parameterless declaration can give it:
  - RequestConsumableEntitlementInfo, as RequestService/UnifiedEntitlementInfo (`:161`, `:165`);
  - PollConsumableEntitlementInfo, as PollService/UnifiedEntitlementInfo (`:153`, `:157`);
  - RequestConsumeEntitlement, as RequestConsumeUnified/ServiceEntitlement (`:145`, `:149`).
- **GetAddcontEntitlementInfoIndividual (D):** its done twin answers from the label argument (`:85-95`), so a declaration without parameters cannot give it. What the Individual form adds (another argument, a different record) is unknown.

## Batch proposal

**"npentitlementaccess: signed-out consumable and consume requests" (3: the C-real rows).**
- Semantics: the done request/poll siblings above. The declarations stay parameterless, and debt `:5` is updated to say they answer SIGNED_OUT without reading arguments.
- Tests: extend `guest_entitlements_tests` (`core/libs/tests/GuestEntitlements.cpp`, `core/libs/CMakeLists.txt:572`, `:576`) so that each of the three returns 0x80550006. Assert the existing siblings at the same time, since the family has no test today.
- Claims: PR 8646 (open) implements all three; run `tools/pr_overlap.py` first. PR 6361 (closed) declared them and PR 9753 (closed) removed them.

## Caveats

- SIGNED_OUT is the tree's offline model, not a console measurement (debt `:7`). A console would probably validate arguments or check initialisation first.
- Polls on ids that no request ever issued answer SIGNED_OUT rather than an id error, matching the siblings.
- If a prototype is found later, the null-pointer and id checks will need their own sources.
- AbortRequest and DeleteRequest are counted done but always throw (`:129`, `:133`).
