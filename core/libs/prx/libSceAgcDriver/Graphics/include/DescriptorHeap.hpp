#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_DESCRIPTORHEAP_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_DESCRIPTORHEAP_HPP

#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include <array>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace AgcDriver::Graphics {

class DescriptorHeap {
public:
    static constexpr std::uint32_t MaxViews = 16u;
    enum class Array : std::uint32_t { Samplers, SampledImages, StorageImages, Count };
    struct Capacities {
        std::uint32_t samplers;
        std::uint32_t sampledImages;
        std::uint32_t storageImages;
    };

    DescriptorHeap(const Context& context, Capacities capacities);
    ~DescriptorHeap();
    DescriptorHeap(const DescriptorHeap&) = delete;
    DescriptorHeap& operator=(const DescriptorHeap&) = delete;

    static Capacities DeviceCapacities(const Context& context);
    VkDescriptorSetLayout Layout(VkShaderStageFlags stages);
    VkDescriptorSet Set(VkShaderStageFlags stages);
    VkDescriptorSetLayout EmptyLayout() const { return empty; }
    std::uint32_t Capacity(Array array) const { return arrays[static_cast<std::size_t>(array)].capacity; }
    std::uint32_t AllocateSampler(VkSampler sampler);
    void ReleaseSampler(std::uint32_t slot) noexcept;
    std::uint32_t AllocateImage(Array array, VkImageView view, VkImageLayout layout);
    void ReleaseImage(Array array, std::uint32_t slot) noexcept;
    void Bind(VkCommandBuffer commands, VkPipelineBindPoint bindPoint, VkPipelineLayout pipelineLayout, VkDescriptorSet set) const;

private:
    struct View {
        VkShaderStageFlags stages;
        VkDescriptorSetLayout layout;
        VkDescriptorSet set;
    };
    struct Slots {
        std::uint32_t capacity = 0;
        VkDescriptorType type = VK_DESCRIPTOR_TYPE_SAMPLER;
        std::vector<VkDescriptorImageInfo> entries;
        std::vector<std::uint32_t> free;
    };
    const View& view(VkShaderStageFlags stages);
    std::uint32_t allocate(Array array, const VkDescriptorImageInfo& entry);
    void release(Array array, std::uint32_t slot) noexcept;
    void write(const View& target, Array array, std::uint32_t first, std::uint32_t count);
    void release() noexcept;

    Context context;
    VkDescriptorSetLayout empty = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkSampler nullSampler = VK_NULL_HANDLE;
    std::mutex mutex;
    std::deque<View> views;
    std::array<Slots, static_cast<std::size_t>(Array::Count)> arrays;
};

}

#endif
