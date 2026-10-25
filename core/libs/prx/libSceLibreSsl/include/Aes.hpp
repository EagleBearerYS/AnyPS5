#ifndef CORE_LIBS_PRX_LIBSCELIBRESSL_INCLUDE_AES_HPP
#define CORE_LIBS_PRX_LIBSCELIBRESSL_INCLUDE_AES_HPP

#include <cstddef>
#include <cstdint>
#include "prx/libc/include/general/VabiMacros.hpp"

inline constexpr int MaxRounds = 14;

struct AesKey {
    std::uint32_t roundKeys[4 * (MaxRounds + 1)];
    std::int32_t rounds;
};

extern "C" {
int APS5_VABI AES_set_encrypt_key_nid_postfix(const unsigned char* userKey, int bits, AesKey* key);
void APS5_VABI AES_encrypt_nid_postfix(const unsigned char* in, unsigned char* out, const AesKey* key);
void APS5_VABI AES_ctr128_encrypt_nid_postfix(const unsigned char* in, unsigned char* out, std::size_t length, const AesKey* key, unsigned char* ivec, unsigned char* ecountBuffer, unsigned* num);
}

#endif
