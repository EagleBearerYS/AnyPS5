# libSceNpTrophy2 technical debt

### Silent stubs

- [libSceNpTrophy2](../../../core/libs/prx/libSceNpTrophy2/src/GameInfo.cpp): `sceNpTrophy2RegisterUnlockCallback` accepts a callback that is never called, as the callback's signature and when the console calls it are not known, `sceNpTrophy2UnregisterUnlockCallback` only returns success, and `sceNpTrophy2ShowTrophyList` returns success without showing a list (there is no system trophy UI; seen on PPSA15552).

### Functional

- [Trophy queries](../../../core/libs/prx/libSceNpTrophy2/src/TrophyInfo.cpp) report the unlocks in the [trophy store](../../../core/libs/prx/libSceNpTrophy2/src/TrophyStore.cpp) (unlocked, unlock time, `unlocked_trophies`) and placeholders for everything that comes from the title's trophy data (`sce_sys/trophy2/trophyNN.ucp`), which is not read: names, descriptions, grades, groups, hidden and reward flags, icons, the number of trophies, the grade counts, `progress_percentage`, group data and progress values. `sceNpTrophy2GetTrophyInfoArray` lists only trophy 0. A context that was not created or was destroyed reports no unlocks and returns success; the console's errors for it are unknown.
