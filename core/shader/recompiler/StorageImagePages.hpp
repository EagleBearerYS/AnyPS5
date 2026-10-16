#ifndef CORE_SHADER_RECOMPILER_STORAGEIMAGEPAGES_HPP
#define CORE_SHADER_RECOMPILER_STORAGEIMAGEPAGES_HPP

#include <algorithm>
#include <array>
#include <cstdint>

namespace ShaderRecompiler::StorageImagePages {

enum class Placement : std::uint32_t { Recorded, Dropped, Unplaced };

struct Store {
    Placement placement = Placement::Unplaced;
    std::uint64_t address = 0;
};

inline constexpr std::uint32_t FormatTableEntries = 137;
inline constexpr std::uint32_t FormatTableWords = (FormatTableEntries + 7u) / 8u;

constexpr std::uint32_t ElementSizeCode(std::uint32_t format) {
    switch (format) {
        case 1: case 2: case 5: case 6: case 128:
            return 1u;
        case 7: case 8: case 11: case 12: case 13: case 14: case 15: case 18: case 19: case 129: case 133: case 134: case 136:
            return 2u;
        case 20: case 21: case 22: case 23: case 24: case 27: case 28: case 29: case 30: case 34: case 36: case 43: case 50: case 54: case 56: case 57: case 60: case 61: case 130: case 132:
            return 3u;
        case 62: case 63: case 64: case 65: case 66: case 69: case 70: case 71:
            return 4u;
        case 75: case 76: case 77:
            return 5u;
        default:
            return 0u;
    }
}

constexpr std::array<std::uint32_t, FormatTableWords> FormatTable() {
    std::array<std::uint32_t, FormatTableWords> table{};
    for (std::uint32_t format = 0; format < FormatTableEntries; ++format) table[format / 8u] |= ElementSizeCode(format) << (format % 8u * 4u);
    return table;
}

enum class TileClass : std::uint32_t { Unsupported, Linear, Block256B, Block4KB, Block64KB };

constexpr TileClass ClassOf(std::uint32_t tileMode) {
    switch (tileMode) {
        case 0: return TileClass::Linear;
        case 1: case 2: return TileClass::Block256B;
        case 5: case 6: case 21: case 22: return TileClass::Block4KB;
        case 9: case 10: case 17: case 18: case 24: case 25: case 26: case 27: return TileClass::Block64KB;
        default: return TileClass::Unsupported;
    }
}

constexpr bool XorMode(std::uint32_t tileMode) {
    return tileMode == 17u || tileMode == 18u || tileMode == 21u || tileMode == 22u || tileMode >= 24u && tileMode <= 27u;
}

constexpr std::uint32_t BlockLog2(TileClass tileClass) {
    return tileClass == TileClass::Block256B ? 8u : tileClass == TileClass::Block4KB ? 12u : 16u;
}

inline constexpr std::array<std::array<std::uint32_t, 5>, 2> PageBitInputs{{
    {(22u << 0) | (6u << 5) | (23u << 10) | (7u << 15), (21u << 0) | (6u << 5) | (22u << 10) | (7u << 15), (21u << 0) | (5u << 5) | (22u << 10) | (6u << 15), (20u << 0) | (5u << 5) | (21u << 10) | (6u << 15), (20u << 0) | (4u << 5) | (21u << 10) | (5u << 15)},
    {(22u << 0) | (6u << 5) | (23u << 10) | (7u << 15), (20u << 0) | (6u << 5) | (22u << 10) | (7u << 15), (19u << 0) | (4u << 5) | (22u << 10) | (6u << 15), (18u << 0) | (3u << 5) | (20u << 10) | (6u << 15), (18u << 0) | (2u << 5) | (19u << 10) | (4u << 15)},
}};

constexpr std::uint32_t PageBitGroup(std::uint32_t tileMode) {
    return tileMode == 24u || tileMode == 27u ? 1u : 0u;
}

constexpr std::uint32_t PageBits(std::uint32_t tileMode, std::uint32_t elementLog2, std::uint32_t x, std::uint32_t y) {
    const auto inputs = PageBitInputs[PageBitGroup(tileMode)][elementLog2];
    std::uint32_t bits = 0;
    for (std::uint32_t bit = 0; bit < 4u; ++bit) {
        const auto input = (inputs >> (bit * 5u)) & 0x1fu;
        const auto coordinate = (input & 0x10u) != 0u ? y : x;
        bits |= ((coordinate >> (input & 0xfu)) & 1u) << bit;
    }
    return bits;
}

inline constexpr std::array<std::array<std::uint32_t, 12>, 5> TailLocations64KB{{
    {(128u << 16) | 0u, (0u << 16) | 128u, (64u << 16) | 0u, (0u << 16) | 64u, (32u << 16) | 0u, (16u << 16) | 32u, (0u << 16) | 48u, (0u << 16) | 32u, (16u << 16) | 16u, (16u << 16) | 0u, (0u << 16) | 16u, 0u},
    {(128u << 16) | 0u, (0u << 16) | 64u, (64u << 16) | 0u, (0u << 16) | 32u, (32u << 16) | 0u, (16u << 16) | 16u, (0u << 16) | 24u, (0u << 16) | 16u, (16u << 16) | 8u, (16u << 16) | 0u, (0u << 16) | 8u, 0u},
    {(64u << 16) | 0u, (0u << 16) | 64u, (32u << 16) | 0u, (0u << 16) | 32u, (16u << 16) | 0u, (8u << 16) | 16u, (0u << 16) | 24u, (0u << 16) | 16u, (8u << 16) | 8u, (8u << 16) | 0u, (0u << 16) | 8u, 0u},
    {(64u << 16) | 0u, (0u << 16) | 32u, (32u << 16) | 0u, (0u << 16) | 16u, (16u << 16) | 0u, (8u << 16) | 8u, (0u << 16) | 12u, (0u << 16) | 8u, (8u << 16) | 4u, (8u << 16) | 0u, (0u << 16) | 4u, 0u},
    {(32u << 16) | 0u, (0u << 16) | 32u, (16u << 16) | 0u, (0u << 16) | 16u, (8u << 16) | 0u, (4u << 16) | 8u, (0u << 16) | 12u, (0u << 16) | 8u, (4u << 16) | 4u, (4u << 16) | 0u, (0u << 16) | 4u, 0u},
}};

constexpr std::uint32_t ShiftCeil(std::uint32_t value, std::uint32_t shift) {
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(value) + (std::uint64_t{1} << shift) - 1u) >> shift);
}

constexpr std::uint32_t AlignUp(std::uint32_t value, std::uint32_t alignment) {
    return (value + alignment - 1u) / alignment * alignment;
}

constexpr Store Locate(const std::array<std::uint32_t, 8>& words, std::uint32_t x, std::uint32_t y, std::uint32_t layer, std::uint32_t lod) {
    auto base = (static_cast<std::uint64_t>(words[0]) | static_cast<std::uint64_t>(words[1] & 0xffu) << 32u) << 8u;
    const auto format = (words[1] >> 20u) & 0x1ffu;
    const auto width = (((words[1] >> 30u) & 0x3u) | ((words[2] & 0xfffu) << 2u)) + 1u;
    const auto height = ((words[2] >> 14u) & 0x3fffu) + 1u;
    const auto baseLevel = (words[3] >> 12u) & 0xfu;
    const auto lastLevel = (words[3] >> 16u) & 0xfu;
    const auto tileMode = (words[3] >> 20u) & 0x1fu;
    const auto type = words[3] >> 28u;
    const auto depth = words[4] & 0x1fffu;
    const auto baseArray = (words[4] >> 16u) & 0x1fffu;
    const auto arrayPitch = words[5] & 0xfu;
    const auto maxMip = (words[5] >> 4u) & 0xfu;
    const auto sizeCode = format < FormatTableEntries ? ElementSizeCode(format) : 0u;
    const auto tileClass = ClassOf(tileMode);
    const bool arrayed = type == 11u || type == 12u || type == 13u;
    bool typed = type == 8u ? height == 1u && depth == 0u && baseArray == 0u : type == 9u ? depth == 0u && baseArray == 0u : type == 11u ? width == height : type == 12u ? height == 1u && (tileMode == 0u || tileMode == 24u || tileMode == 27u) : type == 13u;
    if (sizeCode == 0u || tileClass == TileClass::Unsupported || !typed || arrayPitch != 0u || baseLevel > maxMip || baseLevel > lastLevel || base == 0u) return {};
    std::uint64_t xorBits = 0;
    if (XorMode(tileMode)) {
        xorBits = base & (tileMode == 21u || tileMode == 22u ? 0xfffu : 0xffffu);
        base -= xorBits;
        if (base == 0u) return {};
    }
    const auto blockLog2 = tileClass == TileClass::Linear ? 0u : BlockLog2(tileClass);
    if (tileClass != TileClass::Linear && (base & ((std::uint64_t{1} << std::min(blockLog2, 16u)) - 1u)) != 0u) return {};
    const auto level = baseLevel + lod;
    if (lod > 15u || level > std::min(lastLevel, maxMip)) return {};
    const auto lastLayer = type == 11u && baseArray == depth ? depth + 5u : depth;
    const auto absoluteLayer = arrayed ? baseArray + layer : 0u;
    if (arrayed && (layer > lastLayer || absoluteLayer > lastLayer)) return {Placement::Dropped, 0u};
    if (x >= std::max(width >> level, 1u) || y >= std::max(height >> level, 1u)) return {Placement::Dropped, 0u};
    const auto elementLog2 = sizeCode - 1u;
    const auto mipCount = maxMip + 1u;
    if (tileClass == TileClass::Linear) {
        const auto rowElements = 256u >> elementLog2;
        std::uint64_t total = 0;
        std::uint64_t levelOffset = 0;
        std::uint64_t pitch = 0;
        for (auto current = mipCount; current-- > 0u;) {
            const auto padded = AlignUp(std::max(ShiftCeil(width, current), 1u), rowElements);
            const auto rows = std::max(ShiftCeil(height, current), 1u);
            if (current == level) {
                levelOffset = total;
                pitch = static_cast<std::uint64_t>(padded) << elementLog2;
            }
            total += static_cast<std::uint64_t>(padded) * rows << elementLog2;
        }
        return {Placement::Recorded, base + absoluteLayer * total + levelOffset + y * pitch + (static_cast<std::uint64_t>(x) << elementLog2)};
    }
    const auto widthLog2 = (blockLog2 - elementLog2 + 1u) / 2u;
    const auto heightLog2 = (blockLog2 - elementLog2) / 2u;
    auto widthLimit = (1u << widthLog2) >> 1u;
    auto heightLimit = 1u << heightLog2;
    if (tileMode == 24u && elementLog2 <= 1u) {
        widthLimit >>= 1u;
        if (elementLog2 == 0u) heightLimit >>= 1u;
    }
    const auto maxTailLevels = blockLog2 == 12u ? 8u : 12u;
    auto firstTail = mipCount;
    if (blockLog2 != 8u && mipCount > 1u) {
        for (std::uint32_t current = 0; current < mipCount; ++current) {
            if (ShiftCeil(width, current) <= widthLimit && ShiftCeil(height, current) <= heightLimit && mipCount - current <= maxTailLevels) {
                firstTail = current;
                break;
            }
        }
    }
    std::uint64_t offset = firstTail < mipCount ? std::uint64_t{1} << blockLog2 : 0u;
    std::uint64_t levelOffset = 0;
    std::uint32_t blocksPerRow = 0;
    for (auto current = firstTail; current-- > 0u;) {
        const auto paddedWidth = AlignUp(std::max(ShiftCeil(width, current), 1u), 1u << widthLog2);
        const auto paddedHeight = AlignUp(std::max(ShiftCeil(height, current), 1u), 1u << heightLog2);
        if (current == level) {
            levelOffset = offset;
            blocksPerRow = paddedWidth >> widthLog2;
        }
        offset += static_cast<std::uint64_t>(paddedWidth) * paddedHeight << elementLog2;
    }
    auto address = base + absoluteLayer * offset;
    auto swizzleX = x;
    auto swizzleY = y;
    if (level < firstTail) {
        address += levelOffset + ((static_cast<std::uint64_t>(y >> heightLog2) * blocksPerRow + (x >> widthLog2)) << blockLog2);
    } else if (blockLog2 == 16u) {
        const auto location = TailLocations64KB[elementLog2][level - firstTail];
        swizzleX += location >> 16u;
        swizzleY += location & 0xffffu;
    }
    if (blockLog2 == 16u) address += static_cast<std::uint64_t>((PageBits(tileMode, elementLog2, swizzleX, swizzleY) ^ static_cast<std::uint32_t>(xorBits >> 12u)) & 0xfu) << 12u;
    return {Placement::Recorded, address};
}

}

#endif
