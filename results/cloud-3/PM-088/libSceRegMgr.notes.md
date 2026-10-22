# libSceRegMgr (65 todo: A 0, B-S 0, B-M 0, B-L 0, C 0, D 65)

What it is: the system registry manager. System software reads and writes typed entries (int, string, binary) by 32-bit entry ID, keeps NVS/HDD-backed registry files, backs them up, reports change events and converts region/machine-type codes. Titles reach the settings it stores through libSceSystemService (`sceSystemServiceParamGetInt`), not through this library.

Families and decisions:
- Get/Set Int/Str/Bin and NonSys variants (12): signatures are declared, but the entry IDs, their types, default values and the REGMGR error codes are not in the tree or in public sources. A general store would be easy to write; returning the right value for an entry is not. D.
- Init values, CheckError/NonSysCheckError/CheckIntVal, ResetVal, GetVersion(ForPS4), Setbin (10): unknown signatures (debt) and depend on registry defaults and errors. D.
- Backup Push/Pull/NeedMem (+Gkp, ForPS4) (8): registry backup blob format not public. D.
- Srv region/machine type (7): unknown signatures; region and machine codes not public. D.
- Evt/IsChange (5): change-event queue internals. D.
- Cntl/Drv/Tool/Log/Print/Recover, init level/state maintenance and debug exports (21): registry file maintenance. D.
- Strncpy/Memset (2): probably libc-style helpers, but the debt lists their signatures as unknown, so they stay D until an argument order is confirmed.

New third-party dependencies: none would help; the gap is the registry schema, not code.

Batch proposals: none from this library (no A/B/C rows). The registry-backed `sceSystemServiceParamGetInt` parameters 100-111, 200-213 and 300 are blocked on the same missing schema.
