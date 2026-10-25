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

int main() {
    InitializeHeap();
    TestFlags();
    TestSize();
    TestNew();
    TestFree();
    std::puts("libSceLibreSsl RSA tests passed");
}
