#include "prx/libSceLibreSsl/include/Api.hpp"
#include "prx/libc/include/Api.hpp"
#include "prx/libc/include/ApplicationHeap.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>

struct TestBignum {
    std::uint64_t* d;
    int top;
    int dmax;
    int neg;
    int flags;
};

struct TestRsaMethod {
    const char* name;
    void* operations[8];
    int flags;
    char* appData;
    void* sign;
    void* verify;
    void* keygen;
};

struct TestRsa {
    int pad;
    std::int64_t version;
    const TestRsaMethod* meth;
    void* engine;
    void* n;
    void* rest[15];
};

static_assert(offsetof(TestRsaMethod, flags) == 72);
static_assert(offsetof(TestRsa, meth) == 16 && offsetof(TestRsa, n) == 32 && sizeof(TestRsa) == 160);

static const LibreSslRsa* Rsa(const TestRsa* rsa) {
    return reinterpret_cast<const LibreSslRsa*>(rsa);
}

static void Require(bool condition, std::source_location location = std::source_location::current()) {
    if (condition) return;
    std::fprintf(stderr, "LibreSSL RSA check failed at line %u\n", location.line());
    std::abort();
}

template<typename TAction> static void RequireThrows(TAction action, std::source_location location = std::source_location::current()) {
    try { action(); } catch (const std::exception&) { return; }
    Require(false, location);
}

static void TestFlags() {
    TestRsaMethod method {};
    TestRsa rsa {};
    rsa.meth = &method;
    Require(RSA_flags_nid_postfix(nullptr) == 0);
    Require(RSA_flags_nid_postfix(Rsa(&rsa)) == 0);
    method.flags = 0x0400;
    Require(RSA_flags_nid_postfix(Rsa(&rsa)) == 0x0400);
    method.flags = -1;
    Require(RSA_flags_nid_postfix(Rsa(&rsa)) == -1);
    rsa.meth = nullptr;
    RequireThrows([&] { RSA_flags_nid_postfix(Rsa(&rsa)); });
}

static int Size(TestBignum* modulus) {
    TestRsa rsa {};
    rsa.n = modulus;
    return RSA_size_nid_postfix(Rsa(&rsa));
}

static void TestSize() {
    std::uint64_t words[32] {};
    TestBignum modulus {words, 0, 32, 0, 0};
    Require(Size(&modulus) == 0);
    modulus.d = nullptr;
    Require(Size(&modulus) == 0);
    modulus.d = words;
    modulus.top = 1;
    words[0] = 1;
    Require(Size(&modulus) == 1);
    words[0] = 0xFF;
    Require(Size(&modulus) == 1);
    words[0] = 0x100;
    Require(Size(&modulus) == 2);
    words[0] = 0x8000000000000000ull;
    Require(Size(&modulus) == 8);
    modulus.top = 2;
    words[1] = 1;
    Require(Size(&modulus) == 9);
    modulus.top = 32;
    words[31] = 0xC000000000000000ull;
    Require(Size(&modulus) == 256);
    modulus.neg = 1;
    Require(Size(&modulus) == 256);
    modulus.neg = 0;
    modulus.top = 16;
    words[15] = 0x00FFFFFFFFFFFFFFull;
    Require(Size(&modulus) == 127);
    words[15] = 0;
    Require(Size(&modulus) == 120);
    modulus.top = -1;
    RequireThrows([&] { Size(&modulus); });
    modulus.top = 1;
    modulus.d = nullptr;
    RequireThrows([&] { Size(&modulus); });
    RequireThrows([] { Size(nullptr); });
    RequireThrows([] { RSA_size_nid_postfix(nullptr); });
}

constexpr int BignumStaticData = 0x02;
constexpr int BignumMalloced = 0x01;

struct Finishes {
    int calls;
    LibreSslRsa* rsa;
};

static Finishes finishes;

static int APS5_VABI RecordFinish(LibreSslRsa* rsa) {
    ++finishes.calls;
    finishes.rsa = rsa;
    return 1;
}

static void InitializeHeap() {
    void* const api[10] {};
    ApplicationHeapRegister_nid_no_patch(api);
}

static bool Zero(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index)
        if (bytes[index]) return false;
    return true;
}

static TestBignum* AllocatedBignum(std::uint64_t value) {
    auto* number = static_cast<TestBignum*>(malloc_nid_postfix(sizeof(TestBignum)));
    auto* words = static_cast<std::uint64_t*>(malloc_nid_postfix(2 * sizeof(std::uint64_t)));
    Require(number != nullptr && words != nullptr);
    words[0] = value;
    words[1] = 0;
    *number = {words, 1, 2, 0, BignumMalloced};
    return number;
}

static void TestNew() {
    auto* rsa = RSA_new_nid_postfix();
    Require(rsa != nullptr && rsa->meth != nullptr);
    Require(rsa->pad == 0 && rsa->version == 0 && rsa->engine == nullptr && rsa->references == 1 && rsa->flags == 0x0006);
    for (void* field : {rsa->n, rsa->e, rsa->d, rsa->p, rsa->q, rsa->dmp1, rsa->dmq1, rsa->iqmp, rsa->exData}) Require(field == nullptr);
    for (void* field : {rsa->methodModN, rsa->methodModP, rsa->methodModQ, rsa->blinding, rsa->mtBlinding}) Require(field == nullptr);
    const auto* method = rsa->meth;
    Require(std::string(method->name) == "Eric Young's PKCS#1 RSA" && method->flags == 0 && method->appData == nullptr);
    for (void* operation : method->operations) Require(operation != nullptr);
    Require(method->sign == nullptr && method->verify == nullptr && method->keygen == nullptr);
    Require(RSA_flags_nid_postfix(rsa) == 0);
    auto* second = RSA_new_nid_postfix();
    Require(second != nullptr && second != rsa && second->meth == method);
    RSA_free_nid_postfix(second);
    RSA_free_nid_postfix(rsa);
}

static void TestFree() {
    RSA_free_nid_postfix(nullptr);
    auto* rsa = RSA_new_nid_postfix();
    std::uint64_t words[4] {0x1234, 0x5678, 0, 0};
    TestBignum stored {words, 2, 4, 0, BignumStaticData};
    rsa->n = &stored;
    rsa->e = AllocatedBignum(65537);
    rsa->d = AllocatedBignum(3);
    rsa->references = 2;
    RSA_free_nid_postfix(rsa);
    Require(rsa->references == 1 && rsa->n == &stored && stored.top == 2);
    RSA_free_nid_postfix(rsa);
    Require(Zero(&stored, sizeof(stored)) && words[0] == 0x1234 && words[1] == 0x5678);

    rsa = RSA_new_nid_postfix();
    LibreSslRsaMethod method = *rsa->meth;
    method.operations[7] = reinterpret_cast<void*>(RecordFinish);
    rsa->meth = &method;
    finishes = {};
    RSA_free_nid_postfix(rsa);
    Require(finishes.calls == 1 && finishes.rsa == rsa);
    method.operations[7] = nullptr;
    rsa = RSA_new_nid_postfix();
    rsa->meth = &method;
    RSA_free_nid_postfix(rsa);
    Require(finishes.calls == 1);

    int marker = 0;
    for (int field = 0; field < 5; ++field) {
        rsa = RSA_new_nid_postfix();
        void** slots[] {&rsa->engine, &rsa->exData, &rsa->blinding, &rsa->mtBlinding, &rsa->methodModN};
        *slots[field] = &marker;
        RequireThrows([&] { RSA_free_nid_postfix(rsa); });
    }
    rsa = RSA_new_nid_postfix();
    rsa->meth = nullptr;
    RequireThrows([&] { RSA_free_nid_postfix(rsa); });
}

using Bytes = std::vector<unsigned char>;

constexpr const char* CavpModulus =
    "cea80475324c1dc8347827818da58bac069d3419c614a6ea1ac6a3b510dcd72cc516954905e9fef908d45e13006adf27"
    "d467a7d83c111d1a5df15ef293771aefb920032a5bb989f8e4f5e1b05093d3f130f984c07a772a3683f4dc6fb28a9681"
    "5b32123ccdd13954f19d5b8b24a103e771a34c328755c65ed64e1924ffd04d30b2142cc262f6e0048fef6dbc652f2147"
    "9ea1c4b1d66d28f4d46ef7185e390cbfa2e02380582f3188bb94ebbf05d31487a09aff01fcbb4cd4bfd1f0a833b38c11"
    "813c84360bb53c7d4481031c40bad8713bb6b835cb08098ed15ba31ee4ba728a8c8e10f7294e1b4163b7aee57277bfd8"
    "81a6f9d43e02c6925aa3a043fb7fb78d";
constexpr const char* CavpSpki =
    "30820122300d06092a864886f70d01010105000382010f003082010a0282010100cea80475324c1dc8347827818da58b"
    "ac069d3419c614a6ea1ac6a3b510dcd72cc516954905e9fef908d45e13006adf27d467a7d83c111d1a5df15ef293771a"
    "efb920032a5bb989f8e4f5e1b05093d3f130f984c07a772a3683f4dc6fb28a96815b32123ccdd13954f19d5b8b24a103"
    "e771a34c328755c65ed64e1924ffd04d30b2142cc262f6e0048fef6dbc652f21479ea1c4b1d66d28f4d46ef7185e390c"
    "bfa2e02380582f3188bb94ebbf05d31487a09aff01fcbb4cd4bfd1f0a833b38c11813c84360bb53c7d4481031c40bad8"
    "713bb6b835cb08098ed15ba31ee4ba728a8c8e10f7294e1b4163b7aee57277bfd881a6f9d43e02c6925aa3a043fb7fb7"
    "8d0203260445";

static Bytes Hex(const char* text) {
    const auto nibble = [](char digit) { return static_cast<unsigned char>(digit <= '9' ? digit - '0' : digit - 'a' + 10); };
    Bytes bytes;
    for (; text[0] && text[1]; text += 2) bytes.push_back(static_cast<unsigned char>(nibble(text[0]) << 4 | nibble(text[1])));
    return bytes;
}

static Bytes Concat(std::initializer_list<Bytes> parts) {
    Bytes bytes;
    for (const auto& part : parts) bytes.insert(bytes.end(), part.begin(), part.end());
    return bytes;
}

static Bytes Encode(unsigned char tag, const Bytes& content) {
    Bytes bytes {tag};
    if (content.size() < 0x80) {
        bytes.push_back(static_cast<unsigned char>(content.size()));
    } else if (content.size() < 0x100) {
        bytes.insert(bytes.end(), {0x81, static_cast<unsigned char>(content.size())});
    } else {
        bytes.insert(bytes.end(), {0x82, static_cast<unsigned char>(content.size() >> 8), static_cast<unsigned char>(content.size())});
    }
    return Concat({bytes, content});
}

static const Bytes RsaEncryption {0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01};
static const Bytes Null {0x05, 0x00};
static const Bytes CavpExponent {0x26, 0x04, 0x45};

static Bytes RsaKey(const Bytes& modulus, const Bytes& exponent) {
    return Encode(0x30, Concat({Encode(0x02, modulus), Encode(0x02, exponent)}));
}

static Bytes PublicKeyInfo(const Bytes& algorithm, const Bytes& bitString) {
    return Encode(0x30, Concat({Encode(0x30, algorithm), Encode(0x03, bitString)}));
}

static Bytes StandardKey() {
    return RsaKey(Concat({{0x00}, Hex(CavpModulus)}), CavpExponent);
}

static LibreSslRsa* Decode(const Bytes& der, long length, long* consumed = nullptr) {
    const unsigned char* data = der.data();
    auto* rsa = d2i_RSA_PUBKEY_nid_postfix(nullptr, &data, length);
    if (consumed) *consumed = data - der.data();
    return rsa;
}

static const TestBignum& Number(const LibreSslRsa* rsa, bool modulus) {
    return *static_cast<const TestBignum*>(modulus ? rsa->n : rsa->e);
}

static void RequireAccepted(const Bytes& der, std::uint64_t exponent, std::source_location location = std::source_location::current()) {
    long consumed = 0;
    auto* rsa = Decode(der, static_cast<long>(der.size()), &consumed);
    Require(rsa != nullptr && consumed == static_cast<long>(der.size()), location);
    Require(Number(rsa, false).top == (exponent ? 1 : 0) && (!exponent || Number(rsa, false).d[0] == exponent), location);
    RSA_free_nid_postfix(rsa);
}

static void RequireRefused(const Bytes& der, std::source_location location = std::source_location::current()) {
    long consumed = -1;
    Require(Decode(der, static_cast<long>(der.size()), &consumed) == nullptr && consumed == 0, location);
}

static void TestPublicKeyInfo() {
    const auto published = Hex(CavpSpki);
    const auto rsaKey = StandardKey();
    Require(PublicKeyInfo(Concat({RsaEncryption, Null}), Concat({{0x00}, rsaKey})) == published);
    long consumed = 0;
    auto* rsa = Decode(published, static_cast<long>(published.size()), &consumed);
    Require(rsa != nullptr && consumed == 294 && RSA_size_nid_postfix(rsa) == 256);
    auto* fresh = RSA_new_nid_postfix();
    Require(rsa->meth == fresh->meth && rsa->flags == 0x0006 && rsa->references == 1 && rsa->engine == nullptr);
    RSA_free_nid_postfix(fresh);
    const auto& modulus = Number(rsa, true);
    const auto& exponent = Number(rsa, false);
    Require(modulus.dmax == 33 && modulus.top == 32 && modulus.neg == 0 && modulus.flags == BignumMalloced);
    Require(modulus.d[0] == 0x5aa3a043fb7fb78dULL && modulus.d[31] == 0xcea80475324c1dc8ULL);
    Require(exponent.dmax == 1 && exponent.top == 1 && exponent.d[0] == 0x260445 && exponent.flags == BignumMalloced);
    for (void* field : {rsa->d, rsa->p, rsa->q, rsa->dmp1, rsa->dmq1, rsa->iqmp}) Require(field == nullptr);

    LibreSslRsa* previous = RSA_new_nid_postfix();
    LibreSslRsa* target = previous;
    const unsigned char* data = published.data();
    auto* replaced = d2i_RSA_PUBKEY_nid_postfix(&target, &data, 294);
    Require(replaced != nullptr && target == replaced && data == published.data() + 294);
    RSA_free_nid_postfix(replaced);
    RSA_free_nid_postfix(rsa);

    auto padded = Concat({published, Null});
    Require((rsa = Decode(padded, static_cast<long>(padded.size()), &consumed)) != nullptr && consumed == 294);
    RSA_free_nid_postfix(rsa);
    Require(Decode(published, 293, &consumed) == nullptr && consumed == 0);
    RequireRefused(Bytes(published.begin(), published.end() - 1));
    Require(Decode(published, 0) == nullptr && Decode(published, -1) == nullptr);
    RequireThrows([&] { d2i_RSA_PUBKEY_nid_postfix(nullptr, nullptr, 294); });
    RequireThrows([&] {
        const unsigned char* none = nullptr;
        d2i_RSA_PUBKEY_nid_postfix(nullptr, &none, 294);
    });
}

static void TestPublicKeyInfoVariants() {
    const auto rsaKey = StandardKey();
    const auto bits = Concat({{0x00}, rsaKey});
    RequireAccepted(PublicKeyInfo(Concat({Encode(0x06, {0x55, 0x08, 0x01, 0x01}), Null}), bits), 0x260445);
    RequireAccepted(PublicKeyInfo(RsaEncryption, bits), 0x260445);
    for (const Bytes& parameter : {Bytes {0x04, 0x00}, Bytes {0x30, 0x00}, Bytes {0xA0, 0x00}, Bytes {0x02, 0x00}, Bytes {0x01, 0x01, 0xFF}, Bytes {0x05, 0x81, 0x00}})
        RequireAccepted(PublicKeyInfo(Concat({RsaEncryption, parameter}), bits), 0x260445);
    for (const Bytes& parameter : {Bytes {0x05, 0x01, 0x00}, Bytes {0x10, 0x00}, Bytes {0x01, 0x02, 0x00, 0x00}, Bytes {0x06, 0x01, 0x80}, Bytes {0x03, 0x00}, Bytes {0x00, 0x00}})
        RequireRefused(PublicKeyInfo(Concat({RsaEncryption, parameter}), bits));
    RequireRefused(PublicKeyInfo(Concat({RsaEncryption, Null, Null}), bits));
    RequireRefused(PublicKeyInfo(Concat({Encode(0x06, {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01}), Null}), bits));
    RequireRefused(PublicKeyInfo(Concat({Encode(0x06, {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0A}), Null}), bits));
    RequireRefused(PublicKeyInfo(Concat({Encode(0x06, {0x2A, 0x80, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01}), Null}), bits));
    RequireRefused(Encode(0x30, Concat({Encode(0x30, Concat({RsaEncryption, Null})), Encode(0x03, bits), Null})));
    RequireRefused(Encode(0x30, Concat({Encode(0x30, Concat({RsaEncryption, Null})), Bytes {0x00, 0x00}, Encode(0x03, bits)})));
    RequireRefused(Encode(0x30, Encode(0x30, Concat({RsaEncryption, Null}))));
    RequireRefused(Encode(0x10, Concat({Encode(0x30, Concat({RsaEncryption, Null})), Encode(0x03, bits)})));
    RequireRefused(Encode(0x31, Concat({Encode(0x30, Concat({RsaEncryption, Null})), Encode(0x03, bits)})));
    const auto algorithm = Concat({RsaEncryption, Null});
    RequireAccepted(PublicKeyInfo(algorithm, Concat({{0x01}, rsaKey})), 0x260444);
    RequireAccepted(PublicKeyInfo(algorithm, Concat({{0x08}, rsaKey})), 0x260400);
    RequireAccepted(PublicKeyInfo(algorithm, Concat({{0x00}, rsaKey, Null})), 0x260445);
    RequireAccepted(PublicKeyInfo(algorithm, Concat({{0x00}, RsaKey(Hex(CavpModulus), CavpExponent)})), 0x260445);
    RequireAccepted(PublicKeyInfo(algorithm, Concat({{0x00}, RsaKey(Concat({{0x00}, Hex(CavpModulus)}), {})})), 0);
    RequireAccepted(PublicKeyInfo(algorithm, Concat({{0x00}, RsaKey(Concat({{0x00}, Hex(CavpModulus)}), {0x00, 0x00, 0x01, 0x00, 0x01})})), 0x10001);
    RequireRefused(PublicKeyInfo(algorithm, {}));
    RequireRefused(PublicKeyInfo(algorithm, {0x00}));
    RequireRefused(PublicKeyInfo(algorithm, Concat({{0x00}, Encode(0x30, Encode(0x02, Hex(CavpModulus)))})));
    RequireRefused(PublicKeyInfo(algorithm, Concat({{0x00}, Encode(0x30, Concat({Encode(0x02, Hex(CavpModulus)), Encode(0x02, CavpExponent), Encode(0x02, CavpExponent)}))})));
    RequireRefused(PublicKeyInfo(algorithm, Concat({{0x00}, Encode(0x31, Concat({Encode(0x02, Hex(CavpModulus)), Encode(0x02, CavpExponent)}))})));
    auto spki = Hex(CavpSpki);
    auto longForm = Concat({{0x30, 0x83, 0x00, 0x01, 0x22}, Bytes(spki.begin() + 4, spki.end())});
    RequireAccepted(longForm, 0x260445);
    auto highTag = Concat({{0x3F, 0x10}, Bytes(spki.begin() + 1, spki.end())});
    RequireAccepted(highTag, 0x260445);
    RequireRefused(Concat({{0x30, 0x89, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x22}, Bytes(spki.begin() + 4, spki.end())}));
    auto indefinite = Concat({{0x30, 0x80}, Bytes(spki.begin() + 4, spki.end()), {0x00, 0x00}});
    RequireThrows([&] { Decode(indefinite, static_cast<long>(indefinite.size())); });
    auto constructedBits = Encode(0x30, Concat({Encode(0x30, algorithm), Encode(0x23, Encode(0x03, bits))}));
    RequireThrows([&] { Decode(constructedBits, static_cast<long>(constructedBits.size())); });
}

int main() {
    InitializeHeap();
    TestFlags();
    TestSize();
    TestNew();
    TestFree();
    TestPublicKeyInfo();
    TestPublicKeyInfoVariants();
    std::puts("libSceLibreSsl RSA tests passed");
}
