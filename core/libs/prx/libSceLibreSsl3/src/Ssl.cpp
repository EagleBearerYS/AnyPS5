#include "prx/libSceLibreSsl3/libSceLibreSsl3.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

namespace {

constexpr std::uint16_t Dtls1Version = 0xFEFF;
constexpr std::uint16_t Dtls12Version = 0xFEFD;
constexpr std::uint16_t Tls11Version = 0x0302;
constexpr std::uint16_t Tls12Version = 0x0303;

std::uint16_t DtlsToTls(std::uint16_t version) {
    if (version == Dtls1Version)
        return Tls11Version;
    if (version == Dtls12Version)
        return Tls12Version;
    return 0;
}

std::uint16_t TlsToDtls(std::uint16_t version) {
    if (version == Tls11Version)
        return Dtls1Version;
    if (version == Tls12Version)
        return Dtls12Version;
    return 0;
}

bool ClampRange(std::uint16_t& minimum, std::uint16_t& maximum, std::uint16_t lowest, std::uint16_t highest) {
    if (lowest > highest || minimum > maximum)
        return false;
    if (highest < minimum || lowest > maximum)
        return false;
    if (minimum < lowest)
        minimum = lowest;
    if (maximum > highest)
        maximum = highest;
    return true;
}

const LibreSsl3SslMethod& ContextMethod(const LibreSsl3SslCtx* context, const char* function) {
    if (!context)
        throw std::invalid_argument(std::string(function) + " requires a context");
    if (!context->method)
        throw std::invalid_argument(std::string(function) + " requires a context with a method");
    return *static_cast<const LibreSsl3SslMethod*>(context->method);
}

} // namespace

extern "C" {

std::size_t APS5_VABI SSL_get_client_random_nid_postfix(const LibreSsl3Ssl* ssl, unsigned char* out, std::size_t maxOut) {
    std::size_t length = sizeof(LibreSsl3Ssl3State::clientRandom);
    if (!out)
        return length;
    if (!ssl || !ssl->s3)
        throw std::invalid_argument("SSL_get_client_random requires a connection with SSLv3 state");
    if (length > maxOut)
        length = maxOut;
    std::memcpy(out, ssl->s3->clientRandom, length);
    return length;
}

int APS5_VABI SSL_CTX_set_min_proto_version_nid_postfix(LibreSsl3SslCtx* context, std::uint16_t version) {
    const auto& method = ContextMethod(context, "SSL_CTX_set_min_proto_version");
    if (version == 0) {
        context->minTlsVersion = method.minTlsVersion;
        context->minProtoVersion = 0;
        return 1;
    }
    std::uint16_t minimum = version;
    std::uint16_t maximum = context->maxTlsVersion;
    if (method.dtls && (minimum = DtlsToTls(version)) == 0)
        return 0;
    if (!ClampRange(minimum, maximum, method.minTlsVersion, method.maxTlsVersion))
        return 0;
    std::uint16_t protocol = minimum;
    if (method.dtls && (protocol = TlsToDtls(minimum)) == 0)
        return 0;
    context->minTlsVersion = minimum;
    context->minProtoVersion = protocol;
    return 1;
}

int APS5_VABI SSL_CTX_set_max_proto_version_nid_postfix(LibreSsl3SslCtx* context, std::uint16_t version) {
    const auto& method = ContextMethod(context, "SSL_CTX_set_max_proto_version");
    if (version == 0) {
        context->maxTlsVersion = method.maxTlsVersion;
        context->maxProtoVersion = 0;
        return 1;
    }
    std::uint16_t minimum = context->minTlsVersion;
    std::uint16_t maximum = version;
    if (method.dtls && (maximum = DtlsToTls(version)) == 0)
        return 0;
    if (!ClampRange(minimum, maximum, method.minTlsVersion, method.maxTlsVersion))
        return 0;
    std::uint16_t protocol = maximum;
    if (method.dtls && (protocol = TlsToDtls(maximum)) == 0)
        return 0;
    context->maxTlsVersion = maximum;
    context->maxProtoVersion = protocol;
    return 1;
}

}
