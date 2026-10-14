# libSceNpUniversalDataSystem technical debt

### Unknown function info

- [Events](../../../core/libs/prx/libSceNpUniversalDataSystem/src/Event.cpp): `sceNpUniversalDataSystemCreateEvent` copies `prop` and returns the event's own property object through `prop_ptr`, as [KytyPS5](https://github.com/KytyPS5/KytyPS5/blob/9e41d8320a2774b303fbad344c2347e8d0292e83/src/libs/libTrophy.cpp) does; `sceNpUniversalDataSystemEventPropertyObjectSetObject`, `SetArray` and the array setters for objects and arrays store a copy of `value` and return the stored copy through `value_ptr`. Destroying a property object or array that its Create function did not return (an event's property object or a stored copy) does nothing, as its owner frees it. These ownership rules are not verified on a console, nor are the output of `sceNpUniversalDataSystemEventToString` (still `{}`) and `sceNpUniversalDataSystemEventEstimateSize` (still 3).

### Functional

- [Trophy events](../../../core/libs/prx/libSceNpUniversalDataSystem/src/Event.cpp): `sceNpUniversalDataSystemPostEvent` stores an `_UnlockTrophy` event with an int32 `_trophy_id` of 0 or more as an unlock for the user and service label of its context, as KytyPS5 does. Unlocks that a title drives through statistics (`statsExtractionRuleArray` in `sce_sys/uds/udsNN.ucp` and `unlockCondition` in `sce_sys/trophy2/trophyNN.ucp`) are not evaluated, and `_UpdateTrophyProgress`, named only by a third-party unlocker, is logged once per trophy and not interpreted. A context need not be registered, and every post returns success: KytyPS5's errors for an unknown or unregistered context and for a malformed unlock are not verified. The storage statistics stay zero.
