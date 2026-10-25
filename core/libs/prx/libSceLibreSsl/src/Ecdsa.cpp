#include "prx/libSceLibreSsl/include/Api.hpp"
#include <cstddef>
#include <cstring>
#include <vector>
#include "prx/libSceLibreSsl/include/Ec.hpp"
#include "prx/libc/include/General.hpp"
#include "mbedtls/bignum.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"

namespace {
constexpr int ErrorLibraryEc = 16;
constexpr int ErrorLibraryEcdsa = 42;
constexpr int PassedNullParameter = 67;
constexpr int EcLibrary = 16;
constexpr int PointAtInfinity = 106;
constexpr int BadSignature = 100;
constexpr int MissingParameters = 103;

constexpr unsigned char Sequence = 0x30;
constexpr unsigned char Integer = 0x02;

void AppendLength(std::vector<unsigned char>& out, std::size_t length) {
    if (length < 0x80) {
        out.push_back(static_cast<unsigned char>(length));
        return;
    }
    unsigned char bytes[sizeof(std::size_t)];
    std::size_t count = 0;
    for (std::size_t rest = length; rest != 0; rest >>= 8) bytes[count++] = static_cast<unsigned char>(rest);
    out.push_back(static_cast<unsigned char>(0x80 | count));
    while (count > 0) out.push_back(bytes[--count]);
}

void AppendInteger(std::vector<unsigned char>& out, const mbedtls_mpi& value) {
    const std::size_t bits = mbedtls_mpi_bitlen(&value);
    const std::size_t pad = bits % 8 == 0 ? 1 : 0;
    const std::size_t size = (bits + 7) / 8;
    std::vector<unsigned char> content(pad + size, 0);
    EcCheck(mbedtls_mpi_write_binary(&value, content.data() + pad, size), "mbedtls_mpi_write_binary");
    out.push_back(Integer);
    AppendLength(out, content.size());
    out.insert(out.end(), content.begin(), content.end());
}

std::vector<unsigned char> EncodeSignature(const mbedtls_mpi& r, const mbedtls_mpi& s) {
    std::vector<unsigned char> integers;
    AppendInteger(integers, r);
    AppendInteger(integers, s);
    std::vector<unsigned char> out{Sequence};
    AppendLength(out, integers.size());
    out.insert(out.end(), integers.begin(), integers.end());
    return out;
}

bool ReadLength(const unsigned char*& cursor, const unsigned char* end, std::size_t& length) {
    if (cursor == end) return false;
    const unsigned char first = *cursor++;
    if (first < 0x80) {
        length = first;
        return true;
    }
    const std::size_t count = first & 0x7f;
    if (count == 0 || count > sizeof(std::size_t) || static_cast<std::size_t>(end - cursor) < count || *cursor == 0) return false;
    length = 0;
    for (std::size_t i = 0; i < count; ++i) length = length << 8 | *cursor++;
    return length >= 0x80;
}

bool ReadInteger(const unsigned char*& cursor, const unsigned char* end, mbedtls_mpi& value) {
    if (cursor == end || *cursor++ != Integer) return false;
    std::size_t length = 0;
    if (!ReadLength(cursor, end, length) || length == 0 || length > static_cast<std::size_t>(end - cursor)) return false;
    const unsigned char* content = cursor;
    cursor += length;
    if ((content[0] & 0x80) != 0 || (content[0] == 0 && length > 1 && (content[1] & 0x80) == 0)) return false;
    EcCheck(mbedtls_mpi_read_binary(&value, content, length), "mbedtls_mpi_read_binary");
    return true;
}

bool DecodeSignature(const unsigned char* data, int length, mbedtls_mpi& r, mbedtls_mpi& s) {
    if (length <= 0) return false;
    const unsigned char* cursor = data;
    const unsigned char* end = data + length;
    std::size_t sequence = 0;
    if (*cursor++ != Sequence || !ReadLength(cursor, end, sequence) || sequence != static_cast<std::size_t>(end - cursor)) return false;
    return ReadInteger(cursor, end, r) && ReadInteger(cursor, end, s) && cursor == end;
}

void DigestNumber(mbedtls_mpi& number, const mbedtls_mpi& order, const unsigned char* digest, int length) {
    if (length < 0 || (digest == nullptr && length > 0)) APS5_INVALID_ARG_EX;
    const std::size_t bits = mbedtls_mpi_bitlen(&order);
    std::size_t used = static_cast<std::size_t>(length);
    if (8 * used > bits) used = (bits + 7) / 8;
    EcCheck(mbedtls_mpi_read_binary(&number, digest, used), "mbedtls_mpi_read_binary");
    if (8 * used > bits) EcCheck(mbedtls_mpi_shift_r(&number, 8 - bits % 8), "mbedtls_mpi_shift_r");
}

void MultiplyModulo(mbedtls_mpi& result, const mbedtls_mpi& left, const mbedtls_mpi& right, const mbedtls_mpi& modulus) {
    EcNumber product;
    EcCheck(mbedtls_mpi_mul_mpi(&product.value, &left, &right), "mbedtls_mpi_mul_mpi");
    EcCheck(mbedtls_mpi_mod_mpi(&result, &product.value, &modulus), "mbedtls_mpi_mod_mpi");
}
}

extern "C" {

int APS5_VABI ECDSA_sign_nid_postfix(int, const unsigned char* digest, int digestLength, unsigned char* signature, unsigned int* signatureLength, EcKey* key) {
    if (key == nullptr || signatureLength == nullptr) APS5_INVALID_ARG_EX;
    if (!key->group || !key->privateKey) {
        EcPutError(ErrorLibraryEcdsa, PassedNullParameter);
        *signatureLength = 0;
        return 0;
    }
    if (digestLength < 0 || (digest == nullptr && digestLength > 0)) APS5_INVALID_ARG_EX;
    EcNumber privateKey;
    EcCheck(mbedtls_mpi_mod_mpi(&privateKey.value, &key->privateKey->value, &key->group->group.N), "mbedtls_mpi_mod_mpi");
    EcNumber r;
    EcNumber s;
    EcCheck(mbedtls_ecdsa_sign(&key->group->group, &r.value, &s.value, &privateKey.value, digest, static_cast<std::size_t>(digestLength), EcRandom, nullptr), "mbedtls_ecdsa_sign");
    const std::vector<unsigned char> encoded = EncodeSignature(r.value, s.value);
    if (signature != nullptr) std::memcpy(signature, encoded.data(), encoded.size());
    *signatureLength = static_cast<unsigned int>(encoded.size());
    return 1;
}

int APS5_VABI ECDSA_verify_nid_postfix(int, const unsigned char* digest, int digestLength, const unsigned char* signature, int signatureLength, EcKey* key) {
    if (signature == nullptr && signatureLength != 0) APS5_INVALID_ARG_EX;
    EcNumber r;
    EcNumber s;
    if (!DecodeSignature(signature, signatureLength, r.value, s.value)) return -1;
    if (key == nullptr) APS5_INVALID_ARG_EX;
    if (!key->group || !key->publicKey) {
        EcPutError(ErrorLibraryEcdsa, MissingParameters);
        return -1;
    }
    EcGroup& group = *key->group;
    const EcPoint& publicKey = *key->publicKey;
    EcRequireSameCurve(group, publicKey, __func__);
    const mbedtls_mpi& order = group.group.N;
    if (mbedtls_mpi_cmp_int(&r.value, 0) == 0 || mbedtls_mpi_cmp_mpi(&r.value, &order) >= 0 || mbedtls_mpi_cmp_int(&s.value, 0) == 0 || mbedtls_mpi_cmp_mpi(&s.value, &order) >= 0) {
        EcPutError(ErrorLibraryEcdsa, BadSignature);
        return 0;
    }
    EcNumber inverse;
    EcNumber message;
    EcNumber first;
    EcNumber second;
    EcCheck(mbedtls_mpi_inv_mod(&inverse.value, &s.value, &order), "mbedtls_mpi_inv_mod");
    DigestNumber(message.value, order, digest, digestLength);
    MultiplyModulo(first.value, message.value, inverse.value, order);
    MultiplyModulo(second.value, r.value, inverse.value, order);
    EcPoint sum(group.curve);
    if (EcIsInfinity(publicKey)) {
        if (mbedtls_mpi_cmp_int(&first.value, 0) != 0) EcCheck(mbedtls_ecp_mul(&group.group, &sum.point, &first.value, &group.group.G, EcRandom, nullptr), "mbedtls_ecp_mul");
    } else {
        if (!EcIsOnCurve(group, publicKey)) NotImplemented_nid_no_patch("ECDSA_verify with a public key off the curve");
        EcCheck(mbedtls_ecp_muladd(&group.group, &sum.point, &first.value, &group.group.G, &second.value, &publicKey.point), "mbedtls_ecp_muladd");
    }
    if (EcIsInfinity(sum)) {
        EcPutError(ErrorLibraryEc, PointAtInfinity);
        EcPutError(ErrorLibraryEcdsa, EcLibrary);
        return -1;
    }
    const std::vector<unsigned char> affine = EcAffineBytes(group, sum);
    EcNumber x;
    EcCheck(mbedtls_mpi_read_binary(&x.value, affine.data() + 1, EcFieldBytes(group)), "mbedtls_mpi_read_binary");
    EcCheck(mbedtls_mpi_mod_mpi(&x.value, &x.value, &order), "mbedtls_mpi_mod_mpi");
    return mbedtls_mpi_cmp_mpi(&x.value, &r.value) == 0 ? 1 : 0;
}

}
