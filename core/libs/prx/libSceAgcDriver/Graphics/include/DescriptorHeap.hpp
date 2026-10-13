#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_DESCRIPTORHEAP_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_DESCRIPTORHEAP_HPP

#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace AgcDriver::Graphics {

class DescriptorHeap {
public:
    static constexpr std::uint32_t MaxViews = 16u;

    DescriptorHeap(const Context& context, std::uint32_t samplers);
    ~DescriptorHeap();
    DescriptorHeap(const DescriptorHeap&) = delete;
    DescriptorHeap& operator=(const DescriptorHeap&) = delete;

    static std::uint32_t SamplerSlots(const Context& context);
    VkDescriptorSetLayout Layout(VkShaderStageFlags stages);
    VkDescriptorSet Set(VkShaderStageFlags stages);
    VkDescriptorSetLayout EmptyLayout() const { return empty; }
    std::uint32_t SamplerCapacity() const { return samplerCapacity; }
    std::uint32_t AllocateSampler(VkSampler sampler);
    void ReleaseSampler(std::uint32_t slot) noexcept;
    void Bind(VkCommandBuffer commands, VkPipelineBindPoint bindPoint, VkPipelineLayout pipelineLayout, VkDescriptorSet set) const;

private:
    struct View {
        VkShaderStageFlags stages;
        VkDescriptorSetLayout layout;
        VkDescriptorSet set;
    };
    const View& view(VkShaderStageFlags stages);
    void writeSamplers(const View& target, std::uint32_t first, std::uint32_t count);
    void release() noexcept;

    Context context;
    VkDescriptorSetLayout empty = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkSampler nullSampler = VK_NULL_HANDLE;
    std::uint32_t samplerCapacity = 0;
    std::mutex mutex;
    std::deque<View> views;
    std::vector<VkSampler> slotSamplers;
    std::vector<std::uint32_t> freeSamplers;
};

}

#endif
