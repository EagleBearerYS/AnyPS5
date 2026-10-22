# libSceNpCommon (150 todo: B-S 2, D 148)

What it is: the NP support library that the other NP modules, and SDK modules a title ships, link against. It holds:
- kernel-object wrappers: mutex, cond, lw-mutex/cond, semaphores, event flags, threads, callouts, atomics;
- heaps and allocators;
- string, number, hex and base64 helpers;
- a JSON parser and PSN server-error JSON parsing;
- RFC 3339 time helpers;
- NpId/online-id utilities and debug/registry settings.

The tree has only throwing declarations (`PendingExports.cpp`), and its debt states that no prototype or behaviour is claimed for any of the 150 names.

Classification: the algorithms behind most helpers are public (strtol/snprintf, RFC 4648 base64, RFC 8259 JSON, RFC 3986 escaping, RFC 3339). The library-specific parts have no source in the tree or in a public reference: argument order, size and NUL conventions, error codes, and the layouts of caller-allocated objects (NpMutex, NpCond, heap, callout context, JSON parser state). Implementing them needs a call site or the module, so they are D.
- B-S (2): `sceNpCmpOnlineId` and `sceNpCmpNpIdInOrder`. Signatures and logic are public in shadPS4 `np_common`: compare the online-id data, then the opt bytes. Two prerequisites:
  - `SCE_NP_UTIL_ERROR_NOT_MATCH` 0x80550609 is not yet in the tree (shadPS4 value);
  - the tree's `NpId` (`SceTypes.hpp:1498`, 24 bytes) must grow to the public 36-byte layout (handle 20, opt 8, reserved 8).
- D (148). Neighbours, if a signature source appears:
  - libkernel `sceKernelCreateEventFlag`, `sceKernelCreateSema`, `scePthreadMutexInit`, `scePthreadCondInit`;
  - libSceRtc `sceRtcFormatRFC3339`/`sceRtcParseRFC3339` (`Export.cpp:582,642`);
  - libSceJson2.

Third-party: none needed now. If base64 is implemented later, `3rdparty/mbedtls` already provides `mbedtls_base64_encode/decode`; a JSON parser needs nothing new beyond the parser in libSceJson2.

Batch proposals: no PR-sized batch of 10+ exists. The only candidate is a 2-function PR, "np: online-id and NpId comparison":
- add NOT_MATCH and fix the `NpId` layout;
- test: a new guest test executable registered in `libSceNpCommon/CMakeLists.txt` (it has none) covering:
  - equal and different online ids;
  - ids that differ only in case (a mismatch under shadPS4's `strncmp`);
  - null arguments (0x80550003);
  - ordering of NpIds that differ only in the opt bytes.
