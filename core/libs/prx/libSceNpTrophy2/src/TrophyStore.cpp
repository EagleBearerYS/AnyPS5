#include "prx/libSceNpTrophy2/include/TrophyStore.hpp"
#include "prx/libSceNpTrophy2/include/TitleTrophies.hpp"
#include "prx/libc/include/General.hpp"
#include "prx/libkernel/AppMetadata/include/AppMetadata.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

namespace NpTrophy2 {

namespace {

template <typename TValue>
bool parseNumber(std::string_view text, TValue& value) {
    const auto* end = text.data() + text.size();
    const auto [stop, error] = std::from_chars(text.data(), end, value);
    return error == std::errc{} && stop == end;
}

struct Listener {
    std::uint64_t id = 0;
    NpTrophy2UnlockListener callback = nullptr;
    void* context = nullptr;
};

struct Listeners {
    std::mutex mutex;
    std::vector<Listener> entries;
    std::uint64_t next = 1;
};

Listeners& listeners() {
    static Listeners registry;
    return registry;
}

}

std::uint64_t CurrentTick() {
    const auto now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return UnixEpochTick + static_cast<std::uint64_t>(now);
}

TrophyStore::TrophyStore(std::filesystem::path directory) : directory(std::move(directory)) {}

std::filesystem::path TrophyStore::RecordPath(std::int32_t userId, std::uint32_t serviceLabel) const {
    return directory / (std::to_string(userId) + "_" + std::to_string(serviceLabel) + ".txt");
}

TrophyStore::Unlocks& TrophyStore::record(const Key& key) {
    if (const auto found = records.find(key); found != records.end()) return found->second;
    Unlocks unlocks;
    const auto path = RecordPath(key.first, key.second);
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) throw std::filesystem::filesystem_error("cannot check the trophy record", path, error);
    if (exists) {
        std::ifstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("cannot read the trophy record " + path.string());
        std::string line;
        std::size_t number = 0;
        while (std::getline(file, line)) {
            ++number;
            const auto space = line.find(' ');
            std::int32_t trophyId = 0;
            std::uint64_t tick = 0;
            const std::string_view text(line);
            if (space == std::string::npos || !parseNumber(text.substr(0, space), trophyId) || trophyId < 0 || !parseNumber(text.substr(space + 1), tick) || !unlocks.emplace(trophyId, tick).second) {
                throw std::runtime_error("the trophy record " + path.string() + " has a line " + std::to_string(number) + " that is not a new '<trophy id> <unlock tick>' pair");
            }
        }
        if (file.bad()) throw std::runtime_error("cannot read the trophy record " + path.string());
    }
    return records.emplace(key, std::move(unlocks)).first->second;
}

void TrophyStore::save(const Key& key, const Unlocks& unlocks) const {
    std::error_code error;
    if (std::filesystem::create_directories(directory, error)) RecordWrittenPath_nid_no_patch(directory);
    if (error) throw std::filesystem::filesystem_error("cannot create the trophy directory", directory, error);
    const auto path = RecordPath(key.first, key.second);
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        for (const auto& [trophyId, tick] : unlocks) file << trophyId << ' ' << tick << '\n';
        file.flush();
        if (!file) throw std::runtime_error("cannot write the trophy record " + temporary.string());
    }
    std::filesystem::rename(temporary, path);
    RecordWrittenPath_nid_no_patch(path);
}

bool TrophyStore::Unlock(std::int32_t userId, std::uint32_t serviceLabel, std::int32_t trophyId, std::uint64_t tick) {
    if (trophyId < 0) throw std::invalid_argument("trophy " + std::to_string(trophyId) + " cannot be unlocked: trophy ids are not negative");
    std::lock_guard lock(mutex);
    const Key key{userId, serviceLabel};
    auto& unlocks = record(key);
    if (unlocks.contains(trophyId)) return false;
    auto updated = unlocks;
    updated.emplace(trophyId, tick);
    save(key, updated);
    unlocks = std::move(updated);
    return true;
}

std::optional<std::uint64_t> TrophyStore::UnlockTick(std::int32_t userId, std::uint32_t serviceLabel, std::int32_t trophyId) {
    std::lock_guard lock(mutex);
    const auto& unlocks = record({userId, serviceLabel});
    const auto found = unlocks.find(trophyId);
    if (found == unlocks.end()) return std::nullopt;
    return found->second;
}

std::size_t TrophyStore::UnlockCount(std::int32_t userId, std::uint32_t serviceLabel) {
    std::lock_guard lock(mutex);
    return record({userId, serviceLabel}).size();
}

TrophyStore& TitleStore() {
    static TrophyStore store(ResolvePath_nid_no_patch(TrophyRootGuest));
    return store;
}

std::optional<std::uint64_t> ContextUnlockTick(int context, std::int32_t trophyId) {
    const auto owner = FindContext(context);
    if (!owner || trophyId < 0) return std::nullopt;
    return TitleStore().UnlockTick(owner->userId, owner->serviceLabel, trophyId);
}

std::size_t ContextUnlockCount(int context) {
    const auto owner = FindContext(context);
    return owner ? TitleStore().UnlockCount(owner->userId, owner->serviceLabel) : 0;
}

}

extern "C" bool NpTrophy2RecordUnlock_nid_no_patch(std::int32_t userId, std::uint32_t serviceLabel, std::int32_t trophyId) {
    const auto title = GetAppTitleId_nid_postfix();
    const std::string_view titleId(title.value, static_cast<std::size_t>(std::find(std::begin(title.value), std::end(title.value), '\0') - std::begin(title.value)));
    const NpTrophy2Unlock unlock{std::string(titleId), userId, serviceLabel, trophyId, NpTrophy2::CurrentTick()};
    if (!NpTrophy2::TitleStore().Unlock(userId, serviceLabel, trophyId, unlock.tick)) return false;
    std::fprintf(stderr, "[trophy] %s unlocked trophy %d for user %d\n", unlock.titleId.c_str(), trophyId, userId);
    std::vector<NpTrophy2::Listener> current;
    {
        auto& registry = NpTrophy2::listeners();
        std::lock_guard lock(registry.mutex);
        current = registry.entries;
    }
    for (const auto& entry : current) entry.callback(entry.context, unlock);
    const auto* package = NpTrophy2::Package(serviceLabel);
    if (package == nullptr) return true;
    if (const auto platinum = NpTrophy2::PlatinumToUnlock(*package, trophyId, NpTrophy2::UnlockedBy(userId, serviceLabel))) NpTrophy2RecordUnlock_nid_no_patch(userId, serviceLabel, *platinum);
    return true;
}

extern "C" std::uint64_t NpTrophy2AddUnlockListener_nid_no_patch(NpTrophy2UnlockListener listener, void* context) {
    if (listener == nullptr) {
        APS5_INVALID_ARG_EX;
    }
    auto& registry = NpTrophy2::listeners();
    std::lock_guard lock(registry.mutex);
    const auto id = registry.next++;
    registry.entries.push_back({id, listener, context});
    return id;
}

extern "C" void NpTrophy2RemoveUnlockListener_nid_no_patch(std::uint64_t id) {
    auto& registry = NpTrophy2::listeners();
    std::lock_guard lock(registry.mutex);
    const auto removed = std::erase_if(registry.entries, [id](const NpTrophy2::Listener& entry) { return entry.id == id; });
    if (removed == 0) throw std::invalid_argument("no trophy unlock listener has id " + std::to_string(id));
}
