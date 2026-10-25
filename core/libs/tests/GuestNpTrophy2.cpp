#include "prx/libSceNpTrophy2/include/Api.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libSceNpTrophy2/include/NpTrophy2Constants.hpp"
#include "prx/libSceNpTrophy2/include/NpTrophy2Types.hpp"
#include <cstddef>
#include <cstdlib>
#include <stdexcept>
#include <string>

static void Require(bool value) { if (!value) std::abort(); }

static void APS5_VABI OnUnlock(int, int, void*) {}

template <typename Call> static bool ThrowsInvalidArgument(Call call) {
    try {
        call();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

static bool ThrowsNotImplemented(int (APS5_VABI* function)(void), const char* name) {
    try {
        function();
    } catch (const std::runtime_error& error) {
        return std::string(error.what()) == std::string(name) + " not implemented";
    }
    return false;
}

static void ResolvePendingExports() {
    struct Export {
        int (APS5_VABI* function)(void);
        const char* name;
    };
    const Export exports[] = {
        {sceNpTrophy2SystemAbortHandle, "sceNpTrophy2SystemAbortHandle"},
        {sceNpTrophy2SystemBuildGroupIconUri, "sceNpTrophy2SystemBuildGroupIconUri"},
        {sceNpTrophy2SystemBuildRewardIconUri, "sceNpTrophy2SystemBuildRewardIconUri"},
        {sceNpTrophy2SystemBuildTrophyIconUri, "sceNpTrophy2SystemBuildTrophyIconUri"},
        {sceNpTrophy2SystemCheckCallback, "sceNpTrophy2SystemCheckCallback"},
        {sceNpTrophy2SystemCheckNetSyncTitles, "sceNpTrophy2SystemCheckNetSyncTitles"},
        {sceNpTrophy2SystemCreateContext, "sceNpTrophy2SystemCreateContext"},
        {sceNpTrophy2SystemCreateHandle, "sceNpTrophy2SystemCreateHandle"},
        {sceNpTrophy2SystemDebugLockTrophy, "sceNpTrophy2SystemDebugLockTrophy"},
        {sceNpTrophy2SystemDebugUnlockTrophy, "sceNpTrophy2SystemDebugUnlockTrophy"},
        {sceNpTrophy2SystemDestroyContext, "sceNpTrophy2SystemDestroyContext"},
        {sceNpTrophy2SystemGetTrophyDetails, "sceNpTrophy2SystemGetTrophyDetails"},
        {sceNpTrophy2SystemGetTrophyGroupConf, "sceNpTrophy2SystemGetTrophyGroupConf"},
        {sceNpTrophy2SystemGetTrophySetArray, "sceNpTrophy2SystemGetTrophySetArray"},
        {sceNpTrophy2SystemGetTrophyTitleConf, "sceNpTrophy2SystemGetTrophyTitleConf"},
        {sceNpTrophy2SystemGetTrophyTitleDetails, "sceNpTrophy2SystemGetTrophyTitleDetails"},
        {sceNpTrophy2SystemGetTrpIconByUri, "sceNpTrophy2SystemGetTrpIconByUri"},
        {sceNpTrophy2SystemIsServerAvailable, "sceNpTrophy2SystemIsServerAvailable"},
        {sceNpTrophy2SystemRegisterTitleSyncedCallback, "sceNpTrophy2SystemRegisterTitleSyncedCallback"},
        {sceNpTrophy2SystemRegisterTitleUpdateCallback, "sceNpTrophy2SystemRegisterTitleUpdateCallback"},
        {sceNpTrophy2SystemRemoveAll, "sceNpTrophy2SystemRemoveAll"},
        {sceNpTrophy2SystemRemoveTitleData, "sceNpTrophy2SystemRemoveTitleData"},
        {sceNpTrophy2SystemRemoveUserData, "sceNpTrophy2SystemRemoveUserData"},
    };
    for (const auto& entry : exports) Require(ThrowsNotImplemented(entry.function, entry.name));
}

int main() {
    ResolvePendingExports();
    int userdata = 0;
    Require(sceNpTrophy2UnregisterUnlockCallback() == 0);
    Require(sceNpTrophy2RegisterUnlockCallback(reinterpret_cast<void*>(&OnUnlock), &userdata) == 0);
    Require(sceNpTrophy2UnregisterUnlockCallback() == 0);
    Require(sceNpTrophy2RegisterUnlockCallback(reinterpret_cast<void*>(&OnUnlock), &userdata) == 0);
    Require(sceNpTrophy2UnregisterUnlockCallback() == 0);

    Require(sceNpTrophy2ShowTrophyList(1) == 0);

    NpTrophy2GameDetails details{};
    NpTrophy2GameData data{};
    Require(sceNpTrophy2GetGameInfo(1, 1, &details, nullptr) == 0);
    Require(details.num_trophies != 0);
    Require(sceNpTrophy2GetGameInfo(1, 1, nullptr, &data) == 0);
    Require(ThrowsInvalidArgument([] { sceNpTrophy2GetGameInfo(1, 1, nullptr, nullptr); }));

    NpTrophy2GroupDetails groupDetails{};
    NpTrophy2GroupData groupData{};
    Require(sceNpTrophy2GetGroupInfo(1, 1, 2, &groupDetails, nullptr) == 0);
    Require(groupDetails.group_id == 2);
    Require(sceNpTrophy2GetGroupInfo(1, 1, 2, nullptr, &groupData) == 0);
    Require(groupData.group_id == 2);
    Require(ThrowsInvalidArgument([] { sceNpTrophy2GetGroupInfo(1, 1, 2, nullptr, nullptr); }));

    NpTrophy2Details trophyDetails{};
    NpTrophy2Data trophyData{};
    Require(sceNpTrophy2GetTrophyInfo(1, 1, 3, &trophyDetails, nullptr) == 0);
    Require(trophyDetails.trophy_id == 3);
    Require(sceNpTrophy2GetTrophyInfo(1, 1, 3, nullptr, &trophyData) == 0);
    Require(trophyData.trophy_id == 3);
    Require(ThrowsInvalidArgument([] { sceNpTrophy2GetTrophyInfo(1, 1, 3, nullptr, nullptr); }));
    std::size_t iconSize = 1234;
    Require(sceNpTrophy2GetRewardIcon(1, 1, 1, nullptr, &iconSize) == SCE_NP_TROPHY2_ERROR_ICON_FILE_NOT_FOUND);
    Require(iconSize == 0);
    Require(sceNpTrophy2GetRewardIcon(1, 1, 1, nullptr, nullptr) == SCE_NP_TROPHY2_ERROR_ICON_FILE_NOT_FOUND);
}
