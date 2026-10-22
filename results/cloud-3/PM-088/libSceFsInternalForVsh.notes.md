# libSceFsInternalForVsh (176 todo: A 0, B-S 0, B-M 0, B-L 0, C 0, D 176)

libSceFsInternalForVsh is the VSH/ShellCore filesystem control library. It mounts and unmounts package, save-data, trophy and LWFS images, formats and checks console storage, and drives the Blu-ray copy scheduler and I/O schedulers. Titles reach save data and packages through libSceSaveData and the kernel path aliases, not through this library. All 176 exports in `core/libs/prx/libSceFsInternalForVsh/Export.cpp` are parameterless stubs. The library's debt file says the names come only from the PS5 3.20 export inventory and that no credible prototypes exist.

Families (all D):
- UFS maintenance (35): `sceFsUfs*` mkfs, fsck, growfs, image allocation, recrypt, extent maps, telemetry. These are system-storage operations with private option structs.
- VSH mounts (32): `sceFsMount*`, `sceFsUmount*`, `sceFsInit{Mount,Umount}*Opt`, bind/unbind. The option-struct initialisers and image formats (game pkg, PPR pkg, download data, LWFS) are not public.
- PFS (18): `sceFsDevpfs*` and `sceFsCreatePfs*Image`, i.e. the Sony PFS container and its event protocol. LVD (10): layered virtual-disk attach. Workspace (11), save-data transactions (4), NSFS/MLFS (7): private filesystems.
- Blu-ray scheduler (15), I/O schedulers ISSched/Gsched (11), external storage and exFAT (10), and misc attribute/cluster/reserve/metadata calls (23): system-only, with no prototype.

No row is A. No other library defines these names. libSceSaveData.native and the libkernel path resolver give titles the result of a mount (`/savedata0`, `/app0`), but not these VSH calls. No row is B. PFS, LVD and the Sony option structs are not documented. UFS mkfs/fsck could follow FreeBSD newfs/fsck_ffs, but there is no signature, and the host has no UFS volume to act on. No row is C, because no "not supported" error value is in the tree or a public source.

Dependencies: none proposed. PFS/LVD crypto (AES-XTS, HMAC) could use the existing 3rdparty/mbedtls submodule if a format ever became public. Formatting exFAT or UFS would need a filesystem library, and nothing in the tree needs one.

Batch proposals: none. The library has no A, B or C row.
