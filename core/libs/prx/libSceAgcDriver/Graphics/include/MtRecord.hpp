#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_MTRECORD_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_MTRECORD_HPP

#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace AgcDriver::Graphics::MtRecord {

using Emit = std::function<void(VkCommandBuffer, const Context&)>;

struct Inheritance {
    bool dynamicRendering = false;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    std::vector<VkFormat> colors;
    VkFormat depth = VK_FORMAT_UNDEFINED;
    VkFormat stencil = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
};

std::uint32_t Threads();
std::size_t ChunkDraws();
bool ReleaseOffCommitter();
void Release(std::function<void()>& work);

struct Stats {
    std::uint64_t runs = 0;
    std::uint64_t draws = 0;
    std::uint64_t chunks = 0;
};
Stats Counted();

struct Chunk;

class Run {
public:
    Run(const Context& context, Inheritance inheritance);
    Run(const Run&) = delete;
    Run& operator=(const Run&) = delete;
    ~Run();
    void Add(Emit emit);
    void Finish(VkCommandBuffer primary);
    std::size_t Draws() const { return draws; }
    std::size_t Chunks() const { return chunks.size(); }

private:
    void dispatch();
    void wait() noexcept;
    Context context;
    Inheritance inheritance;
    std::vector<std::shared_ptr<Chunk>> chunks;
    std::shared_ptr<Chunk> current;
    std::size_t draws = 0;
};

}

#endif
