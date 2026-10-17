#include "SceTypes.hpp"
#include "prx/libSceUserService/UserService.hpp"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include <io.h>
#define APS5_DUP _dup
#define APS5_DUP2 _dup2
#define APS5_CLOSE _close
#define APS5_FILENO _fileno
#else
#include <unistd.h>
#define APS5_DUP dup
#define APS5_DUP2 dup2
#define APS5_CLOSE close
#define APS5_FILENO fileno
#endif

extern "C" int APS5_VABI sceUserServiceGetGamePresets(int user_id, UserServiceGamePresets* presets);

namespace {

void Require(bool value) { if (!value) std::abort(); }

constexpr std::size_t CallerSize = sizeof(UserServiceGamePresets);

void SetPriority(const char* value) {
#ifdef _WIN32
    Require(_putenv_s("ANYPS5_GAME_PRESETS_PRIORITY", value != nullptr ? value : "") == 0);
#else
    Require((value != nullptr ? ::setenv("ANYPS5_GAME_PRESETS_PRIORITY", value, 1) : ::unsetenv("ANYPS5_GAME_PRESETS_PRIORITY")) == 0);
#endif
}

UserServiceGamePresets Unanswered() {
    UserServiceGamePresets presets;
    std::memset(&presets, 0xa5, sizeof(presets));
    presets.this_size = CallerSize;
    return presets;
}

bool Answered(std::uint32_t priority) {
    auto presets = Unanswered();
    if (sceUserServiceGetGamePresets(USER_SERVICE_INITIAL_USER_ID, &presets) != USER_SERVICE_OK) return false;
    return presets.this_size == CallerSize && presets.priority == priority && presets.difficulty == 0 &&
        presets.invert_vertical_view_for_1st_person_view == 0 && presets.invert_horizontal_view_for_1st_person_view == 0 &&
        presets.invert_vertical_view_for_3rd_person_view == 0 && presets.invert_horizontal_view_for_3rd_person_view == 0 &&
        presets.display_sub_titles == 0 && presets.audio_language == 0;
}

bool Untouched(const UserServiceGamePresets& presets) {
    const auto expected = Unanswered();
    return std::memcmp(&presets, &expected, sizeof(presets)) == 0;
}

bool ArgumentsRejected() {
    auto presets = Unanswered();
    return sceUserServiceGetGamePresets(USER_SERVICE_INITIAL_USER_ID, nullptr) == USER_SERVICE_ERROR_INVALID_ARGUMENT &&
        sceUserServiceGetGamePresets(USER_SERVICE_INITIAL_USER_ID + 1, &presets) == USER_SERVICE_ERROR_INVALID_ARGUMENT &&
        sceUserServiceGetGamePresets(USER_SERVICE_USER_ID_INVALID, &presets) == USER_SERVICE_ERROR_INVALID_ARGUMENT && Untouched(presets);
}

bool Rejected(const char* value) {
    SetPriority(value);
    auto presets = Unanswered();
    try {
        static_cast<void>(sceUserServiceGetGamePresets(USER_SERVICE_INITIAL_USER_ID, &presets));
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        return Untouched(presets) && message.find("ANYPS5_GAME_PRESETS_PRIORITY") != std::string::npos && message.find("from 0 to 2") != std::string::npos &&
            message.find("\"" + std::string(value) + "\"") != std::string::npos;
    }
    return false;
}

std::string FirstCallsOutput() {
    const auto path = std::filesystem::temp_directory_path() / ("aps5_game_presets_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".txt");
    std::fflush(stderr);
    const int saved = APS5_DUP(APS5_FILENO(stderr));
    Require(saved >= 0);
    Require(std::freopen(path.string().c_str(), "wb", stderr) != nullptr);
    SetPriority(nullptr);
    const bool rejected = ArgumentsRejected();
    const bool first = Answered(0);
    SetPriority("2");
    const bool second = Answered(2);
    std::fflush(stderr);
    APS5_DUP2(saved, APS5_FILENO(stderr));
    APS5_CLOSE(saved);
    std::string captured;
    {
        std::ifstream file(path, std::ios::binary);
        captured.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    std::filesystem::remove(path);
    Require(rejected && first && second);
    return captured;
}

}

int main() {
    Require(FirstCallsOutput() == "[userservice] sceUserServiceGetGamePresets: this_size " + std::to_string(CallerSize) +
        ", difficulty 0, priority 0, invert_vertical_view_for_1st_person_view 0, invert_horizontal_view_for_1st_person_view 0, "
        "invert_vertical_view_for_3rd_person_view 0, invert_horizontal_view_for_3rd_person_view 0, display_sub_titles 0, audio_language 0\n");

    SetPriority(nullptr);
    Require(Answered(0));
    SetPriority("");
    Require(Answered(0));
    SetPriority("0");
    Require(Answered(0));
    SetPriority("1");
    Require(Answered(1));
    SetPriority("2");
    Require(Answered(2));
    for (const char* value : {"3", "-1", "1x", "x", "+1", " 1", "1 ", "4294967297"}) {
        Require(Rejected(value));
    }

    SetPriority("x");
    Require(ArgumentsRejected());
    SetPriority("1");
    Require(ArgumentsRejected());
    Require(Answered(1));
}
