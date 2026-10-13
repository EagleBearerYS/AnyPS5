#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_TESTS_MOCKDESCRIPTORHEAP_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_TESTS_MOCKDESCRIPTORHEAP_HPP

#include "prx/libSceAgcDriver/Graphics/include/DescriptorHeap.hpp"
#include <cstdint>

AgcDriver::Graphics::Context MockHeapContext();
AgcDriver::Graphics::DescriptorHeap& MockDescriptorHeap();
VkSampler MockHeapSampler(std::uint32_t slot);
VkDescriptorImageInfo MockHeapEntry(VkDescriptorSet set, std::uint32_t binding, std::uint32_t slot);

#endif
