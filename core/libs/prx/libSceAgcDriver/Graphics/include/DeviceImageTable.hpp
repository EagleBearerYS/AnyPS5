#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_DEVICEIMAGETABLE_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_DEVICEIMAGETABLE_HPP

#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include "prx/libSceAgcDriver/Graphics/include/DeviceImageKeys.hpp"
#include "Recompiler.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace AgcDriver::Graphics {

class Buffer;
class Recorder;
class Texture;

class DeviceImageTable {
public:
    static constexpr std::uint32_t RingCapacity = 1024u;

    explicit DeviceImageTable(const Context& context);
    ~DeviceImageTable();
    DeviceImageTable(const DeviceImageTable&) = delete;
    DeviceImageTable& operator=(const DeviceImageTable&) = delete;

    std::uint64_t HeaderAddress() const;
    void BeforeDispatch(const Context& context, Recorder& recorder);

    struct Rejection {
        std::array<std::uint32_t, 8> words{};
        std::uint32_t reason = 0;
        std::uint32_t pc = 0;
        std::string message;
    };
    std::optional<Rejection> FindRejection(std::uint32_t index) const;

    struct Counts {
        std::uint64_t misses = 0;
        std::uint64_t distinct = 0;
        std::uint64_t dropped = 0;
        std::uint64_t inserted = 0;
        std::uint64_t rejected = 0;
        std::uint64_t refreshed = 0;
        std::uint64_t replaced = 0;
        std::uint64_t clears = 0;
        std::uint32_t entries = 0;
        std::uint32_t buckets = 0;
    };
    Counts Totals() const;

private:
    struct Entry {
        std::shared_ptr<Texture> texture;
        std::uint64_t address = 0;
        std::uint64_t guestBytes = 0;
        std::uint64_t generation = 0;
        bool snapshot = false;
    };
    struct ValidationState {
        std::uint64_t epoch = 0;
        std::uint64_t unwatch = 0;
        std::uint64_t depthSurfaces = 0;
        std::uint64_t pending = 0;
        bool operator==(const ValidationState&) const = default;
    };

    void resolve(const Context& build, Recorder& recorder);
    void refresh(const Context& build, Recorder& recorder);
    void record(Recorder& recorder);
    DeviceImageValue lookup(const Context& build, const DeviceImageKeys::Key& key, std::uint32_t pc, Entry& entry);
    std::uint32_t reject(const DeviceImageKeys::Key& key, std::uint32_t pc, std::uint32_t reason, const std::string& message);
    void insert(const DeviceImageKeys::Key& key, const DeviceImageValue& value, Recorder& recorder);
    void retire(const std::shared_ptr<Texture>& texture, Recorder& recorder);
    void drain(const std::shared_ptr<Buffer>& ring);
    void report();

    Context context;
    mutable std::recursive_mutex mutex;
    DeviceImageKeys keys;
    std::map<DeviceImageKeys::Key, Entry> entries;
    std::shared_ptr<Buffer> header;
    std::shared_ptr<Buffer> table;
    std::uint32_t tableBuckets = 0;
    std::shared_ptr<Buffer> ring;
    std::vector<std::shared_ptr<Buffer>> freeRings;
    std::uint64_t ringBatch = 0;
    std::vector<DeviceImageMiss> pending;
    std::vector<Rejection> rejections;
    std::map<std::tuple<DeviceImageKeys::Key, std::uint32_t, std::string>, std::uint32_t> rejectionIndex;
    std::optional<ValidationState> validated;
    Counts totals;
    Counts reported;
    std::uint64_t reportedPresents = 0;
    long long lastReport = 0;
};

bool UsesDeviceImageTable(const ShaderRecompiler::RecompileResult& program);

}

#endif
