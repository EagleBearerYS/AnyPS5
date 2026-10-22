# libSceLibreSsl3 classification notes

- **What it is:** the LibreSSL 3.x-generation console library. Its 87 named exports are all OpenSSL 1.1-style additions:
  - `*_new`/`*_free` contexts, `*_up_ref` and `*_get0_*` accessors;
  - `OpenSSL_version` and `SSL_CTX_set_min`/`max_proto_version`;
  - `EVP_PKEY_CTX` encrypt/decrypt, `EVP_DigestSign*`, AES key wrap, `CRYPTO_memcmp`.
- **Counts:** 0 A, 48 B-S, 13 B-M, 25 B-L, 0 C, 1 D.
- **Why no A:** none of the 87 names is in libSceLibreSsl's todo list, and libSceLibreSsl implements nothing today. Rows that are the same LibreSSL API as libSceLibreSsl functions are B and say "shared code with libSceLibreSsl ...". For example: `EVP_MD_CTX_new`/`free` = `EVP_MD_CTX_create`/`destroy`, `ASN1_STRING_get0_data` = `ASN1_STRING_data`, `EVP_sha1`/`sha224` = the `EVP_sha256` descriptors, `MD5`/`SHA1` = the `MD5_*`/`SHA1_*` code, `EVP_DigestInit`/`EVP_EncryptInit` = the `*_ex` forms.
- **One implementation for both libraries:** compile one source set into both prx, the way core/libs/prx/libSceCesCs/CesLibrary.cmake builds libSceCesCs and libSceCesCs-module. Each prx keeps its own state (for example its error queue), as the two console modules do.
- **Layout:** in 3.x, `X509`/`RSA`/`DH`/`DSA`/`EVP_PKEY` are opaque. These accessors therefore read whatever object model libSceLibreSsl adopts. `EVP_MD_CTX_new` heap-allocates the same 2.x-layout context that libSceLibreSsl's `EVP_MD_CTX_init` fills.
- **mbedtls (3rdparty/mbedtls, as libSceSsl) can serve:**
  - `CRYPTO_memcmp` (`mbedtls_ct_memcmp`);
  - the EVP/digest additions;
  - `EVP_aes_*_wrap` (`mbedtls_nist_kw`, enabled in 3.6.7);
  - the RSA/EC/EVP_PKEY accessors, `EVP_PKEY_CTX`, `EVP_DigestSign*` and `EVP_Sign`/`VerifyFinal` (libSceLibreSsl's key-tier mirrors);
  - `d2i_ASN1_INTEGER`, `o2i_ECPublicKey`, and `d2i_ECParameters` for named curves only (explicit curve parameters need LibreSSL).
- **Needs the LibreSSL submodule (B-L):** `BIO_up_ref`/`BIO_vfree`, `d2i_*_bio`, PKCS7/PKCS8 (`EVP_PKCS82PKEY`), `OCSP_*`, `X509_STORE_CTX_new`/`init`/`free`, `X509_STORE_up_ref`, and every `SSL_*` addition. The same guest FILE/path/socket/callback-thunk and Windows LLP64 caveats apply as in libSceLibreSsl.notes.md.
- **D:** `OpenSSL_version`. `OPENSSL_VERSION` returns the console's `LIBRESSL_VERSION_TEXT`, which is not in the tree or in a public source.

## Batch proposals

1. **1.1 EVP/digest additions (15):** `EVP_MD_CTX_new`/`free`, `EVP_DigestInit`, `EVP_EncryptInit`, `EVP_CIPHER_CTX_set_flags`, `EVP_sha1`, `EVP_sha224`, `EVP_aes_128`/`192`/`256_wrap`, `MD5`, `SHA1`, `CRYPTO_memcmp`, `ERR_peek_last_error`, `ERR_load_crypto_strings`.
   Lands with or after libSceLibreSsl batches 1 and 3.
   Tests: build the libSceLibreSsl digest/EVP guest test sources a second time against libSceLibreSsl3, as `guest_ces_module_tests` reuses GuestCes.cpp. Add RFC 3394 section 4 key-wrap vectors, including the refusal without `EVP_CIPHER_CTX_FLAG_WRAP_ALLOW`.
2. **1.1 key accessors and EVP_PKEY_CTX (19):** `RSA_get0_key`, `RSA_up_ref`, `RSA_check_key`, `RSA_padding_add_PKCS1_PSS_mgf1`, `EC_KEY_up_ref`, `o2i_ECPublicKey`, `d2i_ECParameters`, `EVP_PKEY_get0_RSA`, `EVP_PKEY_set1_EC_KEY`, `EVP_PKEY_size`, `EVP_PKEY_up_ref`, `EVP_PKEY_CTX_new`, `EVP_PKEY_CTX_ctrl`, `EVP_PKEY_encrypt_init`, `EVP_PKEY_encrypt`, `EVP_PKEY_decrypt_init`, `EVP_PKEY_decrypt`, `EVP_SignFinal`, `EVP_VerifyFinal`.
   Lands after libSceLibreSsl's BN/RSA/EC key-tier batch.
   Tests: a guest test with RSA-OAEP and PSS round-trips, fixed RFC 8017 example vectors, P-256 point decode (`o2i`), and a named-curve `d2i_ECParameters`.
