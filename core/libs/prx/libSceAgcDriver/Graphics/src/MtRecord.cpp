#include "prx/libSceAgcDriver/Graphics/include/MtRecord.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#ifdef __linux__
#include <pthread.h>
#endif

namespace AgcDriver::Graphics::MtRecord {

struct Worker;

struct Chunk {
    std::vector<Emit> draws;
    const Context* context = nullptr;
    const Inheritance* inheritance = nullptr;
    Worker* owner = nullptr;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    std::atomic<bool> done{false};
    std::exception_ptr error;
    ~Chunk();
};

struct Worker {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::shared_ptr<Chunk>> jobs;
    std::vector<std::pair<VkCommandPool, VkCommandBuffer>> spare;
    VkDevice device = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    std::thread thread;

    void run(std::uint32_t index) {
#ifdef __linux__
        const std::string name = "mt-record " + std::to_string(index);
        pthread_setname_np(pthread_self(), name.c_str());
#endif
        for (;;) {
            std::shared_ptr<Chunk> chunk;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return !jobs.empty(); });
                chunk = std::move(jobs.front());
                jobs.pop_front();
            }
            try {
                record(*chunk);
            } catch (...) {
                chunk->error = std::current_exception();
            }
            chunk->done.store(true, std::memory_order_release);
            chunk->done.notify_all();
        }
    }

    void record(Chunk& chunk) {
        const auto& context = *chunk.context;
        if (device != context.device) {
            VkCommandPoolCreateInfo info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            info.queueFamilyIndex = context.queueFamily;
            Check(context.Function<PFN_vkCreateCommandPool>("vkCreateCommandPool")(context.device, &info, nullptr, &pool), "vkCreateCommandPool mt-record");
            device = context.device;
        }
        VkCommandBuffer commands = VK_NULL_HANDLE;
        {
            std::lock_guard lock(mutex);
            const auto found = std::find_if(spare.begin(), spare.end(), [&](const auto& entry) { return entry.first == pool; });
            if (found != spare.end()) {
                commands = found->second;
                spare.erase(found);
            }
        }
        if (commands == VK_NULL_HANDLE) {
            VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocation.commandPool = pool;
            allocation.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
            allocation.commandBufferCount = 1;
            Check(context.Function<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(context.device, &allocation, &commands), "vkAllocateCommandBuffers mt-record");
        }
        chunk.pool = pool;
        chunk.commands = commands;
        const auto& inheritance = *chunk.inheritance;
        VkCommandBufferInheritanceRenderingInfoKHR rendering{VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO_KHR};
        VkCommandBufferInheritanceInfo inherit{VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
        if (inheritance.dynamicRendering) {
            rendering.colorAttachmentCount = static_cast<std::uint32_t>(inheritance.colors.size());
            rendering.pColorAttachmentFormats = inheritance.colors.empty() ? nullptr : inheritance.colors.data();
            rendering.depthAttachmentFormat = inheritance.depth;
            rendering.stencilAttachmentFormat = inheritance.stencil;
            rendering.rasterizationSamples = inheritance.samples;
            inherit.pNext = &rendering;
        } else {
            inherit.renderPass = inheritance.renderPass;
            inherit.subpass = 0;
            inherit.framebuffer = inheritance.framebuffer;
        }
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
        begin.pInheritanceInfo = &inherit;
        Check(context.Function<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(commands, &begin), "vkBeginCommandBuffer mt-record");
        for (const auto& emit : chunk.draws) emit(commands, context);
        Check(context.Function<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(commands), "vkEndCommandBuffer mt-record");
        chunk.draws.clear();
    }
};

namespace {

std::vector<std::unique_ptr<Worker>>& Workers() {
    static auto* workers = [] {
        auto* created = new std::vector<std::unique_ptr<Worker>>();
        for (std::uint32_t i = 0; i < Threads(); ++i) {
            created->push_back(std::make_unique<Worker>());
            auto* worker = created->back().get();
            worker->thread = std::thread([worker, i] { worker->run(i); });
            worker->thread.detach();
        }
        return created;
    }();
    return *workers;
}

std::atomic<std::uint64_t> countedRuns{0};
std::atomic<std::uint64_t> countedDraws{0};
std::atomic<std::uint64_t> countedChunks{0};

struct Releaser {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::function<void()>> retired;
    Releaser() {
        std::thread([this] {
#ifdef __linux__
            pthread_setname_np(pthread_self(), "mt-release");
#endif
            for (;;) {
                std::function<void()> work;
                {
                    std::unique_lock lock(mutex);
                    wake.wait(lock, [&] { return !retired.empty(); });
                    work = std::move(retired.front());
                    retired.pop_front();
                }
                work = nullptr;
            }
        }).detach();
    }
};

}

Chunk::~Chunk() {
    if (owner == nullptr || commands == VK_NULL_HANDLE) return;
    std::lock_guard lock(owner->mutex);
    owner->spare.emplace_back(pool, commands);
}

std::uint32_t Threads() {
    static const std::uint32_t threads = [] {
        const char* text = std::getenv("APS5_MT_RECORD");
        if (text == nullptr) return 0u;
        return static_cast<std::uint32_t>(std::min<unsigned long>(std::strtoul(text, nullptr, 10), 32));
    }();
    return threads;
}

std::size_t ChunkDraws() {
    static const std::size_t draws = [] {
        const char* text = std::getenv("APS5_MT_RECORD_CHUNK");
        if (text != nullptr && std::strtoul(text, nullptr, 10) != 0) return static_cast<std::size_t>(std::strtoul(text, nullptr, 10));
        return std::max<std::size_t>(1, (64 + Threads() - 1) / std::max<std::uint32_t>(Threads(), 1));
    }();
    return draws;
}

bool ReleaseOffCommitter() {
    static const bool enabled = std::getenv("APS5_MT_RECORD_RELEASE") != nullptr;
    return enabled;
}

void Release(std::function<void()>& work) {
    static auto* releaser = new Releaser();
    {
        std::lock_guard lock(releaser->mutex);
        releaser->retired.push_back(std::move(work));
    }
    releaser->wake.notify_one();
    work = nullptr;
}

Stats Counted() {
    return {countedRuns.load(std::memory_order_relaxed), countedDraws.load(std::memory_order_relaxed), countedChunks.load(std::memory_order_relaxed)};
}

Run::Run(const Context& context, Inheritance inheritance) : context(context), inheritance(std::move(inheritance)) {}

Run::~Run() {
    wait();
}

void Run::Add(Emit emit) {
    if (current == nullptr) {
        current = std::make_shared<Chunk>();
        current->context = &context;
        current->inheritance = &inheritance;
        current->draws.reserve(ChunkDraws());
    }
    current->draws.push_back(std::move(emit));
    ++draws;
    if (current->draws.size() >= ChunkDraws()) dispatch();
}

void Run::dispatch() {
    auto& workers = Workers();
    Require(!workers.empty(), "APS5_MT_RECORD has no recording threads");
    auto* worker = workers[chunks.size() % workers.size()].get();
    current->owner = worker;
    chunks.push_back(current);
    {
        std::lock_guard lock(worker->mutex);
        worker->jobs.push_back(std::move(current));
    }
    worker->wake.notify_one();
}

void Run::wait() noexcept {
    for (const auto& chunk : chunks) chunk->done.wait(false, std::memory_order_acquire);
}

void Run::Finish(VkCommandBuffer primary) {
    if (current != nullptr) dispatch();
    wait();
    std::vector<VkCommandBuffer> secondaries;
    secondaries.reserve(chunks.size());
    for (const auto& chunk : chunks) {
        if (chunk->error != nullptr) std::rethrow_exception(chunk->error);
        secondaries.push_back(chunk->commands);
    }
    countedRuns.fetch_add(1, std::memory_order_relaxed);
    countedDraws.fetch_add(draws, std::memory_order_relaxed);
    countedChunks.fetch_add(chunks.size(), std::memory_order_relaxed);
    if (!secondaries.empty()) context.Function<PFN_vkCmdExecuteCommands>("vkCmdExecuteCommands")(primary, static_cast<std::uint32_t>(secondaries.size()), secondaries.data());
}

}
