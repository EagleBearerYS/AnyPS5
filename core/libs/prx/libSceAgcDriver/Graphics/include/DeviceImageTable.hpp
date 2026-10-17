#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_DEVICEIMAGETABLE_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_DEVICEIMAGETABLE_HPP

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include "prx/libSceAgcDriver/Graphics/include/DeviceImageKeys.hpp"
#include <cstdint>
#include <optional>
#include <string>

namespace AgcDriver::Graphics {

bool GpuPathEnabled();

struct DeviceImagePathFeatures {
    bool heapFeatures = false;
    bool nonUniformIndexing = false;
    std::uint32_t sampledImages = 0;
    std::uint32_t samplers = 0;
};

std::optional<std::string> DeviceImagePathRefusal(const VkPhysicalDeviceLimits& limits, const DeviceImagePathFeatures& features);

}

#endif
