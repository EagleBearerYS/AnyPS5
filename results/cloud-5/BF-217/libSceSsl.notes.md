# libSceSsl (22 todo: A 0, B-S 0, B-M 0, B-L 0, C-real 0, C-silent 0, D 22)

**What it is:** the system TLS client: contexts with a memory pool, CA and client certificate loading, connections over a title socket, verify options, ALPN and certificate readers. The module also exports NanoSSL core calls (`CA_MGMT_`, `CRYPTO_`, `RSA_`, `VLONG_`), the stack `sceSslGetNanoSSLModuleId` names.

**Existing state** (`core/libs/prx/libSceSsl/Export.cpp`, built at `core/libs/prx/libSceSsl/CMakeLists.txt:57` over the Mbed TLS submodule, `:30`, `:59`):
- 33 done.
  - Runtime contexts and connections (`:485-488`).
  - TLS 1.2 client handshakes with Mbed TLS 3.6.7, whose answers were measured on a PS5 on 13.40 (debt `:9-10`, `:13`).
  - Certificate handles and chains (`:176-186`) and readers (debt `:11`, `:14`).
  - CA loading (debt `:12`).
- Errors (`:44-59`): for example INVALID_ID 0x8095F006, INVALID_VALUE 0x8095F007, WANT_POLLIN and WANT_POLLOUT 0x8095F013 and 0x8095F014.
- Debt:
  - `:5`: the 7 NanoSSL calls plus CheckRecvPending, CreateSslConnection and DeleteSslConnection have no credible prototypes;
  - `:6`: the options, GetNanoSSLModuleId, GetPeerCert, GetSslError, LoadRootCACert and SetSslVersion have none either;
  - `:7`: ReuseConnection's shape comes from SharpProspero, with no console observation;
  - `:16`: SetServerName is unverified.
- Tests:
  - `guest_ssl` (`CMakeLists.txt:70`, `:74`);
  - `guest_ssl_load` (`:75`, `:79`);
  - `guest_ssl_transport_kernel` and `guest_ssl_transport_net` (`:80`, `:85`);
  - `guest_ssl_ps5_readers` (`:88`, `:92`).
- Sources read for this pass:
  - [shadPS4 `ssl.cpp`](https://github.com/shadps4-emu/shadPS4/blob/68818c769a2545d1071fb97e83e3a979b244f324/src/core/libraries/network/ssl.cpp) at 68818c76, the PS4 module: every todo name except GetPeerCert and SetServerName is a parameterless stub that returns 0.
  - [SharpProspero `Http.cs`](https://github.com/SvenGDK/SharpProspero/blob/5dfdb4b0b166e83dc9ae275001afb695f12c7174/src/SharpProspero/Interop/Net/Http.cs) at 5dfdb4b0, its current HEAD: among the todo rows it binds only `sceSslReuseConnection` (`#L260-L262`).
- Neighbour: libSceLibreSsl has OpenSSL-named calls (`RSA_verify`, `CRYPTO_*`), but none of them is a NanoSSL name.

## Families

- **NanoSSL core (7), D:** `CA_MGMT_extractKeyBlobEx`, `CA_MGMT_extractPublicKeyInfo`, `CA_MGMT_freeKeyBlob`, `CRYPTO_initAsymmetricKey`, `CRYPTO_uninitAsymmetricKey`, `RSA_verifySignature` and `VLONG_freeVlongQueue`.
  - The key-blob, asymmetric-key and bignum-queue formats are not public.
- **Connection forms (4), D:**
  - `sceSslCreateSslConnection` and `sceSslDeleteSslConnection` are not shown to be aliases of the done `sceSslCreateConnection` (`:823`) and `sceSslDeleteConnection` (`:835`).
  - `sceSslCheckRecvPending`: its argument and answer are unknown.
  - `sceSslReuseConnection`: SharpProspero gives the shape `(int sslConnectionId, int sockId)` and a one-line summary ("reuses a completed session on a freshly connected socket"), but no contract (debt `:7`).
- **Options (5), D:** `sceSslEnableOption`, `sceSslDisableOption`, `sceSslEnableOptionInternal`, `sceSslDisableOptionInternal` and `sceSslDisableOptionInternalInsecure`.
  - The option bits are unknown. The done verify-option calls (`:1073`, `:1082`, debt `:13`) are a different API.
- **Queries and setters (6), D:**
  - `sceSslGetPeerCert`: the handles exist, but the arguments and lifetime are unknown.
  - `sceSslGetSslError`: not shown to share the arguments of libSceHttp's `sceHttpsGetSslError` (`core/libs/prx/libSceHttp/Export.cpp:591`).
  - `sceSslGetNanoSSLModuleId`: a console value.
  - `sceSslLoadRootCACert`: no native root bundle exists (debt `:12`).
  - `sceSslSetSslVersion`: whether it pins one version or a range is unknown; done `sceSslSetMinSslVersion` (`:1127`) sets a minimum only.
  - `sceSslSetServerName` (debt `:16`): how it interacts with the host name that `sceSslCreateConnection` already takes is unknown.

## Batch proposal

None.

## Caveats

- Debt `:7` links SharpProspero `Http.cs#L2231-L2235` at 5dfdb4b0, but that file has 835 lines at that commit. The `sceSslReuseConnection` binding is at `#L260-L262`, so the anchor is stale.
- PRs 9173 and 10255 (open) both claim `CRYPTO_uninitAsymmetricKey` and `VLONG_freeVlongQueue`. No source read here gives their prototypes, so a reviewer should ask where they came from.
- `sceSslReuseConnection` is the nearest to implementable: Mbed TLS can resume a session. It stays D until the console's contract is known: when the handshake runs, the errors, and what happens to the old socket.
