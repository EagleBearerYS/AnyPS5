#include "prx/libSceLibreSsl/include/Api.hpp"
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>
#include "prx/libSceLibreSsl/include/Ec.hpp"
#include "prx/libSceLibreSsl/include/Error.hpp"
#include "prx/libc/include/General.hpp"
#include "mbedtls/bignum.h"
#include "mbedtls/ecp.h"

namespace {
constexpr int ErrorLibraryEc = 16;
constexpr int AnyFunction = 0xfff;
constexpr int PassedNullParameter = 67;
constexpr int BufferTooSmall = 100;
constexpr int InvalidEncoding = 102;
constexpr int InvalidForm = 104;
constexpr int PointIsNotOnCurve = 107;
constexpr int InvalidCompressedPoint = 110;

constexpr int Compressed = 2;
constexpr int Uncompressed = 4;
constexpr int Hybrid = 6;

void Power(mbedtls_mpi& result, const mbedtls_mpi& base, const mbedtls_mpi& exponent, const mbedtls_mpi& modulus) {
    EcCheck(mbedtls_mpi_exp_mod(&result, &base, &exponent, &modulus, nullptr), "mbedtls_mpi_exp_mod");
}

void MultiplyModulo(mbedtls_mpi& result, const mbedtls_mpi& left, const mbedtls_mpi& right, const mbedtls_mpi& modulus) {
    EcNumber product;
    EcCheck(mbedtls_mpi_mul_mpi(&product.value, &left, &right), "mbedtls_mpi_mul_mpi");
    EcCheck(mbedtls_mpi_mod_mpi(&result, &product.value, &modulus), "mbedtls_mpi_mod_mpi");
}

bool SquareRoot(mbedtls_mpi& root, const mbedtls_mpi& value, const mbedtls_mpi& prime) {
    if (mbedtls_mpi_cmp_int(&value, 0) == 0) {
        EcCheck(mbedtls_mpi_lset(&root, 0), "mbedtls_mpi_lset");
        return true;
    }
    EcNumber half;
    EcNumber test;
    EcCheck(mbedtls_mpi_sub_int(&half.value, &prime, 1), "mbedtls_mpi_sub_int");
    EcCheck(mbedtls_mpi_shift_r(&half.value, 1), "mbedtls_mpi_shift_r");
    Power(test.value, value, half.value, prime);
    if (mbedtls_mpi_cmp_int(&test.value, 1) != 0) return false;
    EcNumber odd;
    EcCheck(mbedtls_mpi_sub_int(&odd.value, &prime, 1), "mbedtls_mpi_sub_int");
    const std::size_t twos = mbedtls_mpi_lsb(&odd.value);
    EcCheck(mbedtls_mpi_shift_r(&odd.value, twos), "mbedtls_mpi_shift_r");
    EcNumber minusOne;
    EcCheck(mbedtls_mpi_sub_int(&minusOne.value, &prime, 1), "mbedtls_mpi_sub_int");
    EcNumber nonResidue;
    EcCheck(mbedtls_mpi_lset(&nonResidue.value, 2), "mbedtls_mpi_lset");
    for (;;) {
        Power(test.value, nonResidue.value, half.value, prime);
        if (mbedtls_mpi_cmp_mpi(&test.value, &minusOne.value) == 0) break;
        EcCheck(mbedtls_mpi_add_int(&nonResidue.value, &nonResidue.value, 1), "mbedtls_mpi_add_int");
    }
    EcNumber factor;
    EcNumber remainder;
    EcNumber exponent;
    Power(factor.value, nonResidue.value, odd.value, prime);
    Power(remainder.value, value, odd.value, prime);
    EcCheck(mbedtls_mpi_add_int(&exponent.value, &odd.value, 1), "mbedtls_mpi_add_int");
    EcCheck(mbedtls_mpi_shift_r(&exponent.value, 1), "mbedtls_mpi_shift_r");
    Power(root, value, exponent.value, prime);
    std::size_t order = twos;
    while (mbedtls_mpi_cmp_int(&remainder.value, 1) != 0) {
        std::size_t smallest = 0;
        EcCheck(mbedtls_mpi_copy(&test.value, &remainder.value), "mbedtls_mpi_copy");
        while (mbedtls_mpi_cmp_int(&test.value, 1) != 0) {
            MultiplyModulo(test.value, test.value, test.value, prime);
            ++smallest;
        }
        EcNumber step;
        EcCheck(mbedtls_mpi_copy(&step.value, &factor.value), "mbedtls_mpi_copy");
        for (std::size_t i = smallest + 1; i < order; ++i) MultiplyModulo(step.value, step.value, step.value, prime);
        order = smallest;
        MultiplyModulo(factor.value, step.value, step.value, prime);
        MultiplyModulo(remainder.value, remainder.value, factor.value, prime);
        MultiplyModulo(root, root, step.value, prime);
    }
    return true;
}

void CurveRightSide(mbedtls_mpi& result, const EcGroup& group, const mbedtls_mpi& x) {
    const mbedtls_mpi& prime = group.group.P;
    EcNumber a;
    if (mbedtls_ecp_group_a_is_minus_3(&group.group)) EcCheck(mbedtls_mpi_sub_int(&a.value, &prime, 3), "mbedtls_mpi_sub_int");
    else EcCheck(mbedtls_mpi_copy(&a.value, &group.group.A), "mbedtls_mpi_copy");
    EcNumber cube;
    MultiplyModulo(cube.value, x, x, prime);
    EcCheck(mbedtls_mpi_add_mpi(&cube.value, &cube.value, &a.value), "mbedtls_mpi_add_mpi");
    MultiplyModulo(cube.value, cube.value, x, prime);
    EcCheck(mbedtls_mpi_add_mpi(&cube.value, &cube.value, &group.group.B), "mbedtls_mpi_add_mpi");
    EcCheck(mbedtls_mpi_mod_mpi(&result, &cube.value, &prime), "mbedtls_mpi_mod_mpi");
}

void SetAffine(const EcGroup& group, EcPoint& point, const mbedtls_mpi& x, const mbedtls_mpi& y) {
    const std::size_t field = EcFieldBytes(group);
    std::vector<unsigned char> bytes(1 + 2 * field);
    bytes[0] = Uncompressed;
    EcCheck(mbedtls_mpi_write_binary(&x, bytes.data() + 1, field), "mbedtls_mpi_write_binary");
    EcCheck(mbedtls_mpi_write_binary(&y, bytes.data() + 1 + field, field), "mbedtls_mpi_write_binary");
    EcCheck(mbedtls_ecp_point_read_binary(&group.group, &point.point, bytes.data(), bytes.size()), "mbedtls_ecp_point_read_binary");
}
}

EcNumber::EcNumber() {
    mbedtls_mpi_init(&value);
}

EcNumber::~EcNumber() {
    mbedtls_mpi_free(&value);
}

EcPoint::EcPoint(const NamedCurve& named) : curve(named) {
    mbedtls_ecp_point_init(&point);
}

EcPoint::~EcPoint() {
    mbedtls_ecp_point_free(&point);
}

void EcPutError(int library, int reason) {
    LibreSslPutError_nid_no_patch(library, AnyFunction, reason);
}

void EcCheck(int result, const char* operation) {
    if (result != 0) throw std::runtime_error(std::string(operation) + " failed with mbedtls error " + std::to_string(result));
}

void EcRequireSameCurve(const EcGroup& group, const EcPoint& point, const char* function) {
    if (group.curve.id != point.curve.id) NotImplemented_nid_no_patch((std::string(function) + ": a point used with another curve's group").c_str());
}

bool EcIsInfinity(const EcPoint& point) {
    return mbedtls_ecp_is_zero(const_cast<mbedtls_ecp_point*>(&point.point)) == 1;
}

bool EcIsOnCurve(const EcGroup& group, const EcPoint& point) {
    return EcIsInfinity(point) || mbedtls_ecp_check_pubkey(&group.group, &point.point) == 0;
}

std::size_t EcFieldBytes(const EcGroup& group) {
    return mbedtls_mpi_size(&group.group.P);
}

std::vector<unsigned char> EcAffineBytes(const EcGroup& group, const EcPoint& point) {
    std::vector<unsigned char> bytes(1 + 2 * EcFieldBytes(group));
    std::size_t written = 0;
    EcCheck(mbedtls_ecp_point_write_binary(&group.group, &point.point, MBEDTLS_ECP_PF_UNCOMPRESSED, &written, bytes.data(), bytes.size()), "mbedtls_ecp_point_write_binary");
    if (written != bytes.size()) throw std::runtime_error("mbedtls_ecp_point_write_binary wrote the point at infinity");
    return bytes;
}

extern "C" {

EcPoint* APS5_VABI EC_POINT_new_nid_postfix(const EcGroup* group) {
    if (group == nullptr) {
        EcPutError(ErrorLibraryEc, PassedNullParameter);
        return nullptr;
    }
    return new EcPoint(group->curve);
}

void APS5_VABI EC_POINT_free_nid_postfix(EcPoint* point) {
    delete point;
}

std::size_t APS5_VABI EC_POINT_point2oct_nid_postfix(const EcGroup* group, const EcPoint* point, int form, unsigned char* buffer, std::size_t length, void*) {
    if (group == nullptr || point == nullptr) APS5_INVALID_ARG_EX;
    EcRequireSameCurve(*group, *point, __func__);
    if (form != Compressed && form != Uncompressed && form != Hybrid) {
        EcPutError(ErrorLibraryEc, InvalidForm);
        return 0;
    }
    if (EcIsInfinity(*point)) {
        if (buffer != nullptr) {
            if (length < 1) {
                EcPutError(ErrorLibraryEc, BufferTooSmall);
                return 0;
            }
            buffer[0] = 0;
        }
        return 1;
    }
    const std::size_t field = EcFieldBytes(*group);
    const std::size_t required = form == Compressed ? 1 + field : 1 + 2 * field;
    if (buffer == nullptr) return required;
    if (length < required) {
        EcPutError(ErrorLibraryEc, BufferTooSmall);
        return 0;
    }
    const std::vector<unsigned char> affine = EcAffineBytes(*group, *point);
    const bool odd = (affine.back() & 1) != 0;
    buffer[0] = static_cast<unsigned char>(form + (form != Uncompressed && odd ? 1 : 0));
    for (std::size_t i = 1; i < required; ++i) buffer[i] = affine[i];
    return required;
}

int APS5_VABI EC_POINT_oct2point_nid_postfix(const EcGroup* group, EcPoint* point, const unsigned char* buffer, std::size_t length, void*) {
    if (group == nullptr || point == nullptr) APS5_INVALID_ARG_EX;
    EcRequireSameCurve(*group, *point, __func__);
    if (length == 0) {
        EcPutError(ErrorLibraryEc, BufferTooSmall);
        return 0;
    }
    if (buffer == nullptr) APS5_INVALID_ARG_EX;
    const int form = buffer[0] & ~1;
    const bool yBit = (buffer[0] & 1) != 0;
    if ((form != 0 && form != Compressed && form != Uncompressed && form != Hybrid) || ((form == 0 || form == Uncompressed) && yBit)) {
        EcPutError(ErrorLibraryEc, InvalidEncoding);
        return 0;
    }
    if (form == 0) {
        if (length != 1) {
            EcPutError(ErrorLibraryEc, InvalidEncoding);
            return 0;
        }
        EcCheck(mbedtls_ecp_set_zero(&point->point), "mbedtls_ecp_set_zero");
        return 1;
    }
    const std::size_t field = EcFieldBytes(*group);
    if (length != (form == Compressed ? 1 + field : 1 + 2 * field)) {
        EcPutError(ErrorLibraryEc, InvalidEncoding);
        return 0;
    }
    const mbedtls_mpi& prime = group->group.P;
    EcNumber x;
    EcNumber y;
    EcCheck(mbedtls_mpi_read_binary(&x.value, buffer + 1, field), "mbedtls_mpi_read_binary");
    if (mbedtls_mpi_cmp_mpi(&x.value, &prime) >= 0) {
        EcPutError(ErrorLibraryEc, InvalidEncoding);
        return 0;
    }
    if (form == Compressed) {
        ERR_clear_error_nid_postfix();
        EcNumber square;
        CurveRightSide(square.value, *group, x.value);
        if (!SquareRoot(y.value, square.value, prime)) {
            EcPutError(ErrorLibraryEc, InvalidCompressedPoint);
            return 0;
        }
        if ((mbedtls_mpi_get_bit(&y.value, 0) == 1) != yBit) EcCheck(mbedtls_mpi_sub_mpi(&y.value, &prime, &y.value), "mbedtls_mpi_sub_mpi");
    } else {
        EcCheck(mbedtls_mpi_read_binary(&y.value, buffer + 1 + field, field), "mbedtls_mpi_read_binary");
        if (mbedtls_mpi_cmp_mpi(&y.value, &prime) >= 0 || (form == Hybrid && (mbedtls_mpi_get_bit(&y.value, 0) == 1) != yBit)) {
            EcPutError(ErrorLibraryEc, InvalidEncoding);
            return 0;
        }
    }
    SetAffine(*group, *point, x.value, y.value);
    if (!EcIsOnCurve(*group, *point)) {
        EcPutError(ErrorLibraryEc, PointIsNotOnCurve);
        return 0;
    }
    return 1;
}

}
