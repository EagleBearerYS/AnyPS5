#include "prx/libSceNpTrophy2/include/TitleTrophies.hpp"
#include "prx/libSceSystemService/SystemService.hpp"
#include "prx/libc/include/General.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace NpTrophy2 {

namespace {

bool tracing() {
    static const bool trace = std::getenv("APS5_TRACE_UDS") != nullptr;
    return trace;
}

std::string labelled(const char* format, std::uint32_t serviceLabel) {
    char path[64];
    std::snprintf(path, sizeof(path), format, static_cast<unsigned>(serviceLabel));
    return path;
}

std::optional<std::string> readTitleFile(const std::string& guestPath) {
    const auto path = ResolvePath_nid_no_patch(guestPath.c_str());
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) throw std::filesystem::filesystem_error("cannot check the file", path, error);
    if (!exists) return std::nullopt;
    const auto size = std::filesystem::file_size(path);
    if (size > MaxUcpSize) throw std::runtime_error("it has " + std::to_string(size) + " bytes, more than " + std::to_string(MaxUcpSize));
    std::string bytes(static_cast<std::size_t>(size), '\0');
    std::ifstream file(path, std::ios::binary);
    if (!file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) throw std::runtime_error("cannot read " + path.string());
    return bytes;
}

template <typename TParsed, typename TParse>
std::optional<TParsed> loadTitleFile(const std::string& guestPath, const char* without, TParse parse) {
    std::optional<std::string> bytes;
    try {
        bytes = readTitleFile(guestPath);
    } catch (const std::runtime_error& error) {
        std::fprintf(stderr, "[trophy] %s is not used, so %s: %s\n", guestPath.c_str(), without, error.what());
        return std::nullopt;
    }
    if (!bytes) {
        if (tracing()) std::fprintf(stderr, "[uds] %s does not exist, so %s\n", guestPath.c_str(), without);
        return std::nullopt;
    }
    try {
        return parse(*bytes);
    } catch (const std::runtime_error& error) {
        std::fprintf(stderr, "[trophy] %s is malformed, so %s: %s\n", guestPath.c_str(), without, error.what());
        return std::nullopt;
    }
}

std::string counted(std::size_t count, const char* one, const char* many) {
    return std::to_string(count) + " " + (count == 1 ? one : many);
}

}

std::string PackagePath(std::uint32_t serviceLabel) {
    return labelled("/app0/sce_sys/trophy2/trophy%02u.ucp", serviceLabel);
}

TitleTrophies LoadTitleTrophies(std::uint32_t serviceLabel, std::string_view language) {
    TitleTrophies title;
    const auto packagePath = PackagePath(serviceLabel);
    title.package = loadTitleFile<TrophyPackage>(packagePath, "trophy queries report placeholders and stats unlock no trophies",
                                                 [language](std::string_view bytes) { return ParseTrophyPackage(bytes, language); });
    if (title.package) {
        std::fprintf(stderr, "[trophy] %s: %s, %s, texts in %s\n", packagePath.c_str(), counted(title.package->trophies.size(), "trophy", "trophies").c_str(),
                     counted(title.package->groups.size(), "group", "groups").c_str(), title.package->language.c_str());
        for (const auto& skipped : title.package->skipped) std::fprintf(stderr, "[trophy] %s: %s\n", packagePath.c_str(), skipped.c_str());
    }
    return title;
}

const TitleTrophies& Title(std::uint32_t serviceLabel) {
    static std::mutex mutex;
    static std::map<std::uint32_t, std::unique_ptr<const TitleTrophies>> titles;
    std::lock_guard lock(mutex);
    auto& title = titles[serviceLabel];
    if (!title) title = std::make_unique<const TitleTrophies>(LoadTitleTrophies(serviceLabel, LanguageTag(SystemServiceConsoleLanguage("libSceNpTrophy2"))));
    return *title;
}

const TrophyPackage* Package(std::uint32_t serviceLabel) {
    const auto& title = Title(serviceLabel);
    return title.package ? &*title.package : nullptr;
}

void CopyText(char* destination, std::size_t size, std::string_view text) {
    if (size == 0) throw std::invalid_argument("text cannot be copied into an empty buffer");
    auto length = std::min(text.size(), size - 1);
    while (length > 0 && length < text.size() && (static_cast<unsigned char>(text[length]) & 0xC0u) == 0x80u) --length;
    std::memcpy(destination, text.data(), length);
    destination[length] = '\0';
}

UnlockedQuery UnlockedBy(std::int32_t userId, std::uint32_t serviceLabel) {
    return [userId, serviceLabel](std::int32_t trophyId) { return TitleStore().UnlockTick(userId, serviceLabel, trophyId).has_value(); };
}

std::optional<std::int32_t> PlatinumToUnlock(const TrophyPackage& package, std::int32_t trophyId, const UnlockedQuery& unlocked) {
    const auto trophy = package.trophies.find(trophyId);
    if (trophy == package.trophies.end() || !trophy->second.platinumId) return std::nullopt;
    const auto platinumId = *trophy->second.platinumId;
    const auto platinum = package.trophies.find(platinumId);
    if (platinum == package.trophies.end() || platinum->second.grade != GradePlatinum || unlocked(platinumId)) return std::nullopt;
    for (const auto& [id, other] : package.trophies) {
        if (id != platinumId && other.platinumId == platinumId && !unlocked(id)) return std::nullopt;
    }
    return platinumId;
}

std::optional<std::uint64_t> ProgressTarget(const TrophyDefinition& trophy) {
    if (!trophy.progressive) return std::nullopt;
    return trophy.target;
}

std::uint64_t TrophyProgress(const TrophyDefinition& trophy, std::optional<std::uint64_t> stat, bool unlocked) {
    const auto target = ProgressTarget(trophy);
    if (!target) throw std::invalid_argument("trophy " + std::to_string(trophy.id) + " is not progressive or has no target");
    if (unlocked) return *target;
    return std::min(stat.value_or(0), *target);
}

TrophyCounts CountTrophies(const TrophyPackage& package, std::optional<std::int32_t> group, const UnlockedQuery& unlocked) {
    TrophyCounts counts;
    for (const auto& [id, trophy] : package.trophies) {
        if (group && trophy.groupId != *group) continue;
        ++counts.trophies;
        ++counts.grades[static_cast<std::size_t>(trophy.grade)];
        if (!unlocked(id)) continue;
        ++counts.unlocked;
        ++counts.unlockedGrades[static_cast<std::size_t>(trophy.grade)];
    }
    return counts;
}

std::uint32_t TrophyCounts::Percentage() const {
    const auto points = [](const std::array<std::uint32_t, GradeBronze + 1>& counts) { return counts[GradeGold] * 6u + counts[GradeSilver] * 2u + counts[GradeBronze]; };
    const auto total = points(grades);
    return total == 0 ? 0 : points(unlockedGrades) * 100u / total;
}

}
