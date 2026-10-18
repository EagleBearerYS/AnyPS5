# libSceNpTrophy2 technical debt

### Silent stubs

- [libSceNpTrophy2](../../../core/libs/prx/libSceNpTrophy2/src/GameInfo.cpp): `sceNpTrophy2RegisterUnlockCallback` accepts a callback that is never called, as no trophy is ever unlocked, `sceNpTrophy2UnregisterUnlockCallback` only returns success, and `sceNpTrophy2ShowTrophyList` returns success without showing a list (there is no system trophy UI; seen on PPSA15552).

### Unknown function info

- [Query structures](../../../core/libs/prx/libSceNpTrophy2/include/NpTrophy2Types.hpp): a progress value is a 16-byte `{int32 type, 4 reserved bytes, uint64 value}` and the game, group and trophy structures are 152, 24, 152, 32, 1312 and 32 bytes, as [KytyPS5](https://github.com/KytyPS5/KytyPS5/blob/9e41d8320a2774b303fbad344c2347e8d0292e83/src/libs/libTrophy.cpp) asserts; no other public source confirms the layout. Progress type 1 marks a progressive trophy's target and progress, as in KytyPS5. Trophies without a `groupId` belong to group -1, KytyPS5's base game group.

- [sceNpTrophy2GetRewardIcon](../../../core/libs/prx/libSceNpTrophy2/src/Context.cpp) (libSceNpTrophy2) takes `(int context, int handle, int reward_id, void* buffer, size_t* size)`, from the call sites in the bundled `Media/Plugins/PSNCore.prx` of PPSA06888, the same shape as `sceNpTrophy2GetTrophyIcon`. The title calls it twice: first with a null buffer to read the size, then with a buffer of that size allocated with `malloc`. The meaning of the third argument is unverified, and the reward icon data is unknown, so every call throws

### Functional

- [Trophy queries](../../../core/libs/prx/libSceNpTrophy2/src/TrophyInfo.cpp) report the title's [trophy data](../../../core/libs/prx/libSceNpTrophy2/src/TrophyPackage.cpp) (`sce_sys/trophy2/trophyNN.ucp`). Without the file, or when it is malformed, they report the old placeholders and `sceNpTrophy2GetTrophyInfoArray` lists only trophy 0. Icons are not read. Hidden trophies report their names and descriptions; whether the console blanks them before the unlock is not known. `progress_percentage` weighs gold 6, silver 2 and bronze 1 and leaves the platinum out, as KytyPS5 does (the ratio of the published PSN trophy points); the console's formula is not known. A trophy or group that is not in the file throws. Unlocks are not recorded, so every trophy reports as locked, with no unlock time and a progress of 0, and the unlocked counts and `progress_percentage` are 0. A context that was not created or was destroyed reports placeholders and returns success; the console's errors for it are unknown.
- [Trophy data limits](../../../core/libs/prx/libSceNpTrophy2/src/TrophyPackage.cpp): a `trophyNN.ucp` larger than 256 MiB or listing more than 4096 files, and a JSON file in it that nests deeper than 64 levels or holds more than 1048576 values, are not used, and the title runs as without that file. The limits keep the memory the reader takes to the file and about 140 MiB of parsed JSON; the console's limits are not known.
- [Text language](../../../core/libs/prx/libSceNpTrophy2/src/TrophyPackage.cpp): texts come from `tropmeta_<tag>.json` for the console language, then `en-US`, then the file's `defaultLanguage`. The tags for languages 0 to 29 are KytyPS5's; `uk-UA` for 30 (Ukrainian) is an assumption.
