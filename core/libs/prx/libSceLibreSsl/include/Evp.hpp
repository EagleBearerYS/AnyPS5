#ifndef CORE_LIBS_PRX_LIBSCELIBRESSL_INCLUDE_EVP_HPP
#define CORE_LIBS_PRX_LIBSCELIBRESSL_INCLUDE_EVP_HPP

#include <cstddef>
#include <cstdint>
#include "prx/libc/include/general/VabiMacros.hpp"

inline constexpr std::uint64_t CipherModeMask = 0xF0007;
inline constexpr std::uint64_t CipherModeStream = 0;
inline constexpr std::uint64_t CipherModeEcb = 1;
inline constexpr std::uint64_t CipherModeCbc = 2;
inline constexpr std::uint64_t CipherModeCfb = 3;
inline constexpr std::uint64_t CipherModeOfb = 4;
inline constexpr std::uint64_t CipherModeCtr = 5;
inline constexpr std::uint64_t CipherFlagCustomIv = 0x10;
inline constexpr std::uint64_t CipherFlagAlwaysCallInit = 0x20;
inline constexpr std::uint64_t CipherFlagCtrlInit = 0x40;
inline constexpr std::uint64_t CipherFlagNoPadding = 0x100;
inline constexpr std::uint64_t CipherFlagCustomCipher = 0x100000;
inline constexpr int CipherControlInit = 0;

struct LibreSslEvpCipherCtx;

struct LibreSslEvpCipher {
    int nid;
    int blockSize;
    int keyLength;
    int ivLength;
    std::uint64_t flags;
    int (APS5_VABI* init)(LibreSslEvpCipherCtx* ctx, const unsigned char* key, const unsigned char* iv, int encrypt);
    int (APS5_VABI* doCipher)(LibreSslEvpCipherCtx* ctx, unsigned char* out, const unsigned char* in, std::size_t length);
    int (APS5_VABI* cleanup)(LibreSslEvpCipherCtx* ctx);
    int contextSize;
    int (APS5_VABI* setAsn1Parameters)(LibreSslEvpCipherCtx* ctx, void* type);
    int (APS5_VABI* getAsn1Parameters)(LibreSslEvpCipherCtx* ctx, void* type);
    int (APS5_VABI* control)(LibreSslEvpCipherCtx* ctx, int type, int arg, void* pointer);
    void* applicationData;
};

struct LibreSslEvpCipherCtx {
    const LibreSslEvpCipher* cipher;
    void* engine;
    int encrypt;
    int bufLen;
    unsigned char oiv[16];
    unsigned char iv[16];
    unsigned char buf[32];
    int num;
    void* appData;
    int keyLen;
    std::uint64_t flags;
    void* cipherData;
    int finalUsed;
    int blockMask;
    unsigned char final[32];
};

static_assert(sizeof(LibreSslEvpCipher) == 88);
static_assert(offsetof(LibreSslEvpCipher, flags) == 16);
static_assert(offsetof(LibreSslEvpCipher, init) == 24);
static_assert(offsetof(LibreSslEvpCipher, contextSize) == 48);
static_assert(offsetof(LibreSslEvpCipher, control) == 72);
static_assert(sizeof(LibreSslEvpCipherCtx) == 168);
static_assert(offsetof(LibreSslEvpCipherCtx, num) == 88);
static_assert(offsetof(LibreSslEvpCipherCtx, flags) == 112);
static_assert(offsetof(LibreSslEvpCipherCtx, final) == 136);

#endif
