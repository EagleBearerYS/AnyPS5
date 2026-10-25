#include "prx/libSceLibreSsl/include/Api.hpp"
#include <atomic>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include "prx/libc/include/Api.hpp"
#include "prx/libc/include/General.hpp"

struct LibreSslBignum {
    std::uint64_t* d;
    int top;
    int dmax;
    int neg;
    int flags;
};

static_assert(sizeof(LibreSslBignum) == 24);
static_assert(offsetof(LibreSslBignum, top) == 8);
static_assert(sizeof(LibreSslRsaMethod) == 112);
static_assert(offsetof(LibreSslRsaMethod, flags) == 72);
static_assert(sizeof(LibreSslRsa) == 152);
static_assert(offsetof(LibreSslRsa, meth) == 16);
static_assert(offsetof(LibreSslRsa, n) == 32);
static_assert(offsetof(LibreSslRsa, flags) == 108);

namespace {

using RsaCallback = int (APS5_VABI*)(LibreSslRsa* rsa);

constexpr int OperationInit = 6;
constexpr int OperationFinish = 7;
constexpr int RsaFlagCachePublic = 0x0002;
constexpr int RsaFlagCachePrivate = 0x0004;
constexpr int RsaFlagNonFipsAllow = 0x0400;
constexpr int BignumFlagStaticData = 0x02;
constexpr int BignumFlagMalloced = 0x01;

template<typename TFunction> TFunction Operation(const LibreSslRsaMethod& method, int index) {
    return reinterpret_cast<TFunction>(method.operations[index]);
}

void Wipe(void* data, std::size_t size) {
    auto* bytes = static_cast<volatile unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) bytes[index] = 0;
}

void ClearFreeBignum(LibreSslBignum* number) {
    if (!number) return;
    if (number->d && !(number->flags & BignumFlagStaticData)) {
        if (number->dmax < 0) throw std::invalid_argument("BN_clear_free: negative word capacity");
        Wipe(number->d, static_cast<std::size_t>(number->dmax) * sizeof(number->d[0]));
        free_nid_postfix(number->d);
    }
    const bool allocated = number->flags & BignumFlagMalloced;
    Wipe(number, sizeof(LibreSslBignum));
    if (allocated) free_nid_postfix(number);
}

int APS5_VABI DefaultPublicEncrypt(int, const unsigned char*, unsigned char*, LibreSslRsa*, int) {
    NotImplemented_nid_no_patch("RSA_eay_public_encrypt");
}

int APS5_VABI DefaultPublicDecrypt(int, const unsigned char*, unsigned char*, LibreSslRsa*, int) {
    NotImplemented_nid_no_patch("RSA_eay_public_decrypt");
}

int APS5_VABI DefaultPrivateEncrypt(int, const unsigned char*, unsigned char*, LibreSslRsa*, int) {
    NotImplemented_nid_no_patch("RSA_eay_private_encrypt");
}

int APS5_VABI DefaultPrivateDecrypt(int, const unsigned char*, unsigned char*, LibreSslRsa*, int) {
    NotImplemented_nid_no_patch("RSA_eay_private_decrypt");
}

int APS5_VABI DefaultModExp(void*, const void*, LibreSslRsa*, void*) {
    NotImplemented_nid_no_patch("RSA_eay_mod_exp");
}

int APS5_VABI DefaultBignumModExp(void*, const void*, const void*, const void*, void*, void*) {
    NotImplemented_nid_no_patch("BN_mod_exp_mont_ct");
}

int APS5_VABI DefaultInit(LibreSslRsa* rsa) {
    rsa->flags |= RsaFlagCachePublic | RsaFlagCachePrivate;
    return 1;
}

int APS5_VABI DefaultFinish(LibreSslRsa* rsa) {
    if (rsa->methodModN || rsa->methodModP || rsa->methodModQ) NotImplemented_nid_no_patch("RSA_eay_finish: freeing a Montgomery context");
    return 1;
}

const LibreSslRsaMethod DefaultMethod {
    "Eric Young's PKCS#1 RSA",
    {
        reinterpret_cast<void*>(DefaultPublicEncrypt),
        reinterpret_cast<void*>(DefaultPublicDecrypt),
        reinterpret_cast<void*>(DefaultPrivateEncrypt),
        reinterpret_cast<void*>(DefaultPrivateDecrypt),
        reinterpret_cast<void*>(DefaultModExp),
        reinterpret_cast<void*>(DefaultBignumModExp),
        reinterpret_cast<void*>(DefaultInit),
        reinterpret_cast<void*>(DefaultFinish),
    },
    0,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

int ByteCount(const LibreSslBignum* number, const char* function) {
    if (!number) throw std::invalid_argument(std::string(function) + ": null modulus");
    if (number->top == 0) return 0;
    if (number->top < 0) throw std::invalid_argument(std::string(function) + ": negative modulus word count");
    if (!number->d) throw std::invalid_argument(std::string(function) + ": null modulus words");
    const std::int64_t bits = static_cast<std::int64_t>(number->top - 1) * 64 + std::bit_width(number->d[number->top - 1]);
    if (bits > INT_MAX - 7) throw std::invalid_argument(std::string(function) + ": modulus bit count overflows int");
    return static_cast<int>((bits + 7) / 8);
}

}

extern "C" {

int APS5_VABI RSA_flags_nid_postfix(const LibreSslRsa* rsa) {
    if (!rsa) return 0;
    if (!rsa->meth) throw std::invalid_argument(std::string(__func__) + ": null method");
    return rsa->meth->flags;
}

int APS5_VABI RSA_size_nid_postfix(const LibreSslRsa* rsa) {
    if (!rsa) throw std::invalid_argument(std::string(__func__) + ": null key");
    return ByteCount(static_cast<const LibreSslBignum*>(rsa->n), __func__);
}

LibreSslRsa* APS5_VABI RSA_new_nid_postfix(void) {
    auto* rsa = static_cast<LibreSslRsa*>(malloc_nid_postfix(sizeof(LibreSslRsa)));
    if (!rsa) return nullptr;
    rsa->meth = &DefaultMethod;
    rsa->engine = nullptr;
    rsa->pad = 0;
    rsa->version = 0;
    rsa->n = rsa->e = rsa->d = rsa->p = rsa->q = rsa->dmp1 = rsa->dmq1 = rsa->iqmp = nullptr;
    rsa->references = 1;
    rsa->methodModN = rsa->methodModP = rsa->methodModQ = nullptr;
    rsa->blinding = rsa->mtBlinding = nullptr;
    rsa->flags = rsa->meth->flags & ~RsaFlagNonFipsAllow;
    rsa->exData = nullptr;
    const auto init = Operation<RsaCallback>(*rsa->meth, OperationInit);
    if (init && !init(rsa)) {
        free_nid_postfix(rsa);
        return nullptr;
    }
    return rsa;
}

void APS5_VABI RSA_free_nid_postfix(LibreSslRsa* rsa) {
    if (!rsa) return;
    if (std::atomic_ref<int>(rsa->references).fetch_sub(1) - 1 > 0) return;
    if (!rsa->meth) throw std::invalid_argument(std::string(__func__) + ": null method");
    if (rsa->engine) NotImplemented_nid_no_patch("RSA_free: releasing an ENGINE");
    if (rsa->exData) NotImplemented_nid_no_patch("RSA_free: freeing ex_data");
    if (rsa->blinding || rsa->mtBlinding) NotImplemented_nid_no_patch("RSA_free: freeing blinding");
    if (const auto finish = Operation<RsaCallback>(*rsa->meth, OperationFinish)) finish(rsa);
    for (void* number : {rsa->n, rsa->e, rsa->d, rsa->p, rsa->q, rsa->dmp1, rsa->dmq1, rsa->iqmp})
        ClearFreeBignum(static_cast<LibreSslBignum*>(number));
    free_nid_postfix(rsa);
}

}
