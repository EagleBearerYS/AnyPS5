READY

RESULT:
- task: BF-131 done; result results/cloud-1/BF-131.md (b2eb0b188) on cloud-results.
- branch: fork fix/shader-pointer-chase-closure = 6a7284108 on #9457 c00cb0f875 (one commit).
- chase arms (V# at P+off, read at fixed offset) taken in the pointer-keyed walk; depth = longest chain of distinct chase sites, loops and depth > 4 refused; closure entries per record pointer up to depth; disk cache format 47.
- test agc_driver_pointer_chase_table: red "GetImageResource dword 0 is not a valid runtime value; chain: LoadAddressU32@pc=0x000000c0(...Phi(Phi(ReadConstBuffer@pc=0x74..."; green 10/10; 41/41 table tests.
- CI mirror 1752 passed/83 skipped/4 failed of 1839 (3 IPv6, sgpr_pattern_fill_keys 5/5 alone); ^agc|^shader head 896 vs base 895, same single failure; MinGW 0 errors; conventions passed; 0 comment lines.
