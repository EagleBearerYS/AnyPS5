#pragma once

#include <cstddef>
#include <memory>
#include <vector>
#include "mbedtls/ecp.h"

struct NamedCurve {
    int nid;
    mbedtls_ecp_group_id id;
};

struct EcGroup {
    explicit EcGroup(const NamedCurve& named);
    ~EcGroup();

    EcGroup(const EcGroup&) = delete;
    EcGroup& operator=(const EcGroup&) = delete;

    const NamedCurve curve;
    mbedtls_ecp_group group;
};

struct EcKey {
    std::unique_ptr<EcGroup> group;
};

struct EcNumber {
    EcNumber();
    ~EcNumber();

    EcNumber(const EcNumber&) = delete;
    EcNumber& operator=(const EcNumber&) = delete;

    mbedtls_mpi value;
};

struct EcPoint {
    explicit EcPoint(const NamedCurve& named);
    ~EcPoint();

    EcPoint(const EcPoint&) = delete;
    EcPoint& operator=(const EcPoint&) = delete;

    const NamedCurve curve;
    mbedtls_ecp_point point;
};

void EcPutError(int library, int reason);
void EcCheck(int result, const char* operation);
void EcRequireSameCurve(const EcGroup& group, const EcPoint& point, const char* function);
bool EcIsInfinity(const EcPoint& point);
bool EcIsOnCurve(const EcGroup& group, const EcPoint& point);
std::size_t EcFieldBytes(const EcGroup& group);
std::vector<unsigned char> EcAffineBytes(const EcGroup& group, const EcPoint& point);
