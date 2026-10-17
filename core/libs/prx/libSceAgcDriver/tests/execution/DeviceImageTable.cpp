#include "prx/libSceAgcDriver/Graphics/include/DeviceImageTable.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "RuntimeAbi.hpp"
#include "VulkanTestDevice.hpp"
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace AgcDriver::Graphics;
namespace Runtime = ShaderRecompiler::RuntimeAbi;

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

VkPhysicalDeviceLimits UsableLimits() {
    VkPhysicalDeviceLimits limits{};
    limits.maxBoundDescriptorSets = 8;
    limits.maxStorageBufferRange = 1u << 30u;
    return limits;
}

DeviceImagePathFeatures UsableFeatures() {
    return {true, true, 1u << 20u, 1u << 12u};
}

void DevicePathLimits() {
    Require(!DeviceImagePathRefusal(UsableLimits(), UsableFeatures()).has_value(), "a device with four sets, the heap's features and its descriptor counts serves the device path");
    auto oneSet = UsableLimits();
    oneSet.maxBoundDescriptorSets = 1;
    const auto refused = DeviceImagePathRefusal(oneSet, UsableFeatures());
    Require(refused.has_value() && refused->find("binds 1 descriptor sets") != std::string::npos, "a device that binds one descriptor set keeps the device path off");
    auto threeSets = UsableLimits();
    threeSets.maxBoundDescriptorSets = Runtime::DeviceTableSet;
    Require(DeviceImagePathRefusal(threeSets, UsableFeatures()).has_value(), "a device that binds three descriptor sets keeps the device path off");
    auto features = UsableFeatures();
    features.heapFeatures = false;
    Require(DeviceImagePathRefusal(UsableLimits(), features).has_value(), "a device without the heap's update-after-bind features keeps the device path off");
    features = UsableFeatures();
    features.nonUniformIndexing = false;
    Require(DeviceImagePathRefusal(UsableLimits(), features).has_value(), "a device without non-uniform sampled image indexing keeps the device path off");
    features = UsableFeatures();
    features.sampledImages = Runtime::GpuImageHeapSlots;
    Require(DeviceImagePathRefusal(UsableLimits(), features).has_value(), "a device that cannot bind the heap's sampled images keeps the device path off");
    features = UsableFeatures();
    features.samplers = Runtime::GpuSamplerHeapSlots - 1u;
    Require(DeviceImagePathRefusal(UsableLimits(), features).has_value(), "a device that cannot bind the heap's samplers keeps the device path off");
    auto smallRange = UsableLimits();
    smallRange.maxStorageBufferRange = 1u << 20u;
    Require(DeviceImagePathRefusal(smallRange, UsableFeatures()).has_value(), "a device whose storage buffer range cannot hold the bucket buffer keeps the device path off");
}

void DeviceTarget(AgcDriver::VulkanDevice& device) {
    const auto target = device.Target();
    Require(target.deviceImageTables == device.DeviceImagePath(), "the recompile target asks for device image tables exactly when the device serves the path");
    Require(device.ComputeTarget(64u).deviceImageTables == device.DeviceImagePath(), "the compute target asks for device image tables exactly when the device serves the path");
    const auto& limits = device.GraphicsContext().limits;
    if (limits.maxBoundDescriptorSets < Runtime::DeviceTableSet + 1u) Require(!device.DeviceImagePath(), "a device binding fewer than four sets keeps the device path off");
}

}

int main() {
    try {
#ifdef _WIN32
        _putenv_s("APS5_GPU_PATH", "1");
#else
        setenv("APS5_GPU_PATH", "1", 1);
#endif
        Require(GpuPathEnabled(), "APS5_GPU_PATH turns the device path on");
        DevicePathLimits();
        auto device = OpenVulkanTestDevice();
        if (device == nullptr) return VulkanTestSkipped;
        DeviceTarget(*device);
        std::printf("device path on %s: %s\n", device->DeviceName().c_str(), device->DeviceImagePath() ? "on" : "off");
        device->WaitIdle();
        device.reset();
        std::cout << "device image table tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "device image table tests failed: " << error.what() << '\n';
        return 1;
    }
}
