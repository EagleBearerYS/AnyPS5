#ifndef CORE_LIBS_PRX_LIBSCENPTROPHY2_INCLUDE_TITLETROPHIES_HPP
#define CORE_LIBS_PRX_LIBSCENPTROPHY2_INCLUDE_TITLETROPHIES_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "prx/libSceNpTrophy2/include/TrophyPackage.hpp"
#include "prx/libSceNpTrophy2/include/TrophyStore.hpp"

namespace NpTrophy2 {

inline constexpr std::uint64_t MaxUcpSize = std::uint64_t{256} << 20u;

struct TitleTrophies {
    std::optional<TrophyPackage> package;
};

std::string PackagePath(std::uint32_t serviceLabel);
TitleTrophies LoadTitleTrophies(std::uint32_t serviceLabel, std::string_view language);
const TitleTrophies& Title(std::uint32_t serviceLabel);
const TrophyPackage* Package(std::uint32_t serviceLabel);
void CopyText(char* destination, std::size_t size, std::string_view text);

using UnlockedQuery = std::function<bool(std::int32_t)>;

UnlockedQuery UnlockedBy(std::int32_t userId, std::uint32_t serviceLabel);

std::optional<std::int32_t> PlatinumToUnlock(const TrophyPackage& package, std::int32_t trophyId, const UnlockedQuery& unlocked);
std::optional<std::uint64_t> ProgressTarget(const TrophyDefinition& trophy);
std::uint64_t TrophyProgress(const TrophyDefinition& trophy, std::optional<std::uint64_t> stat, bool unlocked);

struct TrophyCounts {
    std::uint32_t trophies = 0;
    std::uint32_t unlocked = 0;
    std::array<std::uint32_t, GradeBronze + 1> grades{};
    std::array<std::uint32_t, GradeBronze + 1> unlockedGrades{};

    std::uint32_t Percentage() const;
};

TrophyCounts CountTrophies(const TrophyPackage& package, std::optional<std::int32_t> group, const UnlockedQuery& unlocked);

}

#endif
