#include "prx/libSceLibreSsl/include/Api.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
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

struct KnownSignature {
    int nid;
    const char* order;
    const char* digest;
    const char* r;
    const char* s;
    const char* der;
};

static constexpr KnownSignature KnownSignatures[] = {
    {409, "FFFFFFFFFFFFFFFFFFFFFFFF99DEF836146BC9B1B4D22831", "AF2BDBE1AA9B6EC1E2ADE1D694F41FC71A831D0268E9891562113D8A62ADD1BF", "4B0B8CE98A92866A2820E20AA6B75B56382E0F9BFD5ECB55", "00CCDB006926EA9565CBADC840829D8C384E06DE1F1E381B85", "303502184B0B8CE98A92866A2820E20AA6B75B56382E0F9BFD5ECB55021900CCDB006926EA9565CBADC840829D8C384E06DE1F1E381B85"},
    {415, "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551", "AF2BDBE1AA9B6EC1E2ADE1D694F41FC71A831D0268E9891562113D8A62ADD1BF", "00EFD48B2AACB6A8FD1140DD9CD45E81D69D2C877B56AAF991C34D0EA84EAF3716", "00F7CB1C942D657C41D436C7A1B6E29F65F3E900DBB9AFF4064DC4AB2F843ACDA8", "3046022100EFD48B2AACB6A8FD1140DD9CD45E81D69D2C877B56AAF991C34D0EA84EAF3716022100F7CB1C942D657C41D436C7A1B6E29F65F3E900DBB9AFF4064DC4AB2F843ACDA8"},
    {711, "FFFFFFFFFFFFFFFFFFFFFFFE26F2FC170F69466A74DEFD8D", "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855", "5CA564801C724E9027E6D39F006EC3F63BD8D3829FDD7850", "6EAEC4B21473DB322E9924F12D2A260467C0ED58882E7134", "303402185CA564801C724E9027E6D39F006EC3F63BD8D3829FDD785002186EAEC4B21473DB322E9924F12D2A260467C0ED58882E7134"},
    {712, "010000000000000000000000000001DCE8D2EC6184CAF0A971769FB1F7", "D14A028C2A3A2BC9476102BB288234C415A2B01F828EA62AC5B3E42F", "00E6A214BA602A2A371A7DB42C92AD3D63DA09C84992DCB5E4036DAFB1", "00FD4C323943614D244D76064AD3620BCA3FDAA4D0DB53B93B07D18575", "303E021D00E6A214BA602A2A371A7DB42C92AD3D63DA09C84992DCB5E4036DAFB1021D00FD4C323943614D244D76064AD3620BCA3FDAA4D0DB53B93B07D18575"},
    {713, "FFFFFFFFFFFFFFFFFFFFFFFFFFFF16A2E0B8F03E13DD29455C5C2A3D", "AF2BDBE1AA9B6EC1E2ADE1D694F41FC71A831D0268E9891562113D8A62ADD1BF", "61AA3DA010E8E8406C656BC477A7A7189895E7E840CDFE8FF42307BA", "00BC814050DAB5D23770879494F9E0A680DC1AF7161991BDE692B10101", "303D021C61AA3DA010E8E8406C656BC477A7A7189895E7E840CDFE8FF42307BA021D00BC814050DAB5D23770879494F9E0A680DC1AF7161991BDE692B10101"},
    {714, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141", "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855", "00F80AE4F96CDBC9D853F83D47AAE225BF407D51C56B7776CD67D0DC195D99A9DC", "00B303E26BE1F73465315221F0B331528807A1A9B6EB068EDE6EEBEAAA49AF8A36", "3046022100F80AE4F96CDBC9D853F83D47AAE225BF407D51C56B7776CD67D0DC195D99A9DC022100B303E26BE1F73465315221F0B331528807A1A9B6EB068EDE6EEBEAAA49AF8A36"},
    {715, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC7634D81F4372DDF581A0DB248B0A77AECEC196ACCC52973", "AF2BDBE1AA9B6EC1E2ADE1D694F41FC71A831D0268E9891562113D8A62ADD1BF", "21B13D1E013C7FA1392D03C5F99AF8B30C570C6F98D4EA8E354B63A21D3DAA33BDE1E888E63355D92FA2B3C36D8FB2CD", "00F3AA443FB107745BF4BD77CB3891674632068A10CA67E3D45DB2266FA7D1FEEBEFDC63ECCD1AC42EC0CB8668A4FA0AB0", "3065023021B13D1E013C7FA1392D03C5F99AF8B30C570C6F98D4EA8E354B63A21D3DAA33BDE1E888E63355D92FA2B3C36D8FB2CD023100F3AA443FB107745BF4BD77CB3891674632068A10CA67E3D45DB2266FA7D1FEEBEFDC63ECCD1AC42EC0CB8668A4FA0AB0"},
    {716, "01FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFA51868783BF2F966B7FCC0148F709A5D03BB5C9B8899C47AEBB6FB71E91386409", "AF2BDBE1AA9B6EC1E2ADE1D694F41FC71A831D0268E9891562113D8A62ADD1BF", "01511BB4D675114FE266FC4372B87682BAECC01D3CC62CF2303C92B3526012659D16876E25C7C1E57648F23B73564D67F61C6F14D527D54972810421E7D87589E1A7", "4A171143A83163D6DF460AAF61522695F207A58B95C0644D87E52AA1A347916E4F7A72930B1BC06DBE22CE3F58264AFD23704CBB63B29B931F7DE6C9D949A7ECFC", "308187024201511BB4D675114FE266FC4372B87682BAECC01D3CC62CF2303C92B3526012659D16876E25C7C1E57648F23B73564D67F61C6F14D527D54972810421E7D87589E1A702414A171143A83163D6DF460AAF61522695F207A58B95C0644D87E52AA1A347916E4F7A72930B1BC06DBE22CE3F58264AFD23704CBB63B29B931F7DE6C9D949A7ECFC"},
    {928, "A9FB57DBA1EEA9BC3E660A909D838D718C397AA3B561A6F7901E0E82974856A7", "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855", "745BE1DA902D19C76C8F57D4A1F3362B4B20ED7C8DE8FC0463D566795F979CEA", "5916C317A1E325B53735216A0FA37737F08B32245C88084817B468A41F5AFEE9", "30440220745BE1DA902D19C76C8F57D4A1F3362B4B20ED7C8DE8FC0463D566795F979CEA02205916C317A1E325B53735216A0FA37737F08B32245C88084817B468A41F5AFEE9"},
    {932, "8CB91E82A3386D280F5D6F7E50E641DF152F7109ED5456B31F166E6CAC0425A7CF3AB6AF6B7FC3103B883202E9046565", "38B060A751AC96384CD9327EB1B1E36A21FDB71114BE07434C0CC7BF63F6E1DA274EDEBFE76F65FBD51AD2F14898B95B", "65FD456814371D60883FFDA5F74F36DC2D45886121770E29ED3163754716D12C1CAB03A2CB6A6E3376FC96D8727BD1BF", "1AA65E57932D05788413219B7AB23E5337F63FB2DCB0F89B4227D284A3FCBDF3C54C021A6C0CA42445BF802213121654", "3064023065FD456814371D60883FFDA5F74F36DC2D45886121770E29ED3163754716D12C1CAB03A2CB6A6E3376FC96D8727BD1BF02301AA65E57932D05788413219B7AB23E5337F63FB2DCB0F89B4227D284A3FCBDF3C54C021A6C0CA42445BF802213121654"},
    {934, "AADD9DB8DBE9C48B3FD4E6AE33C9FC07CB308DB3B3C9D20ED6639CCA70330870553E5C414CA92619418661197FAC10471DB1D381085DDADDB58796829CA90069", "CF83E1357EEFB8BDF1542850D66D8007D620E4050B5715DC83F4A921D36CE9CE47D0D13C5D85F2B0FF8318D2877EEC2F63B931BD47417A81A538327AF927DA3E", "0089EDF75E6E986305D8181386C16DB44BA0D7FF40F4335569754A481F5CD48C6211A63DE7BDAA485E9FA79858A4EABF111FED2959F031DE2A132BA709412683A9", "7A8C08564F51534128BB52FE36DFFAAE89079011256EF8069E64D64C5610D3E611C0BA8B19027388FCCC212523B22C44E85A789E16CB1BBD3240C86B43480FDE", "30818502410089EDF75E6E986305D8181386C16DB44BA0D7FF40F4335569754A481F5CD48C6211A63DE7BDAA485E9FA79858A4EABF111FED2959F031DE2A132BA709412683A902407A8C08564F51534128BB52FE36DFFAAE89079011256EF8069E64D64C5610D3E611C0BA8B19027388FCCC212523B22C44E85A789E16CB1BBD3240C86B43480FDE"},
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

static EcKey* GeneratedKey(int nid) {
    EcKey* key = EC_KEY_new_by_curve_name_nid_postfix(nid);
    Require(key != nullptr && EC_KEY_generate_key_nid_postfix(key) == 1, "EC_KEY_generate_key");
    return key;
}

static void CheckKeyGeneration() {
    ERR_clear_error_nid_postfix();
    for (const KnownPoint& known : KnownPoints) {
        EcKey* key = EC_KEY_new_by_curve_name_nid_postfix(known.nid);
        const EcGroup* group = EC_KEY_get0_group_nid_postfix(key);
        Require(EC_KEY_get0_public_key_nid_postfix(key) == nullptr, "a new key has no public key");
        Require(EC_KEY_check_key_nid_postfix(key) == 0, "EC_KEY_check_key without a public key");
        RequireError(16, 67, "ERR_R_PASSED_NULL_PARAMETER for a key without a public key");
        Require(EC_KEY_generate_key_nid_postfix(key) == 1, "EC_KEY_generate_key");
        const EcPoint* publicKey = EC_KEY_get0_public_key_nid_postfix(key);
        Require(publicKey != nullptr && EC_KEY_check_key_nid_postfix(key) == 1, "a generated key checks");
        const std::vector<unsigned char> first = Encode(group, publicKey, 4);
        EcPoint* decoded = EC_POINT_new_nid_postfix(group);
        Require(first.size() == 1 + 2 * known.field && Decode(group, decoded, first) == 1, "a generated public key is on the curve");
        Require(EC_KEY_generate_key_nid_postfix(key) == 1 && EC_KEY_get0_public_key_nid_postfix(key) == publicKey, "generating again reuses the public key object");
        Require(Encode(group, publicKey, 4) != first && EC_KEY_check_key_nid_postfix(key) == 1, "generating again makes a new key pair");
        Require(ERR_get_error_nid_postfix() == 0, "no error from a good key");
        EC_POINT_free_nid_postfix(decoded);
        EC_KEY_free_nid_postfix(key);
    }
}

static void CheckKeyPublicKeys() {
    for (const KnownPoint& known : KnownPoints) {
        EcKey* key = EC_KEY_new_by_curve_name_nid_postfix(known.nid);
        const EcGroup* group = EC_KEY_get0_group_nid_postfix(key);
        EcPoint* point = EC_POINT_new_nid_postfix(group);
        const std::vector<unsigned char> uncompressed = Bytes(known.uncompressed);
        Require(Decode(group, point, uncompressed) == 1, "decoding a known public key");
        Require(EC_KEY_set_public_key_nid_postfix(key, point) == 1, "EC_KEY_set_public_key");
        const EcPoint* stored = EC_KEY_get0_public_key_nid_postfix(key);
        Require(stored != nullptr && stored != point, "EC_KEY_set_public_key stores a copy");
        EC_POINT_free_nid_postfix(point);
        Require(Encode(group, stored, 4) == uncompressed, "the copy outlives the point it was made from");
        Require(EC_KEY_check_key_nid_postfix(key) == 1, "a known public key without a private key checks");
        Require(EC_KEY_set_public_key_nid_postfix(key, stored) == 1 && Encode(group, EC_KEY_get0_public_key_nid_postfix(key), 4) == uncompressed, "setting a key's own public key keeps its value");
        point = EC_POINT_new_nid_postfix(group);
        std::vector<unsigned char> offCurve = uncompressed;
        offCurve.back() ^= 1;
        Require(Decode(group, point, offCurve) == 0, "decoding a point off the curve");
        RequireError(16, 107, "EC_R_POINT_IS_NOT_ON_CURVE from EC_POINT_oct2point");
        Require(EC_KEY_set_public_key_nid_postfix(key, point) == 1, "setting a stored point off the curve");
        Require(EC_KEY_check_key_nid_postfix(key) == 0, "EC_KEY_check_key of a point off the curve");
        RequireError(16, 107, "EC_R_POINT_IS_NOT_ON_CURVE from EC_KEY_check_key");
        Require(Decode(group, point, {0}) == 1 && EC_KEY_set_public_key_nid_postfix(key, point) == 1, "setting the point at infinity");
        Require(EC_KEY_check_key_nid_postfix(key) == 0, "EC_KEY_check_key of the point at infinity");
        RequireError(16, 106, "EC_R_POINT_AT_INFINITY");
        Require(EC_KEY_set_public_key_nid_postfix(key, nullptr) == 0 && EC_KEY_get0_public_key_nid_postfix(key) == nullptr, "a null public key drops the previous one");
        Require(ERR_get_error_nid_postfix() == 0, "no error queued for a null public key");
        EC_POINT_free_nid_postfix(point);
        EC_KEY_free_nid_postfix(key);
    }
}

static void CheckKeyPairs() {
    EcKey* first = GeneratedKey(415);
    EcKey* second = GeneratedKey(415);
    Require(EC_KEY_set_public_key_nid_postfix(first, EC_KEY_get0_public_key_nid_postfix(second)) == 1, "setting another key's public key");
    Require(EC_KEY_check_key_nid_postfix(first) == 0, "a private key that does not match its public key");
    RequireError(16, 123, "EC_R_INVALID_PRIVATE_KEY");
    Require(EC_KEY_generate_key_nid_postfix(first) == 1 && EC_KEY_check_key_nid_postfix(first) == 1, "generating again repairs the pair");
    EcGroup* larger = EC_GROUP_new_by_curve_name_nid_postfix(715);
    EcGroup* smaller = EC_GROUP_new_by_curve_name_nid_postfix(415);
    EcKey* moved = GeneratedKey(715);
    Require(EC_KEY_set_group_nid_postfix(moved, smaller) == 1 && EC_KEY_set_public_key_nid_postfix(moved, EC_KEY_get0_public_key_nid_postfix(second)) == 1, "moving a P-384 key to P-256");
    Require(EC_KEY_check_key_nid_postfix(moved) == 0, "a private key kept from a group with a larger order");
    RequireError(16, 130, "EC_R_WRONG_ORDER");
    EcKey* other = GeneratedKey(715);
    Require(EC_KEY_set_group_nid_postfix(first, larger) == 1 && EC_KEY_set_public_key_nid_postfix(first, EC_KEY_get0_public_key_nid_postfix(other)) == 1, "moving a P-256 key to P-384");
    Require(EC_KEY_check_key_nid_postfix(first) == 0, "a private key kept from a group with a smaller order");
    RequireError(16, 123, "EC_R_INVALID_PRIVATE_KEY for a private key kept from a group with a smaller order");
    EC_KEY_free_nid_postfix(other);
    EC_KEY_free_nid_postfix(moved);
    EC_GROUP_free_nid_postfix(smaller);
    EC_GROUP_free_nid_postfix(larger);
    EC_KEY_free_nid_postfix(second);
    EC_KEY_free_nid_postfix(first);
}

static void CheckKeyArguments() {
    ERR_clear_error_nid_postfix();
    Require(EC_KEY_generate_key_nid_postfix(nullptr) == 0, "EC_KEY_generate_key of a null key");
    RequireError(16, 67, "ERR_R_PASSED_NULL_PARAMETER for a null key");
    EcKey* empty = EC_KEY_new_nid_postfix();
    Require(EC_KEY_generate_key_nid_postfix(empty) == 0, "EC_KEY_generate_key without a group");
    RequireError(16, 67, "ERR_R_PASSED_NULL_PARAMETER for a key without a group");
    Require(EC_KEY_check_key_nid_postfix(nullptr) == 0, "EC_KEY_check_key of a null key");
    RequireError(16, 67, "ERR_R_PASSED_NULL_PARAMETER from EC_KEY_check_key");
    RequireThrows([] { EC_KEY_get0_public_key_nid_postfix(nullptr); }, "EC_KEY_get0_public_key of a null key");
    EcKey* key = GeneratedKey(415);
    RequireThrows([&] { EC_KEY_set_public_key_nid_postfix(nullptr, EC_KEY_get0_public_key_nid_postfix(key)); }, "EC_KEY_set_public_key of a null key");
    Require(EC_KEY_set_public_key_nid_postfix(empty, EC_KEY_get0_public_key_nid_postfix(key)) == 0, "EC_KEY_set_public_key without a group");
    RequireError(16, 67, "ERR_R_PASSED_NULL_PARAMETER from EC_KEY_set_public_key");
    EcKey* other = GeneratedKey(715);
    RequireThrows([&] { EC_KEY_set_public_key_nid_postfix(key, EC_KEY_get0_public_key_nid_postfix(other)); }, "EC_KEY_set_public_key with another curve's point");
    EcGroup* group = EC_GROUP_new_by_curve_name_nid_postfix(715);
    Require(EC_KEY_set_group_nid_postfix(key, group) == 1, "changing a key's group");
    RequireThrows([&] { EC_KEY_check_key_nid_postfix(key); }, "EC_KEY_check_key of a public key from the previous group");
    RequireThrows([&] { EC_KEY_generate_key_nid_postfix(key); }, "EC_KEY_generate_key into a public key from the previous group");
    EC_GROUP_free_nid_postfix(group);
    EC_KEY_free_nid_postfix(other);
    EC_KEY_free_nid_postfix(key);
    EC_KEY_free_nid_postfix(empty);
}

static void AppendDerLength(std::vector<unsigned char>& out, std::size_t length) {
    if (length < 0x80) {
        out.push_back(static_cast<unsigned char>(length));
        return;
    }
    out.push_back(length < 0x100 ? 0x81 : 0x82);
    if (length >= 0x100) out.push_back(static_cast<unsigned char>(length >> 8));
    out.push_back(static_cast<unsigned char>(length));
}

static std::vector<unsigned char> Signature(const std::vector<std::vector<unsigned char>>& integers) {
    std::vector<unsigned char> content;
    for (const std::vector<unsigned char>& integer : integers) {
        content.push_back(0x02);
        AppendDerLength(content, integer.size());
        content.insert(content.end(), integer.begin(), integer.end());
    }
    std::vector<unsigned char> out{0x30};
    AppendDerLength(out, content.size());
    out.insert(out.end(), content.begin(), content.end());
    return out;
}

static std::vector<unsigned char> IntegerContent(std::vector<unsigned char> value) {
    while (!value.empty() && value.front() == 0) value.erase(value.begin());
    if (value.empty() || (value.front() & 0x80) != 0) value.insert(value.begin(), 0);
    return value;
}

static int Verify(const std::vector<unsigned char>& digest, const std::vector<unsigned char>& signature, EcKey* key) {
    return ECDSA_verify_nid_postfix(0, digest.data(), static_cast<int>(digest.size()), signature.data(), static_cast<int>(signature.size()), key);
}

static std::vector<unsigned char> Sign(const std::vector<unsigned char>& digest, EcKey* key) {
    std::vector<unsigned char> signature(static_cast<std::size_t>(ECDSA_size_nid_postfix(key)));
    unsigned int length = 0;
    Require(ECDSA_sign_nid_postfix(0, digest.data(), static_cast<int>(digest.size()), signature.data(), &length, key) == 1, "ECDSA_sign");
    Require(length > 0 && length <= signature.size(), "a signature fits ECDSA_size");
    signature.resize(length);
    return signature;
}

static EcKey* KnownKey(const KnownPoint& known) {
    EcKey* key = EC_KEY_new_by_curve_name_nid_postfix(known.nid);
    EcPoint* point = EC_POINT_new_nid_postfix(EC_KEY_get0_group_nid_postfix(key));
    Require(Decode(EC_KEY_get0_group_nid_postfix(key), point, Bytes(known.uncompressed)) == 1 && EC_KEY_set_public_key_nid_postfix(key, point) == 1, "a key with a known public key");
    EC_POINT_free_nid_postfix(point);
    return key;
}

static void CheckEcdsaKnownAnswers() {
    ERR_clear_error_nid_postfix();
    for (std::size_t i = 0; i < std::size(KnownSignatures); ++i) {
        const KnownSignature& known = KnownSignatures[i];
        Require(KnownPoints[i].nid == known.nid, "the vectors are listed in the same order");
        EcKey* key = KnownKey(KnownPoints[i]);
        const std::vector<unsigned char> digest = Bytes(known.digest);
        const std::vector<unsigned char> der = Bytes(known.der);
        Require(Signature({Bytes(known.r), Bytes(known.s)}) == der, "the vector's DER encodes its r and s");
        Require(Verify(digest, der, key) == 1, "a known-answer signature verifies");
        std::vector<unsigned char> changed = digest;
        changed[0] ^= 1;
        Require(Verify(changed, der, key) == 0, "a known-answer signature over another digest");
        std::vector<unsigned char> r = Bytes(known.r);
        r.back() ^= 1;
        Require(Verify(digest, Signature({r, Bytes(known.s)}), key) == 0, "a known-answer signature with a changed r");
        std::vector<unsigned char> s = Bytes(known.s);
        s.back() ^= 1;
        Require(Verify(digest, Signature({Bytes(known.r), s}), key) == 0, "a known-answer signature with a changed s");
        Require(ERR_get_error_nid_postfix() == 0, "a signature that does not match queues no error");
        EC_KEY_free_nid_postfix(key);
    }
}

static void CheckEcdsaEncodings() {
    const KnownSignature& known = KnownSignatures[1];
    EcKey* key = KnownKey(KnownPoints[1]);
    const std::vector<unsigned char> digest = Bytes(known.digest);
    const std::vector<unsigned char> r = Bytes(known.r);
    const std::vector<unsigned char> s = Bytes(known.s);
    const std::vector<unsigned char> der = Bytes(known.der);
    Require(r.front() == 0 && s.front() == 0, "the P-256 vector's r and s need a leading zero");
    std::vector<unsigned char> bytes = der;
    bytes.push_back(0);
    Require(Verify(digest, bytes, key) == -1, "a signature with a trailing octet");
    bytes = der;
    bytes[0] = 0x31;
    Require(Verify(digest, bytes, key) == -1, "a signature that is not a SEQUENCE");
    bytes = der;
    bytes.insert(bytes.begin() + 1, 0x81);
    Require(Verify(digest, bytes, key) == -1, "a SEQUENCE length in the long form below 128");
    bytes = der;
    bytes[1] = 0x80;
    Require(Verify(digest, bytes, key) == -1, "an indefinite SEQUENCE length");
    bytes = der;
    bytes[2] = 0x03;
    Require(Verify(digest, bytes, key) == -1, "an r that is not an INTEGER");
    bytes = der;
    --bytes[1];
    Require(Verify(digest, bytes, key) == -1, "a SEQUENCE length one short");
    std::vector<unsigned char> padded = r;
    padded.insert(padded.begin(), 0);
    Require(Verify(digest, Signature({padded, s}), key) == -1, "an r with a needless leading zero");
    Require(Verify(digest, Signature({std::vector<unsigned char>(r.begin() + 1, r.end()), s}), key) == -1, "an r whose top bit is set");
    Require(Verify(digest, Signature({r, s, s}), key) == -1, "a third INTEGER");
    Require(Verify(digest, {}, key) == -1, "an empty signature");
    Require(ECDSA_verify_nid_postfix(0, digest.data(), static_cast<int>(digest.size()), nullptr, 0, key) == -1, "a null signature of length zero");
    ERR_clear_error_nid_postfix();
    Require(Verify(digest, Signature({{0}, s}), key) == 0, "an r of zero");
    RequireError(42, 100, "ECDSA_R_BAD_SIGNATURE for an r of zero");
    Require(Verify(digest, Signature({r, {0}}), key) == 0, "an s of zero");
    RequireError(42, 100, "ECDSA_R_BAD_SIGNATURE for an s of zero");
    Require(Verify(digest, Signature({IntegerContent(Bytes(known.order)), s}), key) == 0, "an r equal to the order");
    RequireError(42, 100, "ECDSA_R_BAD_SIGNATURE for an r equal to the order");
    EC_KEY_free_nid_postfix(key);
}

static void CheckEcdsaRoundTrips() {
    std::vector<unsigned char> digest(32);
    for (std::size_t i = 0; i < digest.size(); ++i) digest[i] = static_cast<unsigned char>(0x5a + 3 * i);
    for (const KnownPoint& known : KnownPoints) {
        EcKey* key = GeneratedKey(known.nid);
        EcKey* other = GeneratedKey(known.nid);
        const std::vector<unsigned char> signature = Sign(digest, key);
        Require(Verify(digest, signature, key) == 1, "a signature verifies with its key");
        Require(Verify(digest, signature, other) == 0, "a signature does not verify with another key");
        Require(Sign(digest, key) != signature, "each signature draws a new nonce");
        unsigned int length = 0;
        Require(ECDSA_sign_nid_postfix(0, digest.data(), static_cast<int>(digest.size()), nullptr, &length, key) == 1 && length > 0 && length <= static_cast<unsigned int>(ECDSA_size_nid_postfix(key)), "ECDSA_sign without a buffer reports the length");
        std::vector<unsigned char> changed = digest;
        changed[0] ^= 0x80;
        Require(Verify(changed, signature, key) == 0, "a signature over another digest");
        EC_KEY_free_nid_postfix(other);
        EC_KEY_free_nid_postfix(key);
    }
}

static void CheckEcdsaPreviousGroupKey() {
    const std::vector<unsigned char> digest(32, 0x3c);
    EcKey* key = GeneratedKey(715);
    EcGroup* group = EC_GROUP_new_by_curve_name_nid_postfix(415);
    Require(EC_KEY_set_group_nid_postfix(key, group) == 1, "moving a P-384 key to P-256");
    EcKey* other = GeneratedKey(415);
    ERR_clear_error_nid_postfix();
    Require(Verify(digest, Sign(digest, key), other) == 0 && ERR_get_error_nid_postfix() == 0, "a private key kept from a larger group signs with r and s below the order");
    EC_KEY_free_nid_postfix(other);
    EC_GROUP_free_nid_postfix(group);
    EC_KEY_free_nid_postfix(key);
}

static void CheckEcdsaDigestTruncation() {
    std::vector<unsigned char> digest(70);
    for (std::size_t i = 0; i < digest.size(); ++i) digest[i] = static_cast<unsigned char>(0xa5 ^ (7 * i));
    EcKey* small = GeneratedKey(409);
    const std::vector<unsigned char> smallSignature = Sign(digest, small);
    Require(Verify(std::vector<unsigned char>(digest.begin(), digest.begin() + 24), smallSignature, small) == 1, "P-192 uses the leftmost 24 octets of a long digest");
    std::vector<unsigned char> changed = digest;
    changed[24] ^= 0xff;
    Require(Verify(changed, smallSignature, small) == 1, "octets past the P-192 order length are ignored");
    changed[23] ^= 1;
    Require(Verify(changed, smallSignature, small) == 0, "the last octet within the P-192 order length counts");
    EcKey* large = GeneratedKey(716);
    const std::vector<unsigned char> largeSignature = Sign(digest, large);
    changed = digest;
    changed[65] ^= 0x7f;
    for (std::size_t i = 66; i < changed.size(); ++i) changed[i] ^= 0xff;
    Require(Verify(changed, largeSignature, large) == 1, "P-521 uses only the leftmost 521 bits of a long digest");
    changed[65] ^= 0x80;
    Require(Verify(changed, largeSignature, large) == 0, "the 521st bit counts");
    EC_KEY_free_nid_postfix(large);
    EC_KEY_free_nid_postfix(small);
}

static void CheckEcdsaArguments() {
    const KnownSignature& known = KnownSignatures[1];
    const std::vector<unsigned char> digest = Bytes(known.digest);
    const std::vector<unsigned char> der = Bytes(known.der);
    EcKey* publicOnly = KnownKey(KnownPoints[1]);
    unsigned int length = 99;
    ERR_clear_error_nid_postfix();
    Require(ECDSA_sign_nid_postfix(0, digest.data(), static_cast<int>(digest.size()), nullptr, &length, publicOnly) == 0 && length == 0, "ECDSA_sign without a private key");
    RequireError(42, 67, "ERR_R_PASSED_NULL_PARAMETER from ECDSA_sign");
    EcKey* empty = EC_KEY_new_nid_postfix();
    Require(Verify(digest, der, empty) == -1, "ECDSA_verify without a group or public key");
    RequireError(42, 103, "ECDSA_R_MISSING_PARAMETERS");
    RequireThrows([&] { ECDSA_sign_nid_postfix(0, digest.data(), 32, nullptr, &length, nullptr); }, "ECDSA_sign of a null key");
    RequireThrows([&] { ECDSA_sign_nid_postfix(0, digest.data(), 32, nullptr, nullptr, publicOnly); }, "ECDSA_sign with a null length");
    RequireThrows([&] { Verify(digest, der, nullptr); }, "ECDSA_verify of a null key with a well-formed signature");
    Require(Verify(digest, {}, nullptr) == -1, "a malformed signature is refused before the key is used");
    RequireThrows([&] { ECDSA_verify_nid_postfix(0, digest.data(), -1, der.data(), static_cast<int>(der.size()), publicOnly); }, "a negative digest length");
    EcPoint* point = EC_POINT_new_nid_postfix(EC_KEY_get0_group_nid_postfix(publicOnly));
    Require(EC_KEY_set_public_key_nid_postfix(publicOnly, point) == 1, "a public key at infinity");
    Require(Verify(digest, der, publicOnly) == 0, "ECDSA_verify with a public key at infinity compares u1 times the generator");
    std::vector<unsigned char> offCurve = Bytes(KnownPoints[1].uncompressed);
    offCurve.back() ^= 1;
    Require(Decode(EC_KEY_get0_group_nid_postfix(publicOnly), point, offCurve) == 0 && EC_KEY_set_public_key_nid_postfix(publicOnly, point) == 1, "a public key off the curve");
    RequireThrows([&] { Verify(digest, der, publicOnly); }, "ECDSA_verify with a public key off the curve");
    EcKey* key = GeneratedKey(415);
    EcGroup* group = EC_GROUP_new_by_curve_name_nid_postfix(715);
    Require(EC_KEY_set_group_nid_postfix(key, group) == 1, "changing a key's group");
    RequireThrows([&] { Verify(digest, der, key); }, "ECDSA_verify with a public key from the previous group");
    ERR_clear_error_nid_postfix();
    EC_GROUP_free_nid_postfix(group);
    EC_KEY_free_nid_postfix(key);
    EC_POINT_free_nid_postfix(point);
    EC_KEY_free_nid_postfix(empty);
    EC_KEY_free_nid_postfix(publicOnly);
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
    CheckKeyGeneration();
    CheckKeyPublicKeys();
    CheckKeyPairs();
    CheckKeyArguments();
    CheckEcdsaKnownAnswers();
    CheckEcdsaEncodings();
    CheckEcdsaRoundTrips();
    CheckEcdsaPreviousGroupKey();
    CheckEcdsaDigestTruncation();
    CheckEcdsaArguments();
    return 0;
}
