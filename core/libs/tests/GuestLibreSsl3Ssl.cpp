#include "prx/libSceLibreSsl3/libSceLibreSsl3.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <source_location>

static void Require(bool condition, std::source_location location = std::source_location::current()) {
    if (condition)
        return;
    std::fprintf(stderr, "LibreSSL3 SSL check failed at line %u\n", location.line());
    std::abort();
}

static void TestClientRandom() {
    LibreSsl3Ssl3State state;
    std::memset(&state, 0xC3, sizeof(state));
    for (std::size_t index = 0; index < sizeof(state.clientRandom); ++index)
        state.clientRandom[index] = static_cast<unsigned char>(index + 1);
    LibreSsl3Ssl ssl;
    std::memset(&ssl, 0x3C, sizeof(ssl));
    ssl.s3 = &state;
    LibreSsl3Ssl3State stateBefore;
    std::memcpy(&stateBefore, &state, sizeof(state));
    LibreSsl3Ssl sslBefore;
    std::memcpy(&sslBefore, &ssl, sizeof(ssl));

    Require(SSL_get_client_random_nid_postfix(&ssl, nullptr, 0) == 32);
    Require(SSL_get_client_random_nid_postfix(&ssl, nullptr, 5) == 32);
    Require(SSL_get_client_random_nid_postfix(nullptr, nullptr, 64) == 32);

    for (std::size_t maxOut : {std::size_t{0}, std::size_t{1}, std::size_t{10}, std::size_t{31}, std::size_t{32}, std::size_t{33}, std::size_t{64}}) {
        std::array<unsigned char, 80> out;
        out.fill(0xEE);
        const std::size_t expected = maxOut < 32 ? maxOut : 32;
        Require(SSL_get_client_random_nid_postfix(&ssl, out.data(), maxOut) == expected);
        for (std::size_t index = 0; index < out.size(); ++index)
            Require(out[index] == (index < expected ? static_cast<unsigned char>(index + 1) : 0xEE));
    }

    Require(std::memcmp(&state, &stateBefore, sizeof(state)) == 0);
    Require(std::memcmp(&ssl, &sslBefore, sizeof(ssl)) == 0);
}

struct GuardedContext {
    std::array<std::uint64_t, 2> before;
    LibreSsl3SslCtx context;
    std::array<std::uint64_t, 2> after;
};

static void Prepare(GuardedContext& guarded, const LibreSsl3SslMethod& method, std::uint16_t minTls, std::uint16_t maxTls, std::uint16_t minProto, std::uint16_t maxProto) {
    std::memset(&guarded, 0x6B, sizeof(guarded));
    guarded.context.method = &method;
    guarded.context.minTlsVersion = minTls;
    guarded.context.maxTlsVersion = maxTls;
    guarded.context.minProtoVersion = minProto;
    guarded.context.maxProtoVersion = maxProto;
}

static void RequireVersions(const GuardedContext& guarded, const GuardedContext& reference, std::uint16_t minTls, std::uint16_t maxTls, std::uint16_t minProto, std::uint16_t maxProto, std::source_location location = std::source_location::current()) {
    Require(guarded.context.minTlsVersion == minTls, location);
    Require(guarded.context.maxTlsVersion == maxTls, location);
    Require(guarded.context.minProtoVersion == minProto, location);
    Require(guarded.context.maxProtoVersion == maxProto, location);
    GuardedContext expected;
    std::memcpy(&expected, &reference, sizeof(expected));
    expected.context.minTlsVersion = minTls;
    expected.context.maxTlsVersion = maxTls;
    expected.context.minProtoVersion = minProto;
    expected.context.maxProtoVersion = maxProto;
    Require(std::memcmp(&guarded, &expected, sizeof(guarded)) == 0, location);
}

static void TestTlsVersions() {
    LibreSsl3SslMethod method;
    std::memset(&method, 0, sizeof(method));
    method.minTlsVersion = 0x0301;
    method.maxTlsVersion = 0x0304;
    LibreSsl3SslMethod methodBefore;
    std::memcpy(&methodBefore, &method, sizeof(method));
    GuardedContext guarded;
    Prepare(guarded, method, 0x0301, 0x0304, 0, 0);
    GuardedContext reference;
    std::memcpy(&reference, &guarded, sizeof(guarded));

    Require(SSL_CTX_set_min_proto_version_nid_postfix(&guarded.context, 0x0303) == 1);
    RequireVersions(guarded, reference, 0x0303, 0x0304, 0x0303, 0);
    Require(SSL_CTX_set_max_proto_version_nid_postfix(&guarded.context, 0x0302) == 0);
    RequireVersions(guarded, reference, 0x0303, 0x0304, 0x0303, 0);
    Require(SSL_CTX_set_max_proto_version_nid_postfix(&guarded.context, 0x0303) == 1);
    RequireVersions(guarded, reference, 0x0303, 0x0303, 0x0303, 0x0303);
    Require(SSL_CTX_set_min_proto_version_nid_postfix(&guarded.context, 0x0304) == 0);
    RequireVersions(guarded, reference, 0x0303, 0x0303, 0x0303, 0x0303);
    Require(SSL_CTX_set_max_proto_version_nid_postfix(&guarded.context, 0) == 1);
    RequireVersions(guarded, reference, 0x0303, 0x0304, 0x0303, 0);
    Require(SSL_CTX_set_min_proto_version_nid_postfix(&guarded.context, 0) == 1);
    RequireVersions(guarded, reference, 0x0301, 0x0304, 0, 0);

    Require(SSL_CTX_set_min_proto_version_nid_postfix(&guarded.context, 0x0300) == 1);
    RequireVersions(guarded, reference, 0x0301, 0x0304, 0x0301, 0);
    Require(SSL_CTX_set_max_proto_version_nid_postfix(&guarded.context, 0x0306) == 1);
    RequireVersions(guarded, reference, 0x0301, 0x0304, 0x0301, 0x0304);
    Require(SSL_CTX_set_min_proto_version_nid_postfix(&guarded.context, 0x0305) == 0);
    RequireVersions(guarded, reference, 0x0301, 0x0304, 0x0301, 0x0304);
    Require(SSL_CTX_set_max_proto_version_nid_postfix(&guarded.context, 0x0200) == 0);
    RequireVersions(guarded, reference, 0x0301, 0x0304, 0x0301, 0x0304);
    Require(SSL_CTX_set_min_proto_version_nid_postfix(&guarded.context, 0xFEFD) == 0);
    RequireVersions(guarded, reference, 0x0301, 0x0304, 0x0301, 0x0304);

    Prepare(guarded, method, 0x0305, 0x0306, 0x0305, 0x0306);
    std::memcpy(&reference, &guarded, sizeof(guarded));
    Require(SSL_CTX_set_min_proto_version_nid_postfix(&guarded.context, 0x0305) == 0);
    RequireVersions(guarded, reference, 0x0305, 0x0306, 0x0305, 0x0306);
    Require(SSL_CTX_set_max_proto_version_nid_postfix(&guarded.context, 0x0303) == 0);
    RequireVersions(guarded, reference, 0x0305, 0x0306, 0x0305, 0x0306);

    LibreSsl3SslMethod inverted;
    std::memcpy(&inverted, &method, sizeof(method));
    inverted.minTlsVersion = 0x0304;
    inverted.maxTlsVersion = 0x0301;
    Prepare(guarded, inverted, 0x0301, 0x0304, 0, 0);
    std::memcpy(&reference, &guarded, sizeof(guarded));
    Require(SSL_CTX_set_min_proto_version_nid_postfix(&guarded.context, 0x0303) == 0);
    Require(SSL_CTX_set_max_proto_version_nid_postfix(&guarded.context, 0x0303) == 0);
    RequireVersions(guarded, reference, 0x0301, 0x0304, 0, 0);
    Require(SSL_CTX_set_min_proto_version_nid_postfix(&guarded.context, 0) == 1);
    Require(SSL_CTX_set_max_proto_version_nid_postfix(&guarded.context, 0) == 1);
    RequireVersions(guarded, reference, 0x0304, 0x0301, 0, 0);

    Require(std::memcmp(&method, &methodBefore, sizeof(method)) == 0);
}

static void TestDtlsVersions() {
    LibreSsl3SslMethod method;
    std::memset(&method, 0, sizeof(method));
    method.dtls = 1;
    method.minTlsVersion = 0x0302;
    method.maxTlsVersion = 0x0303;
    GuardedContext guarded;
    Prepare(guarded, method, 0x0302, 0x0303, 0, 0);
    GuardedContext reference;
    std::memcpy(&reference, &guarded, sizeof(guarded));

    Require(SSL_CTX_set_min_proto_version_nid_postfix(&guarded.context, 0xFEFD) == 1);
    RequireVersions(guarded, reference, 0x0303, 0x0303, 0xFEFD, 0);
    Require(SSL_CTX_set_max_proto_version_nid_postfix(&guarded.context, 0xFEFF) == 0);
    RequireVersions(guarded, reference, 0x0303, 0x0303, 0xFEFD, 0);
    Require(SSL_CTX_set_min_proto_version_nid_postfix(&guarded.context, 0xFEFF) == 1);
    RequireVersions(guarded, reference, 0x0302, 0x0303, 0xFEFF, 0);
    Require(SSL_CTX_set_max_proto_version_nid_postfix(&guarded.context, 0xFEFF) == 1);
    RequireVersions(guarded, reference, 0x0302, 0x0302, 0xFEFF, 0xFEFF);
    Require(SSL_CTX_set_max_proto_version_nid_postfix(&guarded.context, 0) == 1);
    RequireVersions(guarded, reference, 0x0302, 0x0303, 0xFEFF, 0);
    Require(SSL_CTX_set_min_proto_version_nid_postfix(&guarded.context, 0) == 1);
    RequireVersions(guarded, reference, 0x0302, 0x0303, 0, 0);

    for (std::uint16_t version : {std::uint16_t{0x0302}, std::uint16_t{0x0303}, std::uint16_t{0xFEFE}, std::uint16_t{0xFEFC}}) {
        Require(SSL_CTX_set_min_proto_version_nid_postfix(&guarded.context, version) == 0);
        Require(SSL_CTX_set_max_proto_version_nid_postfix(&guarded.context, version) == 0);
        RequireVersions(guarded, reference, 0x0302, 0x0303, 0, 0);
    }

    LibreSsl3SslMethod wide;
    std::memcpy(&wide, &method, sizeof(method));
    wide.minTlsVersion = 0x0301;
    wide.maxTlsVersion = 0x0304;
    Prepare(guarded, wide, 0x0301, 0x0304, 0, 0);
    std::memcpy(&reference, &guarded, sizeof(guarded));
    Require(SSL_CTX_set_min_proto_version_nid_postfix(&guarded.context, 0) == 1);
    RequireVersions(guarded, reference, 0x0301, 0x0304, 0, 0);
    Require(SSL_CTX_set_max_proto_version_nid_postfix(&guarded.context, 0xFEFD) == 1);
    RequireVersions(guarded, reference, 0x0301, 0x0303, 0, 0xFEFD);
    Prepare(guarded, wide, 0x0301, 0x0304, 0, 0);
    std::memcpy(&reference, &guarded, sizeof(guarded));
    Require(SSL_CTX_set_min_proto_version_nid_postfix(&guarded.context, 0xFEFF) == 1);
    RequireVersions(guarded, reference, 0x0302, 0x0304, 0xFEFF, 0);
    wide.maxTlsVersion = 0x0302;
    Prepare(guarded, wide, 0x0301, 0x0302, 0, 0);
    std::memcpy(&reference, &guarded, sizeof(guarded));
    Require(SSL_CTX_set_min_proto_version_nid_postfix(&guarded.context, 0xFEFD) == 0);
    RequireVersions(guarded, reference, 0x0301, 0x0302, 0, 0);
    wide.minTlsVersion = 0x0304;
    wide.maxTlsVersion = 0x0304;
    Prepare(guarded, wide, 0x0304, 0x0304, 0, 0);
    std::memcpy(&reference, &guarded, sizeof(guarded));
    Require(SSL_CTX_set_min_proto_version_nid_postfix(&guarded.context, 0xFEFF) == 0);
    RequireVersions(guarded, reference, 0x0304, 0x0304, 0, 0);
    wide.minTlsVersion = 0x0301;
    wide.maxTlsVersion = 0x0301;
    Prepare(guarded, wide, 0x0301, 0x0302, 0, 0);
    std::memcpy(&reference, &guarded, sizeof(guarded));
    Require(SSL_CTX_set_max_proto_version_nid_postfix(&guarded.context, 0xFEFF) == 0);
    RequireVersions(guarded, reference, 0x0301, 0x0302, 0, 0);
}

int main() {
    TestClientRandom();
    TestTlsVersions();
    TestDtlsVersions();
    std::puts("libSceLibreSsl3 SSL tests passed");
}
