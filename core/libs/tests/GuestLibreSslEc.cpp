#include "prx/libSceLibreSsl/include/Api.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>

struct Curve {
    int nid;
    int degree;
    int signatureSize;
};

static constexpr Curve Curves[] = {
    {409, 192, 56},
    {415, 256, 72},
    {711, 192, 56},
    {712, 224, 66},
    {713, 224, 64},
    {714, 256, 72},
    {715, 384, 104},
    {716, 521, 141},
    {928, 256, 72},
    {932, 384, 104},
    {934, 512, 137},
};

static constexpr int UnknownNids[] = {0, -1, 1, 406, 691, 746, 921, 937, 948, 100000};
static constexpr int CurvesMissingFromMbedtls[] = {410, 704, 721, 735, 749, 840, 929, 935, 936, 944};

struct KnownPoint {
    int nid;
    std::size_t field;
    const char* uncompressed;
    int compressibleBelow16;
};

static constexpr KnownPoint KnownPoints[] = {
    {409, 24, "04AC2C77F529F91689FEA0EA5EFEC7F210D8EEA0B9E047ED563BC723E57670BD4887EBC732C523063D0A7C957BC97C1C43", 9},
    {415, 32, "0460FED4BA255A9D31C961EB74C6356D68C049B8923B61FA6CE669622E60F29FB67903FE1008B8BC99A41AE9E95628BC64F2F1B20C2D7E9F5177A3C294D4462299", 7},
    {711, 24, "0404A4E7BEDC7D8137AADE86C1A4D223AD704E63DAD4717C493EFC196DEF1CAD9823C91F6B8BE2611164B93CCA4BB2C559", 10},
    {712, 28, "042EF983FA542B64472E2BC405D9EEDD861ACC9A7F814FAD8275CE6B9A3459BA4AB52164883BD29EB6AC7E6D22AC7D302C053DC39684928EF9", 5},
    {713, 28, "0400CF08DA5AD719E42707FA431292DEA11244D64FC51610D94B130D6CEEAB6F3DEBE455E3DBF85416F7030CBD94F34F2D6F232C69F3C1385A", 6},
    {714, 32, "04782C8ED17E3B2A783B5464F33B09652A71C678E05EC51E84E2BCFC663A3DE963AF9ACB4280B8C7F7C42F4EF9ABA6245EC1EC1712FD38A0FA96418D8CD6AA6152", 9},
    {715, 48, "04EC3A4E415B4E19A4568618029F427FA5DA9A8BC4AE92E02E06AAE5286B300C64DEF8F0EA9055866064A254515480BC138015D9B72D7D57244EA8EF9AC0C621896708A59367F9DFB9F54CA84B3F1C9DB1288B231C3AE0D4FE7344FD2533264720", 8},
    {716, 66, "0401894550D0785932E00EAA23B694F213F8C3121F86DC97A04E5A7167DB4E5BCD371123D46E45DB6B5D5370A7F20FB633155D38FFA16D2BD761DCAC474B9A2F5023A400493101C962CD4D2FDDF782285E64584139C2F91B47F87FF82354D6630F746A28A0DB25741B5B34A828008B22ACC23F924FAAFBD4D33F81EA66956DFEAA2BFDFCF5", 10},
    {928, 32, "042676BD1E3FD83F3328D1AF941442C036760F09587729419053083EB61D1ED22C2CF769688A5FFD67DA1899D243E66BCABE21F9E78335263BF5308B8E41A71B39", 6},
    {932, 48, "046C9AABA343CB2FAF098319CC4D15EA218786F55C8CF0A8B668091170A6422F6C2498945A8164A4B6F27CDD11E800DA501BE961B37B09804610CE0DF40DD8236C75A12D0C8014B163464A4AEBA7CB18D20D3222083EC4A941852F24AA3D5D84E3", 11},
    {934, 64, "041EC7FE2275860C3BC0E4E6E459AF7E16985D37ADBA7351AC357A7C397E07522EA41BCCA8E89777FE05B8F0D9DC8C614004FCAF30A97001A5011A159F46FCD5443CBC1DDFC7AC89A1A2F8EEF77BF9BBA8ADE73DA2100CB6A371546B495FB5EA885EB631645E79591DB659C49266D263D5CBD3403081CB407536EFE9A5BEC69955", 7},
};

static void Require(bool value, const char* what) {
    if (value) return;
    std::fprintf(stderr, "failed: %s\n", what);
    std::abort();
}

template<typename TAction> static void RequireThrows(TAction action, const char* what) {
    try { action(); } catch (const std::exception&) { return; }
    Require(false, what);
}

static void CheckNamedCurves() {
    for (const Curve& curve : Curves) {
        EcGroup* group = EC_GROUP_new_by_curve_name_nid_postfix(curve.nid);
        Require(group != nullptr, "EC_GROUP_new_by_curve_name for a named curve");
        Require(EC_GROUP_get_degree_nid_postfix(group) == curve.degree, "EC_GROUP_get_degree");
        EcKey* key = EC_KEY_new_by_curve_name_nid_postfix(curve.nid);
        Require(key != nullptr, "EC_KEY_new_by_curve_name for a named curve");
        const EcGroup* keyGroup = EC_KEY_get0_group_nid_postfix(key);
        Require(keyGroup != nullptr && keyGroup != group, "EC_KEY_get0_group of a key made from a curve name");
        Require(EC_GROUP_get_degree_nid_postfix(keyGroup) == curve.degree, "degree of the key's group");
        Require(ECDSA_size_nid_postfix(key) == curve.signatureSize, "ECDSA_size");
        EC_KEY_free_nid_postfix(key);
        EC_GROUP_free_nid_postfix(group);
    }
}

static void CheckOtherNids() {
    for (const int nid : UnknownNids) {
        Require(EC_GROUP_new_by_curve_name_nid_postfix(nid) == nullptr, "EC_GROUP_new_by_curve_name for an unknown curve");
        Require(EC_KEY_new_by_curve_name_nid_postfix(nid) == nullptr, "EC_KEY_new_by_curve_name for an unknown curve");
    }
    for (const int nid : CurvesMissingFromMbedtls) {
        RequireThrows([&] { EC_GROUP_new_by_curve_name_nid_postfix(nid); }, "EC_GROUP_new_by_curve_name for a curve mbedtls lacks");
        RequireThrows([&] { EC_KEY_new_by_curve_name_nid_postfix(nid); }, "EC_KEY_new_by_curve_name for a curve mbedtls lacks");
    }
}

static void CheckSetGroup() {
    EcKey* key = EC_KEY_new_nid_postfix();
    Require(key != nullptr, "EC_KEY_new");
    Require(EC_KEY_get0_group_nid_postfix(key) == nullptr, "EC_KEY_new has no group");
    Require(ECDSA_size_nid_postfix(key) == 0, "ECDSA_size of a key without a group");

    EcGroup* group = EC_GROUP_new_by_curve_name_nid_postfix(715);
    Require(EC_KEY_set_group_nid_postfix(key, group) == 1, "EC_KEY_set_group");
    const EcGroup* copy = EC_KEY_get0_group_nid_postfix(key);
    Require(copy != nullptr && copy != group, "EC_KEY_set_group stores a copy");
    EC_GROUP_free_nid_postfix(group);
    Require(EC_GROUP_get_degree_nid_postfix(EC_KEY_get0_group_nid_postfix(key)) == 384, "the copy outlives the original group");
    Require(ECDSA_size_nid_postfix(key) == 104, "ECDSA_size after EC_KEY_set_group");

    Require(EC_KEY_set_group_nid_postfix(key, EC_KEY_get0_group_nid_postfix(key)) == 1, "EC_KEY_set_group with the key's own group");
    Require(EC_GROUP_get_degree_nid_postfix(EC_KEY_get0_group_nid_postfix(key)) == 384, "the key's own group set again");

    EcGroup* other = EC_GROUP_new_by_curve_name_nid_postfix(409);
    Require(EC_KEY_set_group_nid_postfix(key, other) == 1, "EC_KEY_set_group replacing a group");
    Require(EC_GROUP_get_degree_nid_postfix(EC_KEY_get0_group_nid_postfix(key)) == 192, "the replaced group");
    EC_GROUP_free_nid_postfix(other);

    Require(EC_KEY_set_group_nid_postfix(key, nullptr) == 0, "EC_KEY_set_group with a null group");
    Require(EC_KEY_get0_group_nid_postfix(key) == nullptr, "a null group drops the previous one");
    Require(ECDSA_size_nid_postfix(key) == 0, "ECDSA_size after a null group");
    EC_KEY_free_nid_postfix(key);
}

static void CheckNullArguments() {
    EC_GROUP_free_nid_postfix(nullptr);
    EC_KEY_free_nid_postfix(nullptr);
    Require(ECDSA_size_nid_postfix(nullptr) == 0, "ECDSA_size of a null key");
    RequireThrows([] { EC_GROUP_get_degree_nid_postfix(nullptr); }, "EC_GROUP_get_degree of a null group");
    RequireThrows([] { EC_KEY_get0_group_nid_postfix(nullptr); }, "EC_KEY_get0_group of a null key");
    EcGroup* group = EC_GROUP_new_by_curve_name_nid_postfix(415);
    RequireThrows([&] { EC_KEY_set_group_nid_postfix(nullptr, group); }, "EC_KEY_set_group of a null key");
    EC_GROUP_free_nid_postfix(group);
}

static std::vector<unsigned char> Bytes(const char* hex) {
    std::vector<unsigned char> bytes;
    for (const char* digit = hex; digit[0] != 0 && digit[1] != 0; digit += 2) {
        const auto value = [](char c) { return c <= '9' ? c - '0' : c - 'A' + 10; };
        bytes.push_back(static_cast<unsigned char>(value(digit[0]) << 4 | value(digit[1])));
    }
    return bytes;
}

static std::uint64_t ErrorCode(int library, int reason) {
    return static_cast<std::uint64_t>(library) << 24 | std::uint64_t{0xfff} << 12 | static_cast<std::uint64_t>(reason);
}

static void RequireError(int library, int reason, const char* what) {
    Require(ERR_get_error_nid_postfix() == ErrorCode(library, reason), what);
    Require(ERR_get_error_nid_postfix() == 0, "only one error queued");
}

static std::vector<unsigned char> Encode(const EcGroup* group, const EcPoint* point, int form) {
    std::vector<unsigned char> bytes(EC_POINT_point2oct_nid_postfix(group, point, form, nullptr, 0, nullptr));
    Require(EC_POINT_point2oct_nid_postfix(group, point, form, bytes.data(), bytes.size(), nullptr) == bytes.size(), "EC_POINT_point2oct into a buffer of the reported size");
    return bytes;
}

static int Decode(const EcGroup* group, EcPoint* point, const std::vector<unsigned char>& bytes) {
    return EC_POINT_oct2point_nid_postfix(group, point, bytes.data(), bytes.size(), nullptr);
}

static void CheckPointNew() {
    ERR_clear_error_nid_postfix();
    Require(EC_POINT_new_nid_postfix(nullptr) == nullptr, "EC_POINT_new of a null group");
    RequireError(16, 67, "EC_POINT_new of a null group queues ERR_R_PASSED_NULL_PARAMETER");
    EC_POINT_free_nid_postfix(nullptr);
    EcGroup* group = EC_GROUP_new_by_curve_name_nid_postfix(415);
    EcPoint* point = EC_POINT_new_nid_postfix(group);
    Require(point != nullptr, "EC_POINT_new");
    unsigned char encoded[2] = {0xaa, 0xaa};
    Require(EC_POINT_point2oct_nid_postfix(group, point, 4, nullptr, 0, nullptr) == 1, "a new point is the point at infinity");
    Require(EC_POINT_point2oct_nid_postfix(group, point, 2, encoded, sizeof(encoded), nullptr) == 1 && encoded[0] == 0 && encoded[1] == 0xaa, "the point at infinity encodes as one zero octet");
    Require(EC_POINT_point2oct_nid_postfix(group, point, 6, encoded, 0, nullptr) == 0, "the point at infinity in an empty buffer");
    RequireError(16, 100, "EC_R_BUFFER_TOO_SMALL for the point at infinity");
    Require(EC_POINT_point2oct_nid_postfix(group, point, 3, encoded, sizeof(encoded), nullptr) == 0, "an invalid conversion form");
    RequireError(16, 104, "EC_R_INVALID_FORM before the point at infinity");
    const std::vector<unsigned char> infinity = {0};
    Require(Decode(group, point, infinity) == 1 && Encode(group, point, 4) == infinity, "decoding the point at infinity");
    EC_POINT_free_nid_postfix(point);
    EC_GROUP_free_nid_postfix(group);
}

static void CheckPointEncodings() {
    for (const KnownPoint& known : KnownPoints) {
        EcGroup* group = EC_GROUP_new_by_curve_name_nid_postfix(known.nid);
        EcPoint* point = EC_POINT_new_nid_postfix(group);
        EcPoint* decoded = EC_POINT_new_nid_postfix(group);
        const std::vector<unsigned char> uncompressed = Bytes(known.uncompressed);
        const unsigned char parity = uncompressed.back() & 1;
        std::vector<unsigned char> compressed(uncompressed.begin(), uncompressed.begin() + static_cast<std::ptrdiff_t>(1 + known.field));
        compressed[0] = static_cast<unsigned char>(2 + parity);
        std::vector<unsigned char> hybrid = uncompressed;
        hybrid[0] = static_cast<unsigned char>(6 + parity);
        Require(Decode(group, point, uncompressed) == 1, "decoding a known uncompressed point");
        Require(EC_POINT_point2oct_nid_postfix(group, point, 2, nullptr, 0, nullptr) == 1 + known.field, "the compressed length");
        Require(Encode(group, point, 4) == uncompressed, "re-encoding a known point uncompressed");
        Require(Encode(group, point, 2) == compressed, "encoding a known point compressed");
        Require(Encode(group, point, 6) == hybrid, "encoding a known point hybrid");
        Require(Decode(group, decoded, compressed) == 1 && Encode(group, decoded, 4) == uncompressed, "decompressing a known point recovers its y");
        Require(Decode(group, decoded, hybrid) == 1 && Encode(group, decoded, 4) == uncompressed, "decoding a known hybrid point");
        std::vector<unsigned char> buffer(uncompressed.size());
        Require(EC_POINT_point2oct_nid_postfix(group, point, 4, buffer.data(), buffer.size() - 1, nullptr) == 0, "encoding into a short buffer");
        RequireError(16, 100, "EC_R_BUFFER_TOO_SMALL");
        EC_POINT_free_nid_postfix(decoded);
        EC_POINT_free_nid_postfix(point);
        EC_GROUP_free_nid_postfix(group);
    }
}

static void CheckCompressedRoots() {
    for (const KnownPoint& known : KnownPoints) {
        EcGroup* group = EC_GROUP_new_by_curve_name_nid_postfix(known.nid);
        EcPoint* even = EC_POINT_new_nid_postfix(group);
        EcPoint* odd = EC_POINT_new_nid_postfix(group);
        int decoded = 0;
        for (unsigned char x = 0; x < 16; ++x) {
            std::vector<unsigned char> bytes(1 + known.field, 0);
            bytes.back() = x;
            bytes[0] = 2;
            const int evenResult = Decode(group, even, bytes);
            bytes[0] = 3;
            const int oddResult = Decode(group, odd, bytes);
            Require(evenResult == oddResult, "both parities of an x decode alike");
            if (evenResult == 0) {
                RequireError(16, 110, "EC_R_INVALID_COMPRESSED_POINT when x^3 + ax + b has no square root");
                continue;
            }
            ++decoded;
            const std::vector<unsigned char> evenPoint = Encode(group, even, 4);
            const std::vector<unsigned char> oddPoint = Encode(group, odd, 4);
            Require((evenPoint.back() & 1) == 0 && (oddPoint.back() & 1) == 1, "the parity bit picks the root");
            Require(Encode(group, odd, 2) == bytes, "an odd decompressed point compresses back");
            bytes[0] = 2;
            Require(Encode(group, even, 2) == bytes, "an even decompressed point compresses back");
            Require(Decode(group, even, evenPoint) == 1 && Decode(group, odd, oddPoint) == 1, "a decompressed point is on the curve");
        }
        Require(decoded == known.compressibleBelow16, "the x below 16 that decompress match the curve equation");
        Require(Decode(group, even, {0}) == 1, "the point at infinity");
        Require(EC_POINT_new_nid_postfix(nullptr) == nullptr, "queue an error");
        std::vector<unsigned char> compressed = Bytes(known.uncompressed);
        compressed.resize(1 + known.field);
        compressed[0] = static_cast<unsigned char>(2 + (Bytes(known.uncompressed).back() & 1));
        Require(Decode(group, even, compressed) == 1 && ERR_get_error_nid_postfix() == 0, "decompressing clears the error queue, as LibreSSL does");
        EC_POINT_free_nid_postfix(odd);
        EC_POINT_free_nid_postfix(even);
        EC_GROUP_free_nid_postfix(group);
    }
}

static void CheckMalformedPoints() {
    for (const KnownPoint& known : KnownPoints) {
        EcGroup* group = EC_GROUP_new_by_curve_name_nid_postfix(known.nid);
        EcPoint* point = EC_POINT_new_nid_postfix(group);
        const std::vector<unsigned char> uncompressed = Bytes(known.uncompressed);
        const std::size_t field = known.field;
        Require(Decode(group, point, uncompressed) == 1, "decoding a known point");
        const auto refused = [&](std::vector<unsigned char> bytes, int reason, const char* what) {
            Require(Decode(group, point, bytes) == 0, what);
            RequireError(16, reason, what);
            Require(Encode(group, point, 4) == uncompressed, "a refused encoding leaves the point unchanged");
        };
        Require(EC_POINT_oct2point_nid_postfix(group, point, uncompressed.data(), 0, nullptr) == 0, "an empty encoding");
        RequireError(16, 100, "EC_R_BUFFER_TOO_SMALL for an empty encoding");
        std::vector<unsigned char> bytes = uncompressed;
        bytes[0] = 5;
        refused(bytes, 102, "an uncompressed form with the y bit");
        bytes[0] = 8;
        refused(bytes, 102, "an unknown form");
        refused({1}, 102, "the infinity form with the y bit");
        refused({0, 0}, 102, "the infinity form with a trailing octet");
        refused(std::vector<unsigned char>(uncompressed.begin(), uncompressed.end() - 1), 102, "an uncompressed point one octet short");
        bytes = uncompressed;
        bytes[0] = 2;
        refused(bytes, 102, "a compressed form with an uncompressed length");
        bytes = uncompressed;
        for (std::size_t i = 1; i <= field; ++i) bytes[i] = 0xff;
        refused(bytes, 102, "an x at or above the field prime");
        bytes.resize(1 + field);
        bytes[0] = 3;
        refused(bytes, 102, "a compressed x at or above the field prime");
        bytes = uncompressed;
        for (std::size_t i = 1 + field; i < bytes.size(); ++i) bytes[i] = 0xff;
        refused(bytes, 102, "a y at or above the field prime");
        bytes = uncompressed;
        bytes[0] = static_cast<unsigned char>(7 - (uncompressed.back() & 1));
        refused(bytes, 102, "a hybrid point with the wrong y bit");
        bytes = uncompressed;
        bytes.back() ^= 1;
        Require(Decode(group, point, bytes) == 0, "a point off the curve");
        RequireError(16, 107, "EC_R_POINT_IS_NOT_ON_CURVE");
        Require(Encode(group, point, 4) == bytes, "an uncompressed point off the curve is still stored, as LibreSSL stores it before the check");
        EC_POINT_free_nid_postfix(point);
        EC_GROUP_free_nid_postfix(group);
    }
}

static void CheckPointArguments() {
    EcGroup* group = EC_GROUP_new_by_curve_name_nid_postfix(415);
    EcGroup* other = EC_GROUP_new_by_curve_name_nid_postfix(715);
    EcPoint* point = EC_POINT_new_nid_postfix(group);
    const std::vector<unsigned char> infinity = {0};
    RequireThrows([&] { EC_POINT_point2oct_nid_postfix(nullptr, point, 4, nullptr, 0, nullptr); }, "EC_POINT_point2oct of a null group");
    RequireThrows([&] { EC_POINT_point2oct_nid_postfix(group, nullptr, 4, nullptr, 0, nullptr); }, "EC_POINT_point2oct of a null point");
    RequireThrows([&] { Decode(nullptr, point, infinity); }, "EC_POINT_oct2point of a null group");
    RequireThrows([&] { Decode(group, nullptr, infinity); }, "EC_POINT_oct2point of a null point");
    RequireThrows([&] { EC_POINT_oct2point_nid_postfix(group, point, nullptr, 1, nullptr); }, "EC_POINT_oct2point of a null buffer");
    RequireThrows([&] { EC_POINT_point2oct_nid_postfix(other, point, 4, nullptr, 0, nullptr); }, "EC_POINT_point2oct with another curve's group");
    RequireThrows([&] { Decode(other, point, infinity); }, "EC_POINT_oct2point with another curve's group");
    EC_POINT_free_nid_postfix(point);
    EC_GROUP_free_nid_postfix(other);
    EC_GROUP_free_nid_postfix(group);
}

int main() {
    CheckNamedCurves();
    CheckOtherNids();
    CheckSetGroup();
    CheckNullArguments();
    CheckPointNew();
    CheckPointEncodings();
    CheckCompressedRoots();
    CheckMalformedPoints();
    CheckPointArguments();
    return 0;
}
