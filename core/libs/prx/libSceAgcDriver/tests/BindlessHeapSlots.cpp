#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/BindlessHeap.hpp"
#include "prx/libSceAgcDriver/Graphics/include/PlaceholderImages.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Sampler.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Texture.hpp"
#include "RuntimeAbi.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

using namespace AgcDriver::Graphics;
namespace Abi = ShaderRecompiler::RuntimeAbi;

constexpr int VulkanTestSkipped = 77;

void require(bool condition, const std::string& what) {
    if (!condition) throw std::runtime_error(what);
}

alignas(64) std::array<std::array<std::byte, 64>, 2> fakeTextures{};

std::shared_ptr<Texture> fakeTexture(std::size_t index) {
    return std::shared_ptr<Texture>(reinterpret_cast<Texture*>(fakeTextures[index].data()), [](Texture*) {});
}

void textureSlots(const Context& context, VkImageView view) {
    BindlessHeap heap(context);
    const auto kind = Abi::FirstImageBinding;
    auto first = fakeTexture(0);
    const auto slot = heap.TextureSlot(kind, first, view, VK_IMAGE_LAYOUT_GENERAL);
    require(slot.has_value(), "the first texture takes a slot");
    require(heap.SlotTexture(kind, *slot) == first.get(), "the slot holds the first texture");
    const auto generation = heap.SlotGeneration(kind, *slot);
    std::array<std::uint32_t, Abi::GpuLookupKeyDwords> firstKey{kind, 0u, 0x1111u, 1u, 2u, 3u, 4u, 5u, 6u, 7u};
    require(heap.LookupWord(kind, firstKey.data(), *slot) == Abi::GpuLookupSentinel, "the first key is inserted for the slot");
    require(heap.LookupWord(kind, firstKey.data(), *slot) == Abi::GpuLookupSentinel, "the first key still maps to the slot");
    require(heap.LookupWord(kind, firstKey.data(), *slot + 1u) == *slot + 1u, "a key taken by another slot is named by the draw");

    auto second = fakeTexture(1);
    require(!heap.TextureSlot(kind, second, view, VK_IMAGE_LAYOUT_GENERAL).has_value(), "a held slot is not reused");
    first.reset();
    const auto reused = heap.TextureSlot(kind, second, view, VK_IMAGE_LAYOUT_GENERAL);
    require(reused.has_value() && *reused == *slot, "the released texture's slot is reused");
    require(heap.SlotGeneration(kind, *slot) != generation, "a freed slot moves its generation");
    require(heap.SlotTexture(kind, *slot) == second.get(), "the reused slot holds the second texture");
    require(heap.LookupWord(kind, firstKey.data(), *slot + 1u) == Abi::GpuLookupSentinel, "the freed slot's key is tombstoned and revived for the new slot");
    const auto counts = heap.Statistics();
    require(counts.slotsFreed == 1u && counts.slotsReused == 1u && counts.lookupRevived == 1u, "one slot freed, reused and its key revived");
}

void samplerSlots(const Context& context) {
    BindlessHeap heap(context);
    auto cache = std::make_unique<SamplerCache>();
    const std::array<std::uint32_t, 4> firstWords{0u, 0u, 0u, 0u};
    const std::array<std::uint32_t, 4> secondWords{0u, 0u, 0x00100000u, 0u};
    auto first = cache->Get(context, firstWords, false);
    const auto slot = heap.SamplerSlot(first);
    require(slot.has_value(), "the first sampler takes a slot");
    const auto generation = heap.SamplerGeneration(*slot);
    require(first->CacheKey() != nullptr, "a cached sampler carries its key");
    first.reset();
    cache.reset();
    cache = std::make_unique<SamplerCache>();
    auto second = cache->Get(context, secondWords, false);
    const auto reused = heap.SamplerSlot(second);
    require(reused.has_value() && *reused == *slot, "the released sampler's slot is reused");
    require(heap.SamplerGeneration(*slot) != generation, "a freed sampler slot moves its generation");
}

}

int main() {
    try {
        std::unique_ptr<AgcDriver::VulkanDevice> device;
        try {
            device = std::make_unique<AgcDriver::VulkanDevice>();
        } catch (const std::exception& error) {
            if (std::getenv("ANYPS5_REQUIRE_VULKAN") != nullptr) throw;
            std::printf("skipped, no usable Vulkan device: %s\n", error.what());
            return VulkanTestSkipped;
        }
#ifdef _WIN32
        _putenv_s("APS5_HEAP_SLOT_CAP", "1");
#else
        setenv("APS5_HEAP_SLOT_CAP", "1", 1);
#endif
        {
            const auto context = device->GraphicsContext();
            const PlaceholderImages placeholders(context);
            textureSlots(context, placeholders.View(Abi::FirstImageBinding));
            samplerSlots(context);
            device->WaitIdle();
        }
        device.reset();
        std::cout << "bindless heap slot reuse tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "bindless heap slot reuse tests failed: " << error.what() << '\n';
        return 1;
    }
}
