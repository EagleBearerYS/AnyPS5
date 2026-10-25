# libSceAppContent (16 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 8, C-silent 0, D 8)

**What it is:** a title's app-content services: its app parameters, the temporary (`/temp0`) and download (`/download0`) data areas, and additional-content (DLC) queries, mounts and downloads.

**Existing state** (`core/libs/prx/libSceAppContent/Export.cpp`, built at `core/libs/prx/libSceAppContent/CMakeLists.txt:1`):
- 16 done. Every call checks `initialized` first (`:32`, e.g. `:73`). `sceAppContentInitialize` (`:147-160`) carries the recovered SDK 0x01500000 checks (debt `:12`).
- Errors (`:17-22`): NOT_INITIALIZED 0x80D90001, PARAMETER 0x80D90002, BUSY 0x80D90003, NOT_MOUNTED 0x80D90004, NOT_FOUND 0x80D90005, DRM_NO_ENTITLEMENT 0x80D90007.
- No additional content is ever installed (debt `:14`):
  - AddcontMount and AddcontUnmount answer NOT_FOUND (`:72-84`);
  - GetAddcontInfo answers DRM_NO_ENTITLEMENT (`:86-92`);
  - GetAddcontInfoList reports 0 entries and needs `hitNum` (`:94-100`);
  - EnqueueDownload, Delete and GetAddcontDownloadProgress answer NOT_FOUND (`:208-230`).
  - The label padding is checked only for titles built with SDK 0x01500000 or later (Mount, Info, EnqueueDownload) or 0x03500000 or later (Delete, Progress), through `validEntitlementLabel` (`:42-44`).
- App params: SKU flag FULL and user params 1-4 (`:102-115`). Temporary data (`:46-61`, `:162-206`). Download data as a host directory with a quota (`:117-145`).
- Debt: `:5-6` download data; `:9` the 16 pending exports ("signatures and behaviour are unverified", no-parameter declarations); `:12` initialization; `:14` additional content, citing dlc_emu at fcf69b16 and saying it is not a console measurement; `:15` temporary data. The input gives no debt line for any row, but `:9` covers all 16.
- Test: `guest_appcontent_tests` (`core/libs/tests/GuestAppContent.cpp`), built at `CMakeLists.txt:6` and registered at `:10`. It checks:
  - pre-init answers (`:26-39`);
  - add-content answers (`:111-132`);
  - info (`:191-207`);
  - list (`:215-230`).
- Source read for this pass: [dlc_emu `dlc_content.h`](https://github.com/drakmor/dlc_emu/blob/fcf69b16d46a9850578bce19dc2c1b4dc867cbb5/src/dlc_modules/dlc_content.h) and [`dlc_content.cpp`](https://github.com/drakmor/dlc_emu/blob/fcf69b16d46a9850578bce19dc2c1b4dc867cbb5/src/dlc_modules/dlc_content.cpp), the commit the debt already cites. It is a replacement for the console's AppContent and NpEntitlementAccess modules (`dlc_content.h#L4`), and it declares all 16 todo names (`#L74-L140`).

## Families

- **C-real (8): add-content forms that answer locally in dlc_emu.** With no matching entry, each one answers what the done siblings answer for every label.
  - **By entitlement id** (a `const char*` string):
    - `AddcontMountByEntitlemetId` (`h#L88-L89`, `cpp#L2125-L2140`) gives PARAMETER or NOT_FOUND, as AddcontMount does (`:72-78`);
    - `AddcontEnqueueDownloadByEntitlemetId` (`h#L97`, `cpp#L2192-L2199`) gives PARAMETER or NOT_FOUND, as EnqueueDownload does (`:208-214`);
    - `GetAddcontInfoByEntitlementId` (`h#L74-L76`, `cpp#L2040-L2055`) gives PARAMETER or DRM_NO_ENTITLEMENT, as GetAddcontInfo does (`:86-92`).
  - **By label:**
    - `AddcontEnqueueDownloadSp` (`h#L95-L96`). Its dlc_emu body is the same as EnqueueDownload's (`cpp#L2172-L2190`). It is not A: nothing shows Sp is an alias, only that both answer NOT_FOUND when nothing is queued.
    - `AddcontShrink` (`h#L98-L99`, `cpp#L2201-L2209`), as AddcontDelete (`:216-222`).
    - `GetEntitlementKey` (`h#L82-L84`, 16-byte key `h#L41-L46`, `cpp#L2089-L2100`) gives PARAMETER or DRM_NO_ENTITLEMENT and leaves the key untouched. [shadPS4](https://github.com/shadps4-emu/shadPS4/blob/68818c769a2545d1071fb97e83e3a979b244f324/src/core/libraries/app_content/app_content.cpp#L329-L357) has the same three arguments and answers. The NpEntitlementAccess twin answers the same way in its own error range (`core/libs/prx/libSceNpEntitlementAccess/Export.cpp:112-120`).
  - `GetAddcontInfoListByIroTag` (`h#L77-L81`, `cpp#L2058-L2086`, which ignores the tag) reports 0 entries, as GetAddcontInfoList does (`:94-100`).
  - `GetPftFlag` (`h#L131`, `cpp#L2343-L2347`) writes PFT_FLAG_OFF 0 (`h#L35-L39`). The tree already reports a full game:
    - SKU flag FULL (`:106-108`);
    - done `sceNpEntitlementAccessGetPftFlag` writes 0 (`core/libs/prx/libSceNpEntitlementAccess/Export.cpp:169-174`, `docs/dev/debt/libSceNpEntitlementAccess.md:11`).
- **D (8): calls that dlc_emu forwards to the real service over IPC**, so it gives a prototype but no local answer:
  - `AppParamGetString` (`h#L137-L139`, `cpp#L2372-L2377`; also SharpProspero [`AppContent.cs#L96`](https://github.com/SvenGDK/SharpProspero/blob/5dfdb4b0b166e83dc9ae275001afb695f12c7174/src/SharpProspero/Interop/AppContent/AppContent.cs#L96)). Which ids hold strings and what a short buffer answers are unknown, and there is no string accessor in `AppMetadata.hpp:25-33`.
  - `Download0Expand`, `Download0Shrink`, `Download1Expand`, `Download1Shrink` (`h#L123-L126`, `cpp#L2312-L2326`). dlc_emu declares a mount-point pointer but sends it as a 64-bit handle (`cpp#L597-L601`), so even the prototype is unsettled.
  - `GetRegion` and `GetDownloadedStoreCountry` (`h#L135`, `h#L140`): 4-byte console values.
  - `RequestPatchInstall` (`h#L136`): the path is at most 0xff (`cpp#L1612-L1614`); the answer with no patch installer is unmeasured.

## Batch proposal

**"feat(libSceAppContent): entitlement-id, Sp, shrink, key, IRO-list and PFT-flag answers" (the 8 C-real rows).**
- Declarations: replace the 8 `(void)` declarations (`Export.cpp:231-299`, `include/Api.hpp:27-40`) with dlc_emu's prototypes.
- Semantics:
  - check `initialized` first, as every sibling does;
  - a null pointer gives PARAMETER;
  - then the sibling's answer: NOT_FOUND, DRM_NO_ENTITLEMENT, or 0 entries;
  - GetPftFlag writes 0.
- Label padding:
  - a title built before 0x01500000 accepts it, since both sibling gates agree;
  - a title built with 0x03500000 or later gets PARAMETER, since both gates agree;
  - between the two, throw: the gate for Sp, Shrink and Key is unknown.
- Tests: extend `guest_appcontent_tests` (`GuestAppContent.cpp`, `CMakeLists.txt:6`, `:10`):
  - each of the 8 returns 0x80D90001 before `sceAppContentInitialize`, next to `:26-39`;
  - MountByEntitlemetId and EnqueueDownloadByEntitlemetId return 0x80D90005, or 0x80D90002 for a null argument;
  - GetAddcontInfoByEntitlementId returns 0x80D90007 and leaves a canary info untouched;
  - EnqueueDownloadSp and Shrink return 0x80D90005 for the label at `:111` and 0x80D90002 for null;
  - GetEntitlementKey returns 0x80D90007 with a canary key untouched, and 0x80D90002 for a null key;
  - ByIroTag gives `hitNum` 0 in count and list modes, and 0x80D90002 for a null `hitNum`;
  - GetPftFlag writes 0 over a canary and returns 0, and gives 0x80D90002 for null.
- Debt: shrink `:9` to the 8 D names, and add the dlc_emu prototypes to `:14`.
- Claims:
  - PR 8747 (closed) only declared these names;
  - PR 9638 (open) implements `sceAppContentGetEntitlementKey`. Compare its answers with the above and run `tools/pr_overlap.py` before a second version lands.

## Caveats

- dlc_emu is a replacement module, not a console measurement (debt `:14`). Its prototypes have to match what titles pass, but its checks are its own:
  - it never checks initialization;
  - it ignores the SDK version for padding;
  - its list calls accept a null `hitNum` in list mode (`cpp#L1993-L2019`, `#L2058-L2086`), where the tree's GetAddcontInfoList returns PARAMETER (`:97`, test `:227`).
  - The proposal follows the tree.
- Whether GetPftFlag needs `sceAppContentInitialize` is unknown. Neither dlc_emu nor the NpEntitlementAccess twin checks.
- `Entitlemet` is the export's own spelling; dlc_emu uses it too, and shadPS4 spells it `EntitlementId` (`app_content.cpp#L670-L680`).
