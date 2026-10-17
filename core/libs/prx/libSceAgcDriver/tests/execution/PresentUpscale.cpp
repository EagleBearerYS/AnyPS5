#include "prx/libSceAgcDriver/Execution/include/Presentation.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include <SDL_loadso.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using AgcDriver::VulkanDevice;
constexpr int VulkanTestSkipped = 77;

struct Headless {
    std::uint32_t width;
    std::uint32_t height;
    PFN_vkGetInstanceProcAddr resolve;
};

VkSurfaceKHR createSurface(void* context, VkInstance instance) {
    auto* headless = static_cast<Headless*>(context);
    const auto create = reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(headless->resolve(instance, "vkCreateHeadlessSurfaceEXT"));
    Require(create != nullptr, "vkCreateHeadlessSurfaceEXT is unavailable");
    VkHeadlessSurfaceCreateInfoEXT info{VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT};
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    Require(create(instance, &info, nullptr, &surface) == VK_SUCCESS, "vkCreateHeadlessSurfaceEXT failed");
    return surface;
}

void drawableSize(void* context, std::uint32_t* width, std::uint32_t* height) {
    const auto* headless = static_cast<const Headless*>(context);
    *width = headless->width;
    *height = headless->height;
}

bool headlessSurfaces(PFN_vkGetInstanceProcAddr resolve) {
    const auto enumerate = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(resolve(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties"));
    if (enumerate == nullptr) return false;
    std::uint32_t count = 0;
    if (enumerate(nullptr, &count, nullptr) != VK_SUCCESS) return false;
    std::vector<VkExtensionProperties> extensions(count);
    if (enumerate(nullptr, &count, extensions.data()) != VK_SUCCESS) return false;
    for (const auto& extension : extensions) {
        if (std::strcmp(extension.extensionName, VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME) == 0) return true;
    }
    return false;
}

void setUpscale(const char* value) {
#ifdef _WIN32
    _putenv_s("ANYPS5_UPSCALE", value);
#else
    setenv("ANYPS5_UPSCALE", value, 1);
#endif
}

void expectUpscaled(VulkanDevice& device, std::uint32_t width, std::uint32_t height, bool upscaled, const char* failure) {
    const auto before = VulkanDevice::UpscaledPresents();
    const std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height * 4, std::byte{0x40});
    device.PresentPixels(width, height, pixels);
    Require(VulkanDevice::UpscaledPresents() - before == (upscaled ? 1u : 0u), failure);
}

}

int main() {
    try {
#ifdef _WIN32
        void* library = SDL_LoadObject("vulkan-1.dll");
#else
        void* library = SDL_LoadObject("libvulkan.so.1");
#endif
        const auto resolve = library != nullptr ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_LoadFunction(library, "vkGetInstanceProcAddr")) : nullptr;
        if (resolve == nullptr || !headlessSurfaces(resolve)) {
            std::printf("skipped, no Vulkan loader with %s\n", VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME);
            return VulkanTestSkipped;
        }
        Headless headless{1084, 610, resolve};
        const char* extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME};
        const AgcDriver::PresentationWindow window{&headless, extensions, createSurface, drawableSize, 3840, 2160, nullptr};
        setUpscale("fsr");
        {
            VulkanDevice device(&window);
            expectUpscaled(device, 3840, 2160, false, "FSR ran on a 3840x2160 display buffer in a 1084x610 window, where it cannot upscale");
            expectUpscaled(device, 1084, 610, false, "FSR ran on a display buffer the size of its window");
            expectUpscaled(device, 960, 540, true, "FSR did not upscale a 960x540 display buffer into a 1084x610 window");
            expectUpscaled(device, 1920, 1080, false, "FSR ran again once the display buffer was larger than the window");
        }
        setUpscale("");
        {
            VulkanDevice device(&window);
            expectUpscaled(device, 960, 540, false, "FSR ran with ANYPS5_UPSCALE empty");
        }
        std::cout << "FSR presents only where it upscales\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
