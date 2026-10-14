# libSceNpTrophy2 technical debt

### Silent stubs

- [libSceNpTrophy2](../../../core/libs/prx/libSceNpTrophy2/src/GameInfo.cpp): `sceNpTrophy2RegisterUnlockCallback` accepts a callback that is never called, as the callback's signature and when the console calls it are not known, `sceNpTrophy2UnregisterUnlockCallback` only returns success, and `sceNpTrophy2ShowTrophyList` returns success without showing a list (there is no system trophy UI; seen on PPSA15552).

### Unknown function info

- [Query structures](../../../core/libs/prx/libSceNpTrophy2/include/NpTrophy2Types.hpp): a progress value is a 16-byte `{int32 type, 4 reserved bytes, uint64 value}` and the game, group and trophy structures are 152, 24, 152, 32, 1312 and 32 bytes, as [KytyPS5](https://github.com/KytyPS5/KytyPS5/blob/9e41d8320a2774b303fbad344c2347e8d0292e83/src/libs/libTrophy.cpp) asserts; no other public source confirms the layout. Progress type 1 marks a progressive trophy's target and progress, as in KytyPS5. Trophies without a `groupId` belong to group -1, KytyPS5's base game group.

### Functional

- [Trophy queries](../../../core/libs/prx/libSceNpTrophy2/src/TrophyInfo.cpp) report the title's [trophy data](../../../core/libs/prx/libSceNpTrophy2/src/TrophyPackage.cpp) (`sce_sys/trophy2/trophyNN.ucp`) and the unlocks in the [trophy store](../../../core/libs/prx/libSceNpTrophy2/src/TrophyStore.cpp). Without the file, or when it is malformed, they report the old placeholders and `sceNpTrophy2GetTrophyInfoArray` lists only trophy 0. Icons are not read. Hidden trophies report their names and descriptions; whether the console blanks them before the unlock is not known. `progress_percentage` weighs gold 6, silver 2 and bronze 1 and leaves the platinum out, as KytyPS5 does (the ratio of the published PSN trophy points); the console's formula is not known. A trophy or group that is not in the file throws. A context that was not created or was destroyed reports placeholders and no unlocks and returns success; the console's errors for it are unknown.
- [Text language](../../../core/libs/prx/libSceNpTrophy2/src/TrophyPackage.cpp): texts come from `tropmeta_<tag>.json` for the console language, then `en-US`, then the file's `defaultLanguage`. The tags for languages 0 to 29 are KytyPS5's; `uk-UA` for 30 (Ukrainian) is an assumption.
- [Platinum](../../../core/libs/prx/libSceNpTrophy2/src/TitleTrophies.cpp): an unlock through any path unlocks the platinum that the trophy names in `platinumTrophyId` once every trophy naming that platinum is unlocked, as KytyPS5 does; trophies without `platinumTrophyId` are not required. A platinum or a progressive trophy can still be unlocked directly with `_UnlockTrophy`, which KytyPS5 refuses; the console's behaviour is not known.
