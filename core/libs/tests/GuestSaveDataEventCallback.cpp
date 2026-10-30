#include "prx/libSceSaveData/include/Api.hpp"
#include "prx/libSceSaveData.native/include/Api.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include "SceTypes.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace {

constexpr int SaveDataErrorParameter = -2137063424;
constexpr int SaveDataErrorNotInitialized = -2137063423;
constexpr int SaveDataErrorBusy = -2137063421;
constexpr int SaveDataErrorNotFound = -2137063416;
constexpr std::uint32_t MountReadWrite = 2;
constexpr std::uint32_t MountCreate2 = 32;
constexpr std::int32_t User = 7;

int failures = 0;

void Check(bool condition, const std::string& what) {
    if (condition) return;
    std::fprintf(stderr, "savedata event callback check failed: %s\n", what.c_str());
    ++failures;
}

struct Call {
    int callback;
    SaveDataEvent event;
    void* userdata;
    std::thread::id thread;
    int terminated;
};

std::mutex callsMutex;
std::condition_variable callsChanged;
std::vector<Call> calls;

void Record(int callback, const SaveDataEvent* event, void* userdata, int terminated = 0) {
    std::lock_guard lock(callsMutex);
    calls.push_back({callback, *event, userdata, std::this_thread::get_id(), terminated});
    callsChanged.notify_all();
}

void APS5_VABI First(const SaveDataEvent* event, void* userdata) { Record(1, event, userdata); }
void APS5_VABI Second(const SaveDataEvent* event, void* userdata) { Record(2, event, userdata); }
void APS5_VABI Terminating(const SaveDataEvent* event, void* userdata) { Record(3, event, userdata, sceSaveDataTerminate()); }

std::size_t CallCount() {
    std::lock_guard lock(callsMutex);
    return calls.size();
}

bool WaitForCalls(std::size_t count) {
    std::unique_lock lock(callsMutex);
    return callsChanged.wait_for(lock, std::chrono::seconds(5), [&] { return calls.size() >= count; });
}

Call CallAt(std::size_t index) {
    std::lock_guard lock(callsMutex);
    return calls.at(index);
}

class Watchdog {
public:
    explicit Watchdog(const char* what) : what(what), thread([this] {
        std::unique_lock lock(mutex);
        if (!changed.wait_for(lock, std::chrono::seconds(10), [this] { return done; })) {
            std::fprintf(stderr, "savedata event callback check failed: %s did not return within 10 s\n", this->what);
            std::_Exit(1);
        }
    }) {}

    ~Watchdog() {
        {
            std::lock_guard lock(mutex);
            done = true;
        }
        changed.notify_all();
        thread.join();
    }

    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

private:
    const char* what;
    std::mutex mutex;
    std::condition_variable changed;
    bool done = false;
    std::thread thread;
};

void WriteText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    Check(static_cast<bool>(file), "writes " + path.string());
}

SceSaveDataDirName DirName(const char* name) {
    SceSaveDataDirName dirName{};
    std::strncpy(dirName.data, name, sizeof(dirName.data) - 1);
    return dirName;
}

SceSaveDataTitleId TitleId(const char* name) {
    SceSaveDataTitleId titleId{};
    std::strncpy(titleId.data, name, sizeof(titleId.data) - 1);
    return titleId;
}

int BackUp(const char* name) {
    const auto dirName = DirName(name);
    const auto titleId = TitleId("PPSA00000");
    SaveDataBackup backup{};
    backup.user_id = User;
    backup.title_id = &titleId;
    backup.dir_name = &dirName;
    return sceSaveDataBackup(&backup);
}

SaveDataEvent Poll(const std::string& what) {
    SaveDataEvent event{};
    Check(sceSaveDataGetEventResult(nullptr, &event) == 0, what + " is polled");
    return event;
}

bool SameEvent(const SaveDataEvent& left, const SaveDataEvent& right) {
    return left.type == right.type && left.error_code == right.error_code && left.user_id == right.user_id && std::strncmp(left.title_id.data, right.title_id.data, sizeof(left.title_id.data)) == 0 && std::strncmp(left.dir_name.data, right.dir_name.data, sizeof(left.dir_name.data)) == 0;
}

void CheckDelivered(std::size_t index, int callback, void* userdata, std::uint32_t type, const char* dir, const std::string& what) {
    const auto call = CallAt(index);
    Check(call.callback == callback, what + " reaches callback " + std::to_string(callback) + ", got " + std::to_string(call.callback));
    Check(call.userdata == userdata, what + " passes the registered userdata");
    Check(call.event.type == type, what + " has type " + std::to_string(type) + ", got " + std::to_string(call.event.type));
    Check(call.event.error_code == 0, what + " has error 0, got " + std::to_string(call.event.error_code));
    Check(call.event.user_id == User, what + " names the user");
    Check(std::strncmp(call.event.title_id.data, "PPSA00000", sizeof(call.event.title_id.data)) == 0, what + " names the title");
    Check(std::strncmp(call.event.dir_name.data, dir, sizeof(call.event.dir_name.data)) == 0, what + " names the save " + dir);
    Check(call.thread != std::this_thread::get_id(), what + " runs on another thread than the caller");
    const auto polled = Poll(what);
    Check(SameEvent(polled, call.event), what + " is the event the poll returns");
}

void CheckRegistration() {
    int tag = 0;
    Check(sceSaveDataRegisterEventCallback(First, &tag) == SaveDataErrorNotInitialized, "register before initialize is NOT_INITIALIZED");
    Check(sceSaveDataUnregisterEventCallback() == 0, "unregister before initialize succeeds");
    Check(sceSaveDataInitialize3(nullptr) == 0, "SaveData initializes");
    Check(sceSaveDataRegisterEventCallback(nullptr, &tag) == SaveDataErrorParameter, "a null callback is PARAMETER");
}

void CheckBackupDelivery(int* firstTag) {
    Check(sceSaveDataRegisterEventCallback(First, firstTag) == 0, "the first callback registers");
    Check(BackUp("backed") == 0, "the backup succeeds");
    Check(WaitForCalls(1), "the backup's completion reaches the callback within 5 s");
    Check(CallCount() == 1, "the backup is delivered once");
    if (CallCount() >= 1) CheckDelivered(0, 1, firstTag, 2, "backed", "the backup event");
    SaveDataEvent none{};
    Check(sceSaveDataGetEventResult(nullptr, &none) == SaveDataErrorNotFound, "the backup posts one event to poll");
}

void CheckUmountWithBackupDelivery(int* firstTag) {
    std::filesystem::create_directories("app0/sce_sys");
    WriteText("app0/sce_sys/param.json", "{\"titleId\":\"PPSA00000\",\"localizedParameters\":{\"en-US\":{\"titleName\":\"Example\"}}}");
    auto dirName = DirName("mounted");
    SaveDataMount3 mount{};
    mount.user_id = User;
    mount.dir_name = &dirName;
    mount.mount_mode = MountReadWrite | MountCreate2;
    SaveDataMountResult mounted{};
    Check(sceSaveDataMount3(&mount, &mounted) == 0, "the save mounts");
    const auto before = CallCount();
    Check(sceSaveDataUmountWithBackup(&mounted.mount_point) == 0, "umountWithBackup succeeds");
    Check(WaitForCalls(before + 1), "umountWithBackup's completion reaches the callback within 5 s");
    Check(CallCount() == before + 1, "umountWithBackup is delivered once");
    if (CallCount() >= before + 1) CheckDelivered(before, 1, firstTag, 1, "mounted", "the umountWithBackup event");
}

void CheckUnregister(int* firstTag, int* secondTag) {
    Check(sceSaveDataUnregisterEventCallback() == 0, "the first callback unregisters");
    const auto before = CallCount();
    Check(BackUp("unwatched") == 0, "a backup with no callback succeeds");
    const auto unwatched = Poll("the backup with no callback");
    Check(unwatched.type == 2 && std::strncmp(unwatched.dir_name.data, "unwatched", sizeof(unwatched.dir_name.data)) == 0, "the backup with no callback still posts its event");
    Check(sceSaveDataRegisterEventCallback(Second, secondTag) == 0, "the second callback registers");
    Check(BackUp("watched") == 0, "the second backup succeeds");
    Check(WaitForCalls(before + 1), "the second backup reaches the second callback within 5 s");
    Check(CallCount() == before + 1, "only the second backup is delivered, got " + std::to_string(CallCount() - before) + " calls");
    if (CallCount() >= before + 1) CheckDelivered(before, 2, secondTag, 2, "watched", "the event after the callback was unregistered");
    Check(sceSaveDataRegisterEventCallback(First, firstTag) == 0, "the first callback registers again");
    int replacedTag = 0;
    Check(sceSaveDataRegisterEventCallback(Second, &replacedTag) == 0, "the second callback replaces the first");
    Check(BackUp("replaced") == 0, "the backup after the replacement succeeds");
    Check(WaitForCalls(before + 2), "the backup after the replacement is delivered within 5 s");
    Check(CallCount() == before + 2, "the backup after the replacement is delivered once");
    if (CallCount() >= before + 2) CheckDelivered(before + 1, 2, &replacedTag, 2, "replaced", "the event after the replacement");
}

void CheckTerminateFromCallback() {
    const auto before = CallCount();
    int tag = 0;
    {
        Watchdog watchdog("a terminate from inside the callback");
        Check(sceSaveDataRegisterEventCallback(Terminating, &tag) == 0, "the terminating callback registers");
        Check(BackUp("terminating") == 0, "the backup for the terminating callback succeeds");
        Check(WaitForCalls(before + 1), "the terminating callback runs within 5 s");
    }
    if (CallCount() >= before + 1) Check(CallAt(before).terminated == SaveDataErrorBusy, "a terminate from inside the callback is BUSY, got " + std::to_string(CallAt(before).terminated));
    Poll("the terminating callback's backup");
    Check(sceSaveDataUnregisterEventCallback() == 0, "the terminating callback unregisters");
}

void CheckIdleTerminateAndRestart(int* firstTag) {
    {
        Watchdog watchdog("an idle terminate");
        Check(sceSaveDataTerminate() == 0, "SaveData terminates with the delivery thread idle");
    }
    Check(sceSaveDataRegisterEventCallback(First, firstTag) == SaveDataErrorNotInitialized, "register after terminate is NOT_INITIALIZED");
    Check(sceSaveDataInitialize3(nullptr) == 0, "SaveData initializes again");
    const auto before = CallCount();
    Check(BackUp("restarted") == 0, "a backup after the restart succeeds");
    Poll("the backup after the restart");
    Check(sceSaveDataRegisterEventCallback(First, firstTag) == 0, "the first callback registers after the restart");
    Check(BackUp("redelivered") == 0, "a backup with a callback after the restart succeeds");
    Check(WaitForCalls(before + 1), "the backup after the restart is delivered within 5 s");
    Check(CallCount() == before + 1, "terminate dropped the callback: the restart delivers only after registering again");
    if (CallCount() >= before + 1) CheckDelivered(before, 1, firstTag, 2, "redelivered", "the event after the restart");
    {
        Watchdog watchdog("the final terminate");
        Check(sceSaveDataTerminate() == 0, "SaveData terminates again");
    }
}

}

int main() {
    const auto root = std::filesystem::temp_directory_path() / ("anyps5-savedata-event-callback-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(root);
    const auto previous = std::filesystem::current_path();
    std::filesystem::current_path(root);
    int firstTag = 0;
    int secondTag = 0;
    CheckRegistration();
    CheckBackupDelivery(&firstTag);
    CheckUmountWithBackupDelivery(&firstTag);
    CheckUnregister(&firstTag, &secondTag);
    CheckTerminateFromCallback();
    CheckIdleTerminateAndRestart(&firstTag);
    std::filesystem::current_path(previous);
    std::error_code error;
    std::filesystem::remove_all(root, error);
    if (failures != 0) return 1;
    std::printf("savedata event callback tests passed\n");
    return 0;
}
