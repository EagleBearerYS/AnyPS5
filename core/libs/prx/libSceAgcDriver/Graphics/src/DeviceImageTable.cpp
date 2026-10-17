#include "prx/libSceAgcDriver/Graphics/include/DeviceImageTable.hpp"
#include "DeviceImageTableAbi.hpp"
#include "RuntimeAbi.hpp"
#include <cstdint>
#include <cstdlib>
#include <string>

namespace AgcDriver::Graphics {

namespace {

namespace Abi = ShaderRecompiler::DeviceImageTableAbi;
namespace Runtime = ShaderRecompiler::RuntimeAbi;

constexpr std::uint32_t RequiredSets = Runtime::DeviceTableSet + 1u;
constexpr std::uint32_t HeapImageKinds = Runtime::FirstStorageImageBinding - Runtime::FirstImageBinding;

}

bool GpuPathEnabled() {
    static const bool enabled = std::getenv("APS5_GPU_PATH") != nullptr;
    return enabled;
}

std::optional<std::string> DeviceImagePathRefusal(const VkPhysicalDeviceLimits& limits, const DeviceImagePathFeatures& features) {
    if (limits.maxBoundDescriptorSets < RequiredSets) return "the device binds " + std::to_string(limits.maxBoundDescriptorSets) + " descriptor sets, and the image heap and the device image table need sets " + std::to_string(Runtime::DeviceHeapSet) + " and " + std::to_string(Runtime::DeviceTableSet);
    if (!features.heapFeatures) return "the device lacks descriptorBindingSampledImageUpdateAfterBind, descriptorBindingPartiallyBound or descriptorBindingUpdateUnusedWhilePending, which the image heap needs";
    if (!features.nonUniformIndexing) return "the device lacks shaderSampledImageArrayNonUniformIndexing, which sampling the image heap at a slot read from the table needs";
    const auto heapImages = static_cast<std::uint64_t>(HeapImageKinds) * Runtime::GpuImageHeapSlots;
    if (features.sampledImages < heapImages) return "the device binds " + std::to_string(features.sampledImages) + " update-after-bind sampled images per stage, and the image heap holds " + std::to_string(heapImages);
    if (features.samplers < Runtime::GpuSamplerHeapSlots) return "the device binds " + std::to_string(features.samplers) + " update-after-bind samplers per stage, and the image heap holds " + std::to_string(Runtime::GpuSamplerHeapSlots);
    const auto bucketBytes = (static_cast<std::uint64_t>(Abi::HeaderWords) + static_cast<std::uint64_t>(DeviceImageKeys::MaxBuckets) * Abi::BucketWords) * sizeof(std::uint32_t);
    if (limits.maxStorageBufferRange < bucketBytes) return "the device's storage buffer range of " + std::to_string(limits.maxStorageBufferRange) + " bytes is smaller than the device image table's " + std::to_string(bucketBytes);
    return std::nullopt;
}

}
