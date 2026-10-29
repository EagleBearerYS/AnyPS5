#pragma once

#include <cstddef>
#include <cstdint>

struct LibreSsl3EvpPkey {
    std::int32_t type;
    std::int32_t references;
    const void* method;
    void* key;
    std::int32_t saveParameters;
};

struct LibreSsl3EcKey {
    const void* method;
    std::int32_t version;
    void* group;
    void* publicKey;
    void* privateKey;
    std::uint32_t encodingFlags;
    std::int32_t conversionForm;
    std::int32_t references;
    std::int32_t flags;
    void* extraData;
};

struct LibreSsl3SslCtx {
    const void* method;
    const void* quicMethod;
    void* cipherList;
    void* certStore;
    std::int64_t sessionTimeout;
    std::int32_t references;
    void* extraCerts;
    std::int32_t verifyMode;
    std::uint64_t sessionIdContextLength;
    unsigned char sessionIdContext[32];
    void* param;
    void* defaultPasswordCallback;
    void* defaultPasswordCallbackData;
    std::uint16_t minTlsVersion;
    std::uint16_t maxTlsVersion;
    std::uint16_t minProtoVersion;
    std::uint16_t maxProtoVersion;
};

static_assert(sizeof(LibreSsl3EvpPkey) == 32);
static_assert(offsetof(LibreSsl3EvpPkey, references) == 4);
static_assert(sizeof(LibreSsl3EcKey) == 64);
static_assert(offsetof(LibreSsl3EcKey, references) == 48);
static_assert(offsetof(LibreSsl3SslCtx, references) == 40);
static_assert(offsetof(LibreSsl3SslCtx, minTlsVersion) == 128);
static_assert(offsetof(LibreSsl3SslCtx, maxTlsVersion) == 130);
static_assert(offsetof(LibreSsl3SslCtx, minProtoVersion) == 132);
static_assert(offsetof(LibreSsl3SslCtx, maxProtoVersion) == 134);

struct LibreSsl3Bio {
    const void* method;
    void* callback;
    void* callbackEx;
    char* callbackArgument;
    std::int32_t initialized;
    std::int32_t shutdown;
    std::int32_t flags;
    std::int32_t retryReason;
    std::int32_t number;
    void* data;
    LibreSsl3Bio* nextBio;
    LibreSsl3Bio* previousBio;
    std::int32_t references;
    std::uint64_t bytesRead;
    std::uint64_t bytesWritten;
    void* exData;
};

struct LibreSsl3SslMethod {
    std::int32_t dtls;
    std::int32_t server;
    std::int32_t version;
    std::uint16_t minTlsVersion;
    std::uint16_t maxTlsVersion;
};

struct LibreSsl3SslSession {
    std::int32_t sslVersion;
    std::uint64_t masterKeyLength;
    unsigned char masterKey[48];
    std::uint64_t sessionIdLength;
    unsigned char sessionId[32];
    std::uint64_t sessionIdContextLength;
    unsigned char sessionIdContext[32];
    void* peerCertificate;
    std::int32_t peerCertificateType;
    std::int64_t verifyResult;
    std::int64_t timeout;
    std::int64_t time;
    std::int32_t references;
};

struct LibreSsl3Ssl3State {
    std::int64_t flags;
    unsigned char serverRandom[32];
    unsigned char clientRandom[32];
};

struct LibreSsl3Ssl {
    std::int32_t version;
    const LibreSsl3SslMethod* method;
    const void* quicMethod;
    LibreSsl3Bio* readBio;
    LibreSsl3Bio* writeBio;
    LibreSsl3Bio* bufferBio;
    std::int32_t server;
    LibreSsl3Ssl3State* s3;
    void* d1;
    void* param;
    void* cipherList;
    void* certificate;
    std::uint64_t sessionIdContextLength;
    unsigned char sessionIdContext[32];
    LibreSsl3SslSession* session;
    std::int32_t verifyMode;
    std::int32_t error;
    std::int32_t errorCode;
    LibreSsl3SslCtx* context;
    std::int64_t verifyResult;
    std::int32_t references;
};

static_assert(sizeof(LibreSsl3Bio) == 112);
static_assert(offsetof(LibreSsl3Bio, references) == 80);
static_assert(offsetof(LibreSsl3SslMethod, minTlsVersion) == 12);
static_assert(offsetof(LibreSsl3SslMethod, maxTlsVersion) == 14);
static_assert(offsetof(LibreSsl3SslSession, references) == 184);
static_assert(offsetof(LibreSsl3Ssl3State, clientRandom) == 40);
static_assert(offsetof(LibreSsl3Ssl, s3) == 56);
static_assert(offsetof(LibreSsl3Ssl, references) == 176);
