#include "prx/libSceLibreSsl/include/Api.hpp"
#include <cstddef>
#include <memory>
#include <utility>
#include "prx/libSceLibreSsl/include/Ec.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libc/include/HostEntropy.hpp"
#include "mbedtls/ecp.h"

namespace {
constexpr int ErrorLibraryEc = 16;
constexpr int PassedNullParameter = 67;
constexpr int PointAtInfinity = 106;
constexpr int PointIsNotOnCurve = 107;
constexpr int InvalidPrivateKey = 123;
constexpr int WrongOrder = 130;
}

int EcRandom(void*, unsigned char* output, std::size_t length) {
    HostEntropy_nid_no_patch(output, length);
    return 0;
}

extern "C" {

int APS5_VABI EC_KEY_generate_key_nid_postfix(EcKey* key) {
    if (key == nullptr || !key->group) {
        EcPutError(ErrorLibraryEc, PassedNullParameter);
        return 0;
    }
    if (key->publicKey) EcRequireSameCurve(*key->group, *key->publicKey, __func__);
    if (!key->privateKey) key->privateKey = std::make_unique<EcNumber>();
    if (!key->publicKey) key->publicKey = std::make_unique<EcPoint>(key->group->curve);
    EcCheck(mbedtls_ecp_gen_keypair(&key->group->group, &key->privateKey->value, &key->publicKey->point, EcRandom, nullptr), "mbedtls_ecp_gen_keypair");
    return 1;
}

int APS5_VABI EC_KEY_check_key_nid_postfix(const EcKey* key) {
    if (key == nullptr || !key->group || !key->publicKey) {
        EcPutError(ErrorLibraryEc, PassedNullParameter);
        return 0;
    }
    EcRequireSameCurve(*key->group, *key->publicKey, __func__);
    if (EcIsInfinity(*key->publicKey)) {
        EcPutError(ErrorLibraryEc, PointAtInfinity);
        return 0;
    }
    if (!EcIsOnCurve(*key->group, *key->publicKey)) {
        EcPutError(ErrorLibraryEc, PointIsNotOnCurve);
        return 0;
    }
    if (key->privateKey) {
        if (mbedtls_mpi_cmp_mpi(&key->privateKey->value, &key->group->group.N) >= 0) {
            EcPutError(ErrorLibraryEc, WrongOrder);
            return 0;
        }
        EcPoint product(key->group->curve);
        EcCheck(mbedtls_ecp_mul(&key->group->group, &product.point, &key->privateKey->value, &key->group->group.G, EcRandom, nullptr), "mbedtls_ecp_mul");
        if (mbedtls_ecp_point_cmp(&product.point, &key->publicKey->point) != 0) {
            EcPutError(ErrorLibraryEc, InvalidPrivateKey);
            return 0;
        }
    }
    return 1;
}

const EcPoint* APS5_VABI EC_KEY_get0_public_key_nid_postfix(const EcKey* key) {
    if (key == nullptr) APS5_INVALID_ARG_EX;
    return key->publicKey.get();
}

int APS5_VABI EC_KEY_set_public_key_nid_postfix(EcKey* key, const EcPoint* point) {
    if (key == nullptr) APS5_INVALID_ARG_EX;
    if (point == nullptr) {
        key->publicKey.reset();
        return 0;
    }
    if (!key->group) {
        key->publicKey.reset();
        EcPutError(ErrorLibraryEc, PassedNullParameter);
        return 0;
    }
    EcRequireSameCurve(*key->group, *point, __func__);
    auto copy = std::make_unique<EcPoint>(key->group->curve);
    EcCheck(mbedtls_ecp_copy(&copy->point, &point->point), "mbedtls_ecp_copy");
    key->publicKey = std::move(copy);
    return 1;
}

}
