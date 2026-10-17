#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_DEVICEIMAGETABLE_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_DEVICEIMAGETABLE_HPP

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include "prx/libSceAgcDriver/Graphics/include/DeviceImageKeys.hpp"
#include "Recompiler.hpp"
#include "IntermediateRepresentation/IrMetadata/Resources.hpp"
#include "ImageTableAbi.hpp"
#include "RuntimeAbi.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace AgcDriver::Graphics {

class Buffer;
class Recorder;
class Texture;

bool GpuPathEnabled();

struct DeviceImagePathFeatures {
    bool heapFeatures = false;
    bool nonUniformIndexing = false;
    std::uint32_t sampledImages = 0;
    std::uint32_t samplers = 0;
};

std::optional<std::string> DeviceImagePathRefusal(const VkPhysicalDeviceLimits& limits, const DeviceImagePathFeatures& features);

struct DeviceTableView {
    ShaderRecompiler::DescriptorImageShape shape = ShaderRecompiler::DescriptorImageShape::Image2D;
    bool depthCompare = false;
    bool identitySwizzle = false;
};

struct DeviceTableTexture {
    std::shared_ptr<Texture> texture;
    VkImageView view = VK_NULL_HANDLE;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    std::uint64_t address = 0;
    std::uint64_t guestBytes = 0;
    std::string unservable;
};

DeviceTableTexture LookupDeviceTableTexture(const Context& context, std::span<const std::uint32_t, 8> descriptor, const DeviceTableView& view, std::uint32_t samplerClass);

class DeviceImageTable {
public:
    using Key = DeviceImageKeys::Key;
    struct Interface {
        std::uint64_t id = 0;
        ShaderRecompiler::ImageResource image;
        std::vector<ShaderRecompiler::ImageResource> modes;
    };
    using InterfaceLookup = std::function<std::shared_ptr<const Interface>(std::uint64_t id)>;

    static constexpr std::uint32_t RingCapacity = 4096u;
    static constexpr std::uint32_t RingChunks = 16u;
    static constexpr std::uint32_t RingWords = ShaderRecompiler::DeviceImageTableAbi::RingRecordsWord + RingCapacity * ShaderRecompiler::DeviceImageTableAbi::RecordWords;
    static constexpr std::uint32_t TableWords = ShaderRecompiler::DeviceImageTableAbi::HeaderWords + DeviceImageKeys::MaxBuckets * ShaderRecompiler::DeviceImageTableAbi::BucketWords;

    DeviceImageTable(const Context& context, InterfaceLookup lookup);
    ~DeviceImageTable();
    DeviceImageTable(const DeviceImageTable&) = delete;
    DeviceImageTable& operator=(const DeviceImageTable&) = delete;

    VkDescriptorSetLayout Layout() const { return layout; }
    void Prepare(const Context& build, Recorder& recorder);
    void Bind(VkCommandBuffer commands, VkPipelineBindPoint bindPoint, VkPipelineLayout pipelineLayout, Recorder& recorder);
    std::span<std::uint32_t> RingChunk(Recorder& recorder);
    VkBuffer TableBuffer() const;
    std::optional<DeviceImageValue> Find(const Key& key) const;
    std::vector<std::uint32_t> KeyWords() const;
    std::string LastFault() const;

    struct Counts {
        std::uint64_t drained = 0;
        std::uint64_t distinct = 0;
        std::uint64_t dropped = 0;
        std::uint64_t inserted = 0;
        std::uint64_t faults = 0;
        std::uint64_t rejected = 0;
        std::uint64_t uploads = 0;
        std::uint64_t uploadBytes = 0;
        std::uint64_t chunkWaits = 0;
        std::uint64_t unserved = 0;
        std::uint64_t heapFull = 0;
        std::uint64_t tableFull = 0;
        std::uint32_t entries = 0;
        std::uint32_t buckets = 0;
        std::array<std::uint32_t, ShaderRecompiler::RuntimeAbi::FirstStorageImageBinding> peakTextures{};
    };
    Counts Totals() const;

private:
    struct Entry {
        std::shared_ptr<Texture> texture;
        std::uint32_t kind = 0;
        std::uint64_t address = 0;
        std::uint64_t guestBytes = 0;
    };
    struct Rejection {
        ShaderRecompiler::ImageTableAbi::PoisonReason reason = ShaderRecompiler::ImageTableAbi::PoisonReason::DriverRejected;
        std::string message;
    };
    struct Chunk {
        std::uint64_t serial = 0;
        bool busy = false;
    };

    void resolve(const Context& build, Recorder& recorder);
    std::optional<DeviceImageValue> lookupKey(const Context& build, const Key& key, Entry& entry);
    DeviceImageValue reject(const Key& key, ShaderRecompiler::ImageTableAbi::PoisonReason reason, const std::string& message);
    void unserved(const Key& key, const std::string& why);
    void heapFull(const Key& key, std::uint32_t kind);
    bool insert(const Key& key, const DeviceImageValue& value);
    void hold(const Key& key, Entry entry);
    void upload(Recorder& recorder);
    std::uint32_t chunkFor(Recorder& recorder, bool mayWait);
    void drain(std::uint32_t chunk);
    std::string faultText(const Key& key, std::uint32_t pc) const;
    void report();

    Context context;
    InterfaceLookup lookup;
    mutable std::recursive_mutex mutex;
    DeviceImageKeys keys;
    std::map<Key, Entry> entries;
    std::map<Key, Rejection> rejections;
    std::vector<DeviceImageMiss> pending;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    std::unique_ptr<Buffer> table;
    std::unique_ptr<Buffer> ring;
    VkDeviceSize chunkStride = 0;
    std::vector<Chunk> chunks;
    std::uint32_t uploadedBuckets = 0;
    std::array<std::uint32_t, ShaderRecompiler::RuntimeAbi::FirstStorageImageBinding> liveTextures{};
    std::map<std::pair<std::uint32_t, const void*>, std::uint32_t> textureUses;
    std::set<Key> unservedKeys;
    std::set<std::uint32_t> fullKinds;
    bool tableFullReported = false;
    std::string lastFault;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    Counts totals;
    Counts reported;
    long long lastReport = 0;
};

}

#endif
