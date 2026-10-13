#include "prx/libSceAgcDriver/Graphics/include/DescriptorHeap.hpp"
#include "RuntimeAbi.hpp"
#include <algorithm>
#include <string>

namespace AgcDriver::Graphics {

std::uint32_t DescriptorHeap::SamplerSlots(const Context& context) {
    const auto& limits = context.descriptorIndexingLimits;
    return std::min({limits.maxDescriptorSetUpdateAfterBindSamplers, limits.maxPerStageDescriptorUpdateAfterBindSamplers, context.limits.maxSamplerAllocationCount});
}

DescriptorHeap::DescriptorHeap(const Context& context, std::uint32_t samplers) : context(context), samplerCapacity(samplers) {
    Require(samplers > 1u, "the descriptor heap needs room for its null sampler and one guest sampler");
    try {
        const VkDescriptorSetLayoutCreateInfo emptyInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        Check(context.Function<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(context.device, &emptyInfo, nullptr, &empty), "vkCreateDescriptorSetLayout empty");
        const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_SAMPLER, samplers * MaxViews};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        poolInfo.maxSets = MaxViews;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &size;
        Check(context.Function<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(context.device, &poolInfo, nullptr, &pool), "vkCreateDescriptorPool descriptor heap");
        VkSamplerCreateInfo nullInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        nullInfo.magFilter = VK_FILTER_NEAREST;
        nullInfo.minFilter = VK_FILTER_NEAREST;
        nullInfo.addressModeU = nullInfo.addressModeV = nullInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        nullInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        Check(context.Function<PFN_vkCreateSampler>("vkCreateSampler")(context.device, &nullInfo, nullptr, &nullSampler), "vkCreateSampler descriptor heap null sampler");
        slotSamplers.push_back(nullSampler);
    } catch (...) {
        release();
        throw;
    }
}

DescriptorHeap::~DescriptorHeap() {
    release();
}

void DescriptorHeap::release() noexcept {
    if (pool != VK_NULL_HANDLE) context.Function<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(context.device, pool, nullptr);
    for (const auto& item : views) context.Function<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(context.device, item.layout, nullptr);
    if (empty != VK_NULL_HANDLE) context.Function<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(context.device, empty, nullptr);
    if (nullSampler != VK_NULL_HANDLE) context.Function<PFN_vkDestroySampler>("vkDestroySampler")(context.device, nullSampler, nullptr);
    pool = VK_NULL_HANDLE;
    views.clear();
    empty = VK_NULL_HANDLE;
    nullSampler = VK_NULL_HANDLE;
}

void DescriptorHeap::writeSamplers(const View& target, std::uint32_t first, std::uint32_t count) {
    std::vector<VkDescriptorImageInfo> infos;
    infos.reserve(count);
    for (std::uint32_t slot = first; slot < first + count; ++slot) infos.push_back({slotSamplers[slot] != VK_NULL_HANDLE ? slotSamplers[slot] : nullSampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED});
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = target.set;
    write.dstBinding = ShaderRecompiler::RuntimeAbi::HeapSamplerBinding;
    write.dstArrayElement = first;
    write.descriptorCount = count;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    write.pImageInfo = infos.data();
    context.Function<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(context.device, 1, &write, 0, nullptr);
}

const DescriptorHeap::View& DescriptorHeap::view(VkShaderStageFlags stages) {
    for (const auto& item : views) {
        if (item.stages == stages) return item;
    }
    Require(views.size() < MaxViews, "the descriptor heap has a view for every one of " + std::to_string(MaxViews) + " stage combinations");
    View created{stages, VK_NULL_HANDLE, VK_NULL_HANDLE};
    const VkDescriptorSetLayoutBinding binding{ShaderRecompiler::RuntimeAbi::HeapSamplerBinding, VK_DESCRIPTOR_TYPE_SAMPLER, samplerCapacity, stages, nullptr};
    const VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlags{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
    bindingFlags.bindingCount = 1;
    bindingFlags.pBindingFlags = &flags;
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, &bindingFlags};
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    Check(context.Function<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(context.device, &layoutInfo, nullptr, &created.layout), "vkCreateDescriptorSetLayout descriptor heap");
    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = pool;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &created.layout;
    if (const auto result = context.Function<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(context.device, &allocation, &created.set); result != VK_SUCCESS) {
        context.Function<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(context.device, created.layout, nullptr);
        Check(result, "vkAllocateDescriptorSets descriptor heap");
    }
    const auto& added = views.emplace_back(created);
    writeSamplers(added, 0, static_cast<std::uint32_t>(slotSamplers.size()));
    return added;
}

VkDescriptorSetLayout DescriptorHeap::Layout(VkShaderStageFlags stages) {
    std::lock_guard lock(mutex);
    return view(stages).layout;
}

std::uint32_t DescriptorHeap::AllocateSampler(VkSampler sampler) {
    std::lock_guard lock(mutex);
    std::uint32_t slot = static_cast<std::uint32_t>(slotSamplers.size());
    if (!freeSamplers.empty()) {
        slot = freeSamplers.back();
        freeSamplers.pop_back();
        slotSamplers[slot] = sampler;
    } else {
        Require(slot < samplerCapacity, "the descriptor heap has no free sampler slot: " + std::to_string(samplerCapacity) + " samplers are live");
        slotSamplers.push_back(sampler);
    }
    for (const auto& item : views) writeSamplers(item, slot, 1);
    return slot;
}

void DescriptorHeap::ReleaseSampler(std::uint32_t slot) noexcept {
    std::lock_guard lock(mutex);
    slotSamplers[slot] = VK_NULL_HANDLE;
    freeSamplers.push_back(slot);
}

VkDescriptorSet DescriptorHeap::Set(VkShaderStageFlags stages) {
    std::lock_guard lock(mutex);
    return view(stages).set;
}

void DescriptorHeap::Bind(VkCommandBuffer commands, VkPipelineBindPoint bindPoint, VkPipelineLayout pipelineLayout, VkDescriptorSet set) const {
    context.Resolved(&DeviceFunctions::cmdBindDescriptorSets, "vkCmdBindDescriptorSets")(commands, bindPoint, pipelineLayout, ShaderRecompiler::RuntimeAbi::HeapDescriptorSet, 1, &set, 0, nullptr);
}

}
