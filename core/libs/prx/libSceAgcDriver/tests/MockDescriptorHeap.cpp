#include "MockDescriptorHeap.hpp"
#include <map>
#include <string_view>
#include <type_traits>

namespace {

std::uint64_t nextHandle = 1ull << 40u;
std::map<std::uint32_t, VkSampler> slots;

template<typename THandle>
THandle handle() {
    const auto value = nextHandle++;
    if constexpr (std::is_pointer_v<THandle>) return reinterpret_cast<THandle>(static_cast<std::uintptr_t>(value));
    else return static_cast<THandle>(value);
}

VKAPI_ATTR VkResult VKAPI_CALL createSetLayout(VkDevice, const VkDescriptorSetLayoutCreateInfo*, const VkAllocationCallbacks*, VkDescriptorSetLayout* layout) {
    *layout = handle<VkDescriptorSetLayout>();
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL createPool(VkDevice, const VkDescriptorPoolCreateInfo*, const VkAllocationCallbacks*, VkDescriptorPool* pool) {
    *pool = handle<VkDescriptorPool>();
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL allocateSets(VkDevice, const VkDescriptorSetAllocateInfo*, VkDescriptorSet* set) {
    *set = handle<VkDescriptorSet>();
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL createSampler(VkDevice, const VkSamplerCreateInfo*, const VkAllocationCallbacks*, VkSampler* sampler) {
    *sampler = handle<VkSampler>();
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL updateSets(VkDevice, std::uint32_t count, const VkWriteDescriptorSet* writes, std::uint32_t, const VkCopyDescriptorSet*) {
    for (std::uint32_t index = 0; index < count; ++index) slots[writes[index].dstArrayElement] = writes[index].pImageInfo->sampler;
}

VKAPI_ATTR void VKAPI_CALL destroySetLayout(VkDevice, VkDescriptorSetLayout, const VkAllocationCallbacks*) {}
VKAPI_ATTR void VKAPI_CALL destroyPool(VkDevice, VkDescriptorPool, const VkAllocationCallbacks*) {}
VKAPI_ATTR void VKAPI_CALL destroySampler(VkDevice, VkSampler, const VkAllocationCallbacks*) {}
VKAPI_ATTR void VKAPI_CALL bindSets(VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout, std::uint32_t, std::uint32_t, const VkDescriptorSet*, std::uint32_t, const std::uint32_t*) {}

PFN_vkVoidFunction VKAPI_CALL heapProc(VkDevice, const char* name) {
    static const std::map<std::string_view, PFN_vkVoidFunction> table{
        {"vkCreateDescriptorSetLayout", reinterpret_cast<PFN_vkVoidFunction>(createSetLayout)},
        {"vkDestroyDescriptorSetLayout", reinterpret_cast<PFN_vkVoidFunction>(destroySetLayout)},
        {"vkCreateDescriptorPool", reinterpret_cast<PFN_vkVoidFunction>(createPool)},
        {"vkDestroyDescriptorPool", reinterpret_cast<PFN_vkVoidFunction>(destroyPool)},
        {"vkAllocateDescriptorSets", reinterpret_cast<PFN_vkVoidFunction>(allocateSets)},
        {"vkCreateSampler", reinterpret_cast<PFN_vkVoidFunction>(createSampler)},
        {"vkDestroySampler", reinterpret_cast<PFN_vkVoidFunction>(destroySampler)},
        {"vkUpdateDescriptorSets", reinterpret_cast<PFN_vkVoidFunction>(updateSets)},
        {"vkCmdBindDescriptorSets", reinterpret_cast<PFN_vkVoidFunction>(bindSets)}
    };
    const auto it = table.find(name);
    return it == table.end() ? nullptr : it->second;
}

}

AgcDriver::Graphics::DescriptorHeap& MockDescriptorHeap() {
    static AgcDriver::Graphics::DescriptorHeap heap([] {
        AgcDriver::Graphics::Context context{};
        context.deviceProc = heapProc;
        return context;
    }(), 4096u);
    return heap;
}

VkSampler MockHeapSampler(std::uint32_t slot) {
    return slots.at(slot);
}
