#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_DESCRIPTORHEAP_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_DESCRIPTORHEAP_HPP

#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include "ImageTableAbi.hpp"
#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

namespace AgcDriver::Graphics {

class DescriptorHeap {
public:
    static constexpr std::uint32_t Set = 2u;
    static constexpr std::uint32_t ImageBinding = 0u;
    static constexpr std::uint32_t SamplerBinding = 1u;
    static constexpr std::uint32_t SamplerCapacity = 4096u;
    static constexpr std::uint32_t PlaceholderSlot = 0u;

    static bool Enabled() { return ShaderRecompiler::ImageTableAbi::DescriptorHeapEnabled(); }
    static std::uint32_t CapacityFor(const VkPhysicalDeviceDescriptorIndexingPropertiesEXT& limits);

    DescriptorHeap(const Context& context, std::uint32_t capacity, VkImageView placeholder);
    ~DescriptorHeap();
    DescriptorHeap(const DescriptorHeap&) = delete;
    DescriptorHeap& operator=(const DescriptorHeap&) = delete;

    VkDescriptorSetLayout Layout() const { return layout; }
    VkDescriptorSetLayout EmptyLayout() const { return emptyLayout; }
    VkDescriptorSet Handle() const { return set; }
    std::uint32_t Capacity() const { return capacity; }

    std::uint32_t Acquire(VkImageView view, VkImageLayout imageLayout);
    void Release(std::uint32_t slot, std::uint64_t serial);
    std::uint64_t Assigned() const { return assigned; }

private:
    Context context;
    std::uint32_t capacity = 0;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout emptyLayout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    std::mutex mutex;
    std::uint32_t next = PlaceholderSlot + 1u;
    std::vector<std::uint32_t> free;
    std::deque<std::pair<std::uint64_t, std::uint32_t>> retired;
    std::uint64_t assigned = 0;
};

}

#endif
