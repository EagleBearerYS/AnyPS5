#include "prx/libSceLibreSsl/include/Api.hpp"
#include <atomic>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "prx/libc/include/Api.hpp"
#include "prx/libc/include/General.hpp"

struct LibreSslBignum {
    std::uint64_t* d;
    int top;
    int dmax;
    int neg;
    int flags;
};

static_assert(sizeof(LibreSslBignum) == 24);
static_assert(offsetof(LibreSslBignum, top) == 8);
static_assert(sizeof(LibreSslRsaMethod) == 112);
static_assert(offsetof(LibreSslRsaMethod, flags) == 72);
static_assert(sizeof(LibreSslRsa) == 152);
static_assert(offsetof(LibreSslRsa, meth) == 16);
static_assert(offsetof(LibreSslRsa, n) == 32);
static_assert(offsetof(LibreSslRsa, flags) == 108);

namespace {

using RsaCallback = int (APS5_VABI*)(LibreSslRsa* rsa);

constexpr int OperationInit = 6;
constexpr int OperationFinish = 7;
constexpr int RsaFlagCachePublic = 0x0002;
constexpr int RsaFlagCachePrivate = 0x0004;
constexpr int RsaFlagNonFipsAllow = 0x0400;
constexpr int BignumFlagStaticData = 0x02;
constexpr int BignumFlagMalloced = 0x01;

template<typename TFunction> TFunction Operation(const LibreSslRsaMethod& method, int index) {
    return reinterpret_cast<TFunction>(method.operations[index]);
}

void Wipe(void* data, std::size_t size) {
    auto* bytes = static_cast<volatile unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) bytes[index] = 0;
}

void ClearFreeBignum(LibreSslBignum* number) {
    if (!number) return;
    if (number->d && !(number->flags & BignumFlagStaticData)) {
        if (number->dmax < 0) throw std::invalid_argument("BN_clear_free: negative word capacity");
        Wipe(number->d, static_cast<std::size_t>(number->dmax) * sizeof(number->d[0]));
        free_nid_postfix(number->d);
    }
    const bool allocated = number->flags & BignumFlagMalloced;
    Wipe(number, sizeof(LibreSslBignum));
    if (allocated) free_nid_postfix(number);
}

LibreSslBignum* NewBignum(const unsigned char* bytes, std::size_t length) {
    constexpr std::size_t maxWords = INT_MAX / (4 * 64);
    auto* number = static_cast<LibreSslBignum*>(malloc_nid_postfix(sizeof(LibreSslBignum)));
    if (!number) return nullptr;
    *number = {nullptr, 0, 0, 0, BignumFlagMalloced};
    if (!length) return number;
    const std::size_t words = (length - 1) / 8 + 1;
    if (words <= maxWords) number->d = static_cast<std::uint64_t*>(malloc_nid_postfix(words * sizeof(std::uint64_t)));
    if (!number->d) {
        ClearFreeBignum(number);
        return nullptr;
    }
    number->dmax = static_cast<int>(words);
    for (std::size_t word = 0; word < words; ++word) {
        std::uint64_t value = 0;
        for (std::size_t byte = 0; byte < 8; ++byte) {
            const std::size_t position = word * 8 + byte;
            if (position < length) value |= std::uint64_t(bytes[length - 1 - position]) << (8 * byte);
        }
        number->d[word] = value;
    }
    number->top = static_cast<int>(words);
    while (number->top > 0 && !number->d[number->top - 1]) --number->top;
    return number;
}

struct Element {
    int tag;
    int tagClass;
    bool constructed;
    bool indefinite;
    const unsigned char* content;
    long length;
    const unsigned char* end;
};

struct Cursor {
    const unsigned char* data;
    long length;
};

[[noreturn]] void UnsupportedEncoding(const char* function, const char* what) {
    NotImplemented_nid_no_patch((std::string(function) + ": BER " + what).c_str());
}

bool ReadHeader(const unsigned char* start, long available, Element& element) {
    if (available <= 0) return false;
    const unsigned char* data = start;
    long remaining = available;
    const bool constructed = *data & 0x20;
    const int tagClass = *data & 0xC0;
    int tag = *data & 0x1F;
    if (tag == 0x1F) {
        ++data;
        if (--remaining == 0) return false;
        long value = 0;
        while (*data & 0x80) {
            value = value << 7 | (*data++ & 0x7F);
            if (--remaining == 0) return false;
            if (value > (INT_MAX >> 7)) return false;
        }
        value = value << 7 | (*data++ & 0x7F);
        tag = static_cast<int>(value);
        if (--remaining == 0) return false;
    } else {
        ++data;
        if (--remaining == 0) return false;
    }
    int lengthBytes = static_cast<int>(remaining);
    if (lengthBytes-- < 1) return false;
    bool indefinite = false;
    unsigned long length = 0;
    if (*data == 0x80) {
        indefinite = true;
        ++data;
    } else {
        unsigned count = *data & 0x7F;
        if (*data++ & 0x80) {
            if (lengthBytes < static_cast<int>(count)) return false;
            while (count && !*data) {
                ++data;
                --count;
            }
            if (count > sizeof(long)) return false;
            while (count-- > 0) length = length << 8 | *data++;
        } else {
            length = count;
        }
    }
    if (length > static_cast<unsigned long>(LONG_MAX)) return false;
    if (indefinite && !constructed) return false;
    const long header = data - start;
    if (static_cast<long>(length) > available - header) return false;
    element = {tag, tagClass, constructed, indefinite, data, static_cast<long>(length), data + length};
    return true;
}

bool ReadTop(const unsigned char* data, long length, int tag, Element& element) {
    return ReadHeader(data, length, element) && element.tag == tag && element.tagClass == 0;
}

enum class Field { Present, Absent, Failed };

Field ReadField(Cursor& cursor, int tag, Element& element) {
    if (!cursor.length) return Field::Absent;
    if (cursor.length >= 2 && !cursor.data[0] && !cursor.data[1]) return Field::Failed;
    if (!ReadHeader(cursor.data, cursor.length, element)) return Field::Failed;
    if (tag >= 0 && (element.tag != tag || element.tagClass != 0)) return Field::Failed;
    return Field::Present;
}

void Advance(Cursor& cursor, const Element& element) {
    cursor.length -= element.end - cursor.data;
    cursor.data = element.end;
}

bool ValidObject(const Element& element) {
    if (element.length <= 0 || element.length > INT_MAX || (element.content[element.length - 1] & 0x80)) return false;
    for (long index = 0; index < element.length; ++index)
        if (element.content[index] == 0x80 && (!index || !(element.content[index - 1] & 0x80))) return false;
    return true;
}

bool ValidAny(const Element& element, const char* function) {
    if (element.tagClass != 0) {
        if (element.indefinite) UnsupportedEncoding(function, "indefinite-length parameter");
        return true;
    }
    if (element.tag == 16 || element.tag == 17) {
        if (!element.constructed) return false;
        if (element.indefinite) UnsupportedEncoding(function, "indefinite-length parameter");
        return true;
    }
    if (element.constructed) UnsupportedEncoding(function, "constructed parameter");
    switch (element.tag) {
    case 1: return element.length == 1;
    case 3: return element.length >= 1;
    case 5: return element.length == 0;
    case 6: return ValidObject(element);
    case 28: return element.length % 4 == 0;
    case 30: return element.length % 2 == 0;
    default: return true;
    }
}

struct Algorithm {
    const unsigned char* object;
    long objectLength;
    bool parameter;
    bool nullParameter;
};

bool ReadSequence(Cursor& cursor, Element& element, const char* function) {
    if (ReadField(cursor, 16, element) != Field::Present || !element.constructed) return false;
    if (element.indefinite) UnsupportedEncoding(function, "indefinite-length SEQUENCE");
    return true;
}

bool ReadPrimitive(Cursor& cursor, int tag, Element& element, const char* function) {
    if (ReadField(cursor, tag, element) != Field::Present) return false;
    if (element.constructed) UnsupportedEncoding(function, "constructed string");
    Advance(cursor, element);
    return true;
}

bool ReadAlgorithm(Cursor& cursor, Algorithm& algorithm, const char* function) {
    Element sequence;
    if (!ReadSequence(cursor, sequence, function)) return false;
    Advance(cursor, sequence);
    Cursor fields {sequence.content, sequence.length};
    Element object;
    if (!ReadPrimitive(fields, 6, object, function) || !ValidObject(object)) return false;
    algorithm = {object.content, object.length, false, false};
    if (fields.length) {
        Element parameter;
        if (ReadField(fields, -1, parameter) != Field::Present || !ValidAny(parameter, function)) return false;
        Advance(fields, parameter);
        algorithm.parameter = true;
        algorithm.nullParameter = parameter.tagClass == 0 && parameter.tag == 5;
    }
    return !fields.length;
}

template<std::size_t Size> bool SameObject(const Algorithm& algorithm, const unsigned char (&object)[Size]) {
    return algorithm.objectLength == static_cast<long>(Size) && !std::memcmp(algorithm.object, object, Size);
}

constexpr unsigned char RsaEncryptionObject[] {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01};
constexpr unsigned char RsaObject[] {0x55, 0x08, 0x01, 0x01};

LibreSslRsa* DecodeRsaPublicKey(const unsigned char* data, long length, const char* function) {
    Element sequence;
    if (!ReadTop(data, length, 16, sequence) || !sequence.constructed) return nullptr;
    if (sequence.indefinite) UnsupportedEncoding(function, "indefinite-length SEQUENCE");
    Cursor fields {sequence.content, sequence.length};
    Element modulus;
    Element exponent;
    if (!ReadPrimitive(fields, 2, modulus, function) || !ReadPrimitive(fields, 2, exponent, function) || fields.length) return nullptr;
    auto* rsa = RSA_new_nid_postfix();
    if (!rsa) return nullptr;
    rsa->n = NewBignum(modulus.content, static_cast<std::size_t>(modulus.length));
    rsa->e = rsa->n ? NewBignum(exponent.content, static_cast<std::size_t>(exponent.length)) : nullptr;
    if (!rsa->e) {
        RSA_free_nid_postfix(rsa);
        return nullptr;
    }
    return rsa;
}

int APS5_VABI DefaultPublicEncrypt(int, const unsigned char*, unsigned char*, LibreSslRsa*, int) {
    NotImplemented_nid_no_patch("RSA_eay_public_encrypt");
}

int APS5_VABI DefaultPublicDecrypt(int, const unsigned char*, unsigned char*, LibreSslRsa*, int) {
    NotImplemented_nid_no_patch("RSA_eay_public_decrypt");
}

int APS5_VABI DefaultPrivateEncrypt(int, const unsigned char*, unsigned char*, LibreSslRsa*, int) {
    NotImplemented_nid_no_patch("RSA_eay_private_encrypt");
}

int APS5_VABI DefaultPrivateDecrypt(int, const unsigned char*, unsigned char*, LibreSslRsa*, int) {
    NotImplemented_nid_no_patch("RSA_eay_private_decrypt");
}

int APS5_VABI DefaultModExp(void*, const void*, LibreSslRsa*, void*) {
    NotImplemented_nid_no_patch("RSA_eay_mod_exp");
}

int APS5_VABI DefaultBignumModExp(void*, const void*, const void*, const void*, void*, void*) {
    NotImplemented_nid_no_patch("BN_mod_exp_mont_ct");
}

int APS5_VABI DefaultInit(LibreSslRsa* rsa) {
    rsa->flags |= RsaFlagCachePublic | RsaFlagCachePrivate;
    return 1;
}

int APS5_VABI DefaultFinish(LibreSslRsa* rsa) {
    if (rsa->methodModN || rsa->methodModP || rsa->methodModQ) NotImplemented_nid_no_patch("RSA_eay_finish: freeing a Montgomery context");
    return 1;
}

const LibreSslRsaMethod DefaultMethod {
    "Eric Young's PKCS#1 RSA",
    {
        reinterpret_cast<void*>(DefaultPublicEncrypt),
        reinterpret_cast<void*>(DefaultPublicDecrypt),
        reinterpret_cast<void*>(DefaultPrivateEncrypt),
        reinterpret_cast<void*>(DefaultPrivateDecrypt),
        reinterpret_cast<void*>(DefaultModExp),
        reinterpret_cast<void*>(DefaultBignumModExp),
        reinterpret_cast<void*>(DefaultInit),
        reinterpret_cast<void*>(DefaultFinish),
    },
    0,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

int ByteCount(const LibreSslBignum* number, const char* function) {
    if (!number) throw std::invalid_argument(std::string(function) + ": null modulus");
    if (number->top == 0) return 0;
    if (number->top < 0) throw std::invalid_argument(std::string(function) + ": negative modulus word count");
    if (!number->d) throw std::invalid_argument(std::string(function) + ": null modulus words");
    const std::int64_t bits = static_cast<std::int64_t>(number->top - 1) * 64 + std::bit_width(number->d[number->top - 1]);
    if (bits > INT_MAX - 7) throw std::invalid_argument(std::string(function) + ": modulus bit count overflows int");
    return static_cast<int>((bits + 7) / 8);
}

}

extern "C" {

int APS5_VABI RSA_flags_nid_postfix(const LibreSslRsa* rsa) {
    if (!rsa) return 0;
    if (!rsa->meth) throw std::invalid_argument(std::string(__func__) + ": null method");
    return rsa->meth->flags;
}

int APS5_VABI RSA_size_nid_postfix(const LibreSslRsa* rsa) {
    if (!rsa) throw std::invalid_argument(std::string(__func__) + ": null key");
    return ByteCount(static_cast<const LibreSslBignum*>(rsa->n), __func__);
}

LibreSslRsa* APS5_VABI RSA_new_nid_postfix(void) {
    auto* rsa = static_cast<LibreSslRsa*>(malloc_nid_postfix(sizeof(LibreSslRsa)));
    if (!rsa) return nullptr;
    rsa->meth = &DefaultMethod;
    rsa->engine = nullptr;
    rsa->pad = 0;
    rsa->version = 0;
    rsa->n = rsa->e = rsa->d = rsa->p = rsa->q = rsa->dmp1 = rsa->dmq1 = rsa->iqmp = nullptr;
    rsa->references = 1;
    rsa->methodModN = rsa->methodModP = rsa->methodModQ = nullptr;
    rsa->blinding = rsa->mtBlinding = nullptr;
    rsa->flags = rsa->meth->flags & ~RsaFlagNonFipsAllow;
    rsa->exData = nullptr;
    const auto init = Operation<RsaCallback>(*rsa->meth, OperationInit);
    if (init && !init(rsa)) {
        free_nid_postfix(rsa);
        return nullptr;
    }
    return rsa;
}

void APS5_VABI RSA_free_nid_postfix(LibreSslRsa* rsa) {
    if (!rsa) return;
    if (std::atomic_ref<int>(rsa->references).fetch_sub(1) - 1 > 0) return;
    if (!rsa->meth) throw std::invalid_argument(std::string(__func__) + ": null method");
    if (rsa->engine) NotImplemented_nid_no_patch("RSA_free: releasing an ENGINE");
    if (rsa->exData) NotImplemented_nid_no_patch("RSA_free: freeing ex_data");
    if (rsa->blinding || rsa->mtBlinding) NotImplemented_nid_no_patch("RSA_free: freeing blinding");
    if (const auto finish = Operation<RsaCallback>(*rsa->meth, OperationFinish)) finish(rsa);
    for (void* number : {rsa->n, rsa->e, rsa->d, rsa->p, rsa->q, rsa->dmp1, rsa->dmq1, rsa->iqmp})
        ClearFreeBignum(static_cast<LibreSslBignum*>(number));
    free_nid_postfix(rsa);
}

LibreSslRsa* APS5_VABI d2i_RSA_PUBKEY_nid_postfix(LibreSslRsa** out, const unsigned char** input, long length) {
    if (!input) throw std::invalid_argument(std::string(__func__) + ": null input pointer");
    const unsigned char* data = *input;
    if (length <= 0) return nullptr;
    if (!data) throw std::invalid_argument(std::string(__func__) + ": null input");
    Element publicKey;
    if (!ReadTop(data, length, 16, publicKey) || !publicKey.constructed) return nullptr;
    if (publicKey.indefinite) UnsupportedEncoding(__func__, "indefinite-length SEQUENCE");
    Cursor fields {publicKey.content, publicKey.length};
    Algorithm algorithm;
    Element bits;
    if (!ReadAlgorithm(fields, algorithm, __func__) || !ReadPrimitive(fields, 3, bits, __func__) || bits.length < 1 || fields.length) return nullptr;
    if (!SameObject(algorithm, RsaEncryptionObject) && !SameObject(algorithm, RsaObject)) return nullptr;
    const unsigned unused = bits.content[0];
    std::vector<unsigned char> key(bits.content + 1, bits.content + bits.length);
    if (!key.empty()) {
        if (unused >= 32) UnsupportedEncoding(__func__, "BIT STRING with 32 or more unused bits");
        key.back() &= static_cast<unsigned char>(0xFF << unused);
    }
    auto* rsa = DecodeRsaPublicKey(key.data(), static_cast<long>(key.size()), __func__);
    if (!rsa) return nullptr;
    *input = publicKey.end;
    if (out) {
        RSA_free_nid_postfix(*out);
        *out = rsa;
    }
    return rsa;
}

}
