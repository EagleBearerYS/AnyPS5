#include "prx/libc/include/Api.hpp"
#include "prx/libSceLibreSsl/include/Api.hpp"
#include <climits>
#include <cstddef>
#include <cstring>
#include <vector>
#include "prx/libSceLibreSsl/include/Ec.hpp"
#include "prx/libc/include/General.hpp"
#include "mbedtls/ecp.h"

namespace {
constexpr int ErrorLibraryEc = 16;
constexpr int ErrorLibraryEcdh = 43;
constexpr int MallocFailure = 65;
constexpr int PassedNullParameter = 67;
constexpr int PointAtInfinity = 106;
constexpr int NoPrivateValue = 100;
constexpr int PointArithmeticFailure = 101;
constexpr int KdfFailed = 102;
constexpr int KeyTruncation = 104;

struct GuestBuffer {
    explicit GuestBuffer(std::size_t size) : data(malloc_nid_postfix(size)) {}
    ~GuestBuffer() {
        free_nid_postfix(data);
    }

    GuestBuffer(const GuestBuffer&) = delete;
    GuestBuffer& operator=(const GuestBuffer&) = delete;

    void* data;
};
}

extern "C" {

int APS5_VABI ECDH_compute_key_nid_postfix(void* out, std::size_t outLength, const EcPoint* publicKey, EcKey* key, EcdhKdf kdf) {
    if (key == nullptr) APS5_INVALID_ARG_EX;
    if (outLength > INT_MAX) {
        EcPutError(ErrorLibraryEcdh, MallocFailure);
        return -1;
    }
    if (!key->privateKey) {
        EcPutError(ErrorLibraryEcdh, NoPrivateValue);
        return -1;
    }
    if (!key->group) {
        EcPutError(ErrorLibraryEc, PassedNullParameter);
        EcPutError(ErrorLibraryEcdh, MallocFailure);
        return -1;
    }
    EcGroup& group = *key->group;
    if (publicKey != nullptr) EcRequireSameCurve(group, *publicKey, __func__);
    EcNumber privateKey;
    EcCheck(mbedtls_mpi_mod_mpi(&privateKey.value, &key->privateKey->value, &group.group.N), "mbedtls_mpi_mod_mpi");
    if (publicKey == nullptr || EcIsInfinity(*publicKey) || mbedtls_mpi_cmp_int(&privateKey.value, 0) == 0) {
        EcPutError(ErrorLibraryEc, PointAtInfinity);
        EcPutError(ErrorLibraryEcdh, PointArithmeticFailure);
        return -1;
    }
    if (!EcIsOnCurve(group, *publicKey)) NotImplemented_nid_no_patch("ECDH_compute_key with a public key off the curve");
    EcPoint shared(group.curve);
    EcCheck(mbedtls_ecp_mul(&group.group, &shared.point, &privateKey.value, &publicKey->point, EcRandom, nullptr), "mbedtls_ecp_mul");
    const std::size_t field = EcFieldBytes(group);
    if (kdf == nullptr && outLength < field) {
        EcPutError(ErrorLibraryEcdh, KeyTruncation);
        return -1;
    }
    const std::vector<unsigned char> affine = EcAffineBytes(group, shared);
    const std::size_t lengthOffset = (field + alignof(std::size_t) - 1) / alignof(std::size_t) * alignof(std::size_t);
    GuestBuffer secret(kdf != nullptr ? lengthOffset + sizeof(std::size_t) : field);
    if (secret.data == nullptr) {
        EcPutError(ErrorLibraryEcdh, MallocFailure);
        return -1;
    }
    auto* bytes = static_cast<unsigned char*>(secret.data);
    std::memcpy(bytes, affine.data() + 1, field);
    if (kdf != nullptr) {
        auto* length = reinterpret_cast<std::size_t*>(bytes + lengthOffset);
        *length = outLength;
        if (kdf(bytes, field, out, length) == nullptr) {
            EcPutError(ErrorLibraryEcdh, KdfFailed);
            return -1;
        }
        return static_cast<int>(*length);
    }
    if (out == nullptr) APS5_INVALID_ARG_EX;
    if (outLength > field) {
        std::memset(static_cast<unsigned char*>(out) + field, 0, outLength - field);
        outLength = field;
    }
    std::memcpy(out, bytes, outLength);
    return static_cast<int>(outLength);
}

}
