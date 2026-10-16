#include "prx/libSceAgcDriver/Graphics/include/DescriptorHeap.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Resources.hpp"
#include <algorithm>
#include <array>

namespace AgcDriver::Graphics {

std::uint32_t DescriptorHeap::CapacityFor(const VkPhysicalDeviceDescriptorIndexingPropertiesEXT& limits) {
    std::uint64_t capacity = 1u << 20u;
    capacity = std::min<std::uint64_t>(capacity, limits.maxDescriptorSetUpdateAfterBindSampledImages);
    capacity = std::min<std::uint64_t>(capacity, limits.maxPerStageDescriptorUpdateAfterBindSampledImages);
    capacity = std::min<std::uint64_t>(capacity, limits.maxPerStageUpdateAfterBindResources > 1024u ? limits.maxPerStageUpdateAfterBindResources - 1024u : 0u);
    return static_cast<std::uint32_t>(capacity);
}

DescriptorHeap::DescriptorHeap(const Context& context, std::uint32_t capacity, VkImageView placeholder) : context(context), capacity(capacity) {
    Require(capacity > 1u, "descriptor heap: the device allows no update-after-bind sampled images");
    const std::array<VkDescriptorSetLayoutBinding, 2> bindings{{
        {ImageBinding, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, capacity, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr},
        {SamplerBinding, VK_DESCRIPTOR_TYPE_SAMPLER, SamplerCapacity, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr},
    }};
    const VkDescriptorBindingFlags heapFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
    const std::array<VkDescriptorBindingFlags, 2> flags{heapFlags, heapFlags};
    VkDescriptorSetLayoutBindingFlagsCreateInfoEXT flagInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT};
    flagInfo.bindingCount = static_cast<std::uint32_t>(flags.size());
    flagInfo.pBindingFlags = flags.data();
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, &flagInfo};
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    const auto createLayout = context.Function<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout");
    Check(createLayout(context.device, &layoutInfo, nullptr, &layout), "vkCreateDescriptorSetLayout descriptor heap");
    VkDescriptorSetLayoutCreateInfo emptyInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    Check(createLayout(context.device, &emptyInfo, nullptr, &emptyLayout), "vkCreateDescriptorSetLayout empty");
    const std::array<VkDescriptorPoolSize, 2> sizes{{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, capacity}, {VK_DESCRIPTOR_TYPE_SAMPLER, SamplerCapacity}}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(sizes.size());
    poolInfo.pPoolSizes = sizes.data();
    Check(context.Function<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(context.device, &poolInfo, nullptr, &pool), "vkCreateDescriptorPool descriptor heap");
    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = pool;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &layout;
    Check(context.Function<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(context.device, &allocation, &set), "vkAllocateDescriptorSets descriptor heap");
    if (placeholder != VK_NULL_HANDLE) {
        const VkDescriptorImageInfo image{VK_NULL_HANDLE, placeholder, VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set;
        write.dstBinding = ImageBinding;
        write.dstArrayElement = PlaceholderSlot;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.pImageInfo = &image;
        context.Resolved(&DeviceFunctions::updateDescriptorSets, "vkUpdateDescriptorSets")(context.device, 1, &write, 0, nullptr);
    }
}

DescriptorHeap::~DescriptorHeap() {
    if (pool != VK_NULL_HANDLE) context.Function<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(context.device, pool, nullptr);
    const auto destroyLayout = context.Function<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout");
    if (layout != VK_NULL_HANDLE) destroyLayout(context.device, layout, nullptr);
    if (emptyLayout != VK_NULL_HANDLE) destroyLayout(context.device, emptyLayout, nullptr);
}

std::uint32_t DescriptorHeap::Acquire(VkImageView view, VkImageLayout imageLayout) {
    std::uint32_t slot = 0;
    {
        std::lock_guard lock(mutex);
        const auto* recorder = Recorder::Active();
        const auto completed = recorder != nullptr ? recorder->CompletedThrough() : ~std::uint64_t{0};
        while (!retired.empty() && retired.front().first <= completed) {
            free.push_back(retired.front().second);
            retired.pop_front();
        }
        if (!free.empty()) {
            slot = free.back();
            free.pop_back();
        } else {
            Require(next < capacity, "descriptor heap: out of slots");
            slot = next++;
        }
        ++assigned;
        const VkDescriptorImageInfo image{VK_NULL_HANDLE, view, imageLayout};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = set;
        write.dstBinding = ImageBinding;
        write.dstArrayElement = slot;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        write.pImageInfo = &image;
        context.Resolved(&DeviceFunctions::updateDescriptorSets, "vkUpdateDescriptorSets")(context.device, 1, &write, 0, nullptr);
    }
    return slot;
}

void DescriptorHeap::Release(std::uint32_t slot, std::uint64_t serial) {
    if (slot == PlaceholderSlot) return;
    std::lock_guard lock(mutex);
    retired.emplace_back(serial, slot);
}

}
