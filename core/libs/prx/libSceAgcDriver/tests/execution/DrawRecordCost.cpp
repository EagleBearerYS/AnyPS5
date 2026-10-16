#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/MtRecord.hpp"
#include "prx/libSceAgcDriver/Submit/include/Dcb.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "prx/libc/include/GuestArena.hpp"
#include "prx/libc/include/GuestWriteWatch.hpp"
#include "VulkanTestDevice.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#ifdef __linux__
#include <atomic>
#include <csignal>
#include <ctime>
#include <dirent.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <fstream>
#include <map>
#include <memory>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;

constexpr std::uint32_t Textures = 16;
constexpr std::uint32_t VertexBuffers = 3;
constexpr std::uint32_t MaxDraws = 2048;
constexpr std::uint32_t Format8888UNorm = 56;
constexpr std::uint32_t Type2D = 9;
constexpr std::uint32_t ClampEdge = 2;
constexpr std::uint32_t TexelByte = 4;
constexpr std::size_t SrtOffset = 0;
constexpr std::size_t SamplerOffset = 0x200;
constexpr std::size_t FrameConstantsOffset = 0x210;
constexpr std::size_t VertexTableOffset = 0x300;
constexpr std::size_t FrameDataOffset = 0x400;
constexpr std::size_t VertexDataOffset = 0x800;
constexpr std::size_t TextureOffset = 0x1000;
constexpr std::size_t TextureBytes = 4096;
constexpr std::size_t ObjectOffset = TextureOffset + Textures * TextureBytes;
constexpr std::size_t ObjectBytes = 256;
constexpr std::size_t ColorOffset = (ObjectOffset + MaxDraws * ObjectBytes + 65535) & ~static_cast<std::size_t>(65535);
constexpr std::size_t ColorBytes = 65536;
constexpr std::uint32_t RenderTargetTileMode = 0x1b;
constexpr std::size_t BlockBytes = (ColorOffset + ColorBytes + 65535) & ~static_cast<std::size_t>(65535);

alignas(256) constexpr std::array<std::uint32_t, 40> VertexCode{
    0x7e000d05u, 0x080200f4u, 0x10020300u, 0x100202f4u, 0x060202f3u, 0x060400f3u, 0x10040500u, 0x060404f3u,
    0x7e060280u, 0x7e0802f2u,
    0xf4080100u, 0xfa000000u, 0xf4080200u, 0xfa000010u, 0xf4080300u, 0xfa000020u,
    0xbf8cc07fu,
    0xe00c2000u, 0x80010a05u, 0xe00c2000u, 0x80020e05u, 0xe00c2000u, 0x80031205u,
    0xbf8c3f70u,
    0x0602030au, 0x0604050bu, 0x0606070cu, 0x0608090du,
    0x0602030eu, 0x0604050fu, 0x06060710u, 0x06080911u,
    0x06020312u, 0x06040513u, 0x06060714u, 0x06080915u,
    0xf80008cfu, 0x04030201u,
    0xbf810000u, 0xbf810000u,
};

constexpr std::size_t PixelWords = 10 + Textures * 10 + 5 + 8 + 3;

constexpr std::array<std::uint32_t, PixelWords> MakePixelCode() {
    std::array<std::uint32_t, PixelWords> code{};
    std::size_t n = 0;
    for (const auto word : {0xf4080200u, 0xfa000200u, 0xf4080300u, 0xfa000210u, 0x7e0002f0u, 0x7e0202f0u, 0x7e100280u, 0x7e120280u, 0x7e140280u, 0x7e160280u}) code[n++] = word;
    for (std::uint32_t texture = 0; texture < Textures; ++texture) {
        for (const auto word : {0xf40c0400u, 0xfa000000u | (texture * 32u), 0xbf8cc07fu, 0xf09c0f08u, 0x00440400u, 0xbf8c3f70u, 0x06101104u, 0x06121305u, 0x06141506u, 0x06161707u}) code[n++] = word;
    }
    for (const auto word : {0xf4200606u, 0xfa000000u, 0xf4200642u, 0xfa000000u, 0xbf8cc07fu}) code[n++] = word;
    for (const auto word : {0x10101018u, 0x10121218u, 0x10141418u, 0x10161618u, 0x10101019u, 0x10121219u, 0x10141419u, 0x10161619u}) code[n++] = word;
    for (const auto word : {0xf800180fu, 0x0b0a0908u, 0xbf810000u}) code[n++] = word;
    return code;
}

alignas(256) constexpr std::array<std::uint32_t, PixelWords> PixelCode = MakePixelCode();

struct Header {
    Shader shader{};
    ShaderUserData userData{};
};

alignas(8) Header VertexHeader{};
alignas(8) Header PixelHeader{};

void Register(Header& header, const std::uint32_t* code, std::size_t bytes, std::uint8_t type) {
    header.shader.file_header = 0x34333231u;
    header.shader.version = 0x18u;
    header.shader.header_size = sizeof(Header);
    header.shader.shader_size = static_cast<std::uint32_t>(bytes);
    header.shader.code = code;
    header.shader.type = type;
    header.shader.user_data = &header.userData;
    AgcDriverRegisterShader_nid_postfix(&header.shader);
}

class GuestBlock {
public:
    GuestBlock() {
#ifdef _WIN32
        block = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(BlockBytes, 65536));
        if (block != nullptr) GuestArena::GuestArenaCommit_nid_postfix(block, BlockBytes, PAGE_READWRITE, BlockBytes);
#else
        constexpr std::uintptr_t alignment = 65536;
        void* mapped = mmap(nullptr, BlockBytes + alignment, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapped != MAP_FAILED) {
            const auto begin = reinterpret_cast<std::uintptr_t>(mapped);
            const auto aligned = (begin + alignment - 1) & ~(alignment - 1);
            if (aligned != begin) munmap(mapped, aligned - begin);
            if (aligned + BlockBytes != begin + BlockBytes + alignment) munmap(reinterpret_cast<void*>(aligned + BlockBytes), begin + alignment - aligned);
            block = reinterpret_cast<std::uint8_t*>(aligned);
            GuestWriteWatch::GuestWriteWatchRegister_nid_postfix(block, BlockBytes);
        }
#endif
        Require(block != nullptr, "draw record cost: cannot allocate the guest block");
        std::memset(block, 0, BlockBytes);
        GuestAllocations::Mutation().Add(block, BlockBytes, true, true, true);
    }

    GuestBlock(const GuestBlock&) = delete;
    GuestBlock& operator=(const GuestBlock&) = delete;

    std::uint64_t At(std::size_t offset) const { return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(block + offset)); }
    std::uint8_t* Bytes(std::size_t offset) const { return block + offset; }

private:
    std::uint8_t* block = nullptr;
};

std::array<std::uint32_t, 4> BufferDescriptor(std::uint64_t address, std::uint32_t stride, std::uint32_t records, std::uint32_t word3) {
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (stride << 16u), records, word3};
}

void Prepare(const GuestBlock& block) {
    for (std::uint32_t texture = 0; texture < Textures; ++texture) {
        const auto address = block.At(TextureOffset + texture * TextureBytes);
        std::memset(block.Bytes(TextureOffset + texture * TextureBytes), static_cast<int>(TexelByte), TextureBytes);
        const std::array<std::uint32_t, 8> words{static_cast<std::uint32_t>(address >> 8u), static_cast<std::uint32_t>((address >> 40u) & 0xffu) | (Format8888UNorm << 20u) | (3u << 30u), 7u << 14u, 0xfacu | (Type2D << 28u), 0u, 0u, 0u, 0u};
        std::memcpy(block.Bytes(SrtOffset + texture * 32u), words.data(), sizeof(words));
    }
    const std::array<std::uint32_t, 4> sampler{ClampEdge | (ClampEdge << 3u) | (ClampEdge << 6u), 0u, 0u, 0u};
    std::memcpy(block.Bytes(SamplerOffset), sampler.data(), sizeof(sampler));
    const auto frame = BufferDescriptor(block.At(FrameDataOffset), 0, 256, 0x31016facu);
    std::memcpy(block.Bytes(FrameConstantsOffset), frame.data(), sizeof(frame));
    const float one = 1.0f;
    std::memcpy(block.Bytes(FrameDataOffset), &one, sizeof(one));
    for (std::uint32_t buffer = 0; buffer < VertexBuffers; ++buffer) {
        const auto vertices = BufferDescriptor(block.At(VertexDataOffset + buffer * 0x100u), 16, 3, 0x0104dfacu);
        std::memcpy(block.Bytes(VertexTableOffset + buffer * 16u), vertices.data(), sizeof(vertices));
    }
    for (std::uint32_t draw = 0; draw < MaxDraws; ++draw) std::memcpy(block.Bytes(ObjectOffset + draw * ObjectBytes), &one, sizeof(one));
}

void Append(std::vector<std::uint32_t>& commands, std::uint32_t opcode, std::initializer_list<std::uint32_t> payload) {
    commands.push_back(0xc0000000u | (static_cast<std::uint32_t>(payload.size() - 1) << 16u) | (opcode << 8u));
    commands.insert(commands.end(), payload);
}

std::vector<std::uint32_t> Commands(const GuestBlock& block, std::uint32_t draws) {
    const auto color = block.At(ColorOffset);
    const auto vertex = reinterpret_cast<std::uintptr_t>(VertexCode.data());
    const auto pixel = reinterpret_cast<std::uintptr_t>(PixelCode.data());
    const auto srt = block.At(SrtOffset);
    const auto table = block.At(VertexTableOffset);
    const float scale = 32.0f;
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> context{
        {0x2d5, 0x2000},
        {0x1b6, 0}, {0x207, 0}, {0x200, 0}, {0x203, 0x800},
        {0x2dc, 0xaa00}, {0x2f8, 0}, {0x292, 2}, {0x293, 0},
        {0x80, 0}, {0x8d, 0}, {0x83, 0xffff}, {0x8c, 0xa},
        {0x2f9, 0x2d}, {0x313, 0x6000}, {0x30e, 0xffffffff}, {0x30f, 0xffffffff},
        {0x206, 0x43f}, {0x204, 0x80000}, {0x205, 0x240},
        {0x8e, 0xf}, {0x8f, 0xf}, {0x202, 0xcc0010},
        {0x1c4, 0}, {0x1c5, 9}, {0x1c3, 4}, {0x31c, 0x28028},
        {0x31b, 0}, {0x31d, 0}, {0x3b0, (63u << 14u) | 3u},
        {0x3b8, 0x9000000u | (RenderTargetTileMode << 14u)}, {0x1e0, 0},
        {0xc, 0}, {0xd, 0x40040},
        {0x81, 0x80000000}, {0x82, 0x40040},
        {0x90, 0x80000000}, {0x91, 0x40040},
        {0x94, 0x80000000}, {0x95, 0x40040},
        {0x318, static_cast<std::uint32_t>(color >> 8u)}, {0x390, static_cast<std::uint32_t>(color >> 40u)},
        {0x10f, std::bit_cast<std::uint32_t>(scale)}, {0x110, std::bit_cast<std::uint32_t>(scale)},
        {0x111, std::bit_cast<std::uint32_t>(-2.0f)}, {0x112, std::bit_cast<std::uint32_t>(2.0f)},
        {0x113, std::bit_cast<std::uint32_t>(1.0f)}, {0x114, 0},
        {0xb4, 0}, {0xb5, std::bit_cast<std::uint32_t>(1.0f)},
        {0x2fe, 0}, {0x302, 0}, {0x306, 0}, {0x30a, 0},
    };
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> shader{
        {0xc8, static_cast<std::uint32_t>(vertex >> 8u)}, {0xc9, static_cast<std::uint32_t>(vertex >> 40u)}, {0x8b, 2u << 1u},
        {0x8c, static_cast<std::uint32_t>(table)}, {0x8d, static_cast<std::uint32_t>(table >> 32u)},
        {0x008, static_cast<std::uint32_t>(pixel >> 8u)}, {0x009, static_cast<std::uint32_t>(pixel >> 40u)}, {0x00b, 8u << 1u},
        {0x00c, static_cast<std::uint32_t>(srt)}, {0x00d, static_cast<std::uint32_t>(srt >> 32u)}, {0x00e, 0}, {0x00f, 0},
    };
    std::vector<std::uint32_t> commands;
    Append(commands, 0x79, {0x242, 4});
    for (const auto& [offset, value] : context) Append(commands, 0x69, {offset, value});
    for (const auto& [offset, value] : shader) Append(commands, 0x76, {offset, value});
    for (std::uint32_t draw = 0; draw < draws; ++draw) {
        const auto object = BufferDescriptor(block.At(ObjectOffset + draw * ObjectBytes), 0, 16, 0x31016facu);
        Append(commands, 0x76, {0x010, object[0], object[1], object[2], object[3]});
        Append(commands, 0x2d, {3, 2});
    }
    return commands;
}

double Submit(std::vector<std::uint32_t>& commands) {
    Packet packet{commands.data(), static_cast<std::uint32_t>(commands.size()), 0, {}};
    const auto start = std::chrono::steady_clock::now();
    Require(sceAgcDriverSubmitDcb(&packet) == 0, "draw record cost: the draw submission failed");
    AgcDriverWaitIdle_nid_postfix();
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
}

unsigned ColorByte(const GuestBlock& block) {
    AgcDriver::GuestMemory::FlushGpuWrites(static_cast<std::uintptr_t>(block.At(ColorOffset)), ColorBytes);
    unsigned brightest = 0;
    for (std::size_t i = 0; i < ColorBytes; ++i) brightest = std::max(brightest, static_cast<unsigned>(block.Bytes(ColorOffset)[i]));
    return brightest;
}

void Check(const GuestBlock& block) {
    std::memset(block.Bytes(ColorOffset), 0, ColorBytes);
    auto commands = Commands(block, 16);
    Submit(commands);
    const auto byte = ColorByte(block);
    Require(byte == Textures * TexelByte, "draw record cost: 16 draws sampling 16 textures of byte " + std::to_string(TexelByte) + " wrote " + std::to_string(byte) + ", expected " + std::to_string(Textures * TexelByte));
}

#ifdef __linux__
std::vector<std::pair<pid_t, std::string>> DriverThreads() {
    std::vector<std::pair<pid_t, std::string>> threads;
    if (DIR* tasks = opendir("/proc/self/task")) {
        while (const dirent* entry = readdir(tasks)) {
            if (entry->d_name[0] == '.') continue;
            std::ifstream comm(std::string("/proc/self/task/") + entry->d_name + "/comm");
            std::string name;
            std::getline(comm, name);
            if (name.rfind("qworker 0x0", 0) == 0 || name.rfind("draw-commit", 0) == 0) threads.emplace_back(static_cast<pid_t>(std::stol(entry->d_name)), name + "/" + entry->d_name);
        }
        closedir(tasks);
    }
    std::sort(threads.begin(), threads.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return threads;
}

std::uint64_t ThreadCpuNs(pid_t tid) {
    const auto clock = static_cast<clockid_t>((~static_cast<unsigned>(tid) << 3u) | 6u);
    timespec time{};
    if (clock_gettime(clock, &time) != 0) return 0;
    return static_cast<std::uint64_t>(time.tv_sec) * 1000000000ull + static_cast<std::uint64_t>(time.tv_nsec);
}

class Sampler {
public:
    static constexpr int Depth = 64;
    struct Sample {
        pid_t tid;
        int frames;
        void* pcs[Depth];
    };

    explicit Sampler(std::string path) : path(std::move(path)) {
        samples.resize(200000);
        void* warm[4];
        backtrace(warm, 4);
        struct sigaction action{};
        action.sa_sigaction = &Sampler::Handle;
        action.sa_flags = SA_SIGINFO | SA_RESTART;
        sigemptyset(&action.sa_mask);
        sigaction(SIGPROF, &action, nullptr);
        Instance() = this;
    }

    void Start(std::uint32_t intervalUs) {
        for (const auto& [tid, name] : DriverThreads()) {
            sigevent event{};
            event.sigev_notify = SIGEV_THREAD_ID;
            event.sigev_signo = SIGPROF;
            event._sigev_un._tid = tid;
            timer_t timer{};
            if (timer_create(CLOCK_MONOTONIC, &event, &timer) != 0) continue;
            itimerspec spec{};
            spec.it_interval.tv_nsec = static_cast<long>(intervalUs) * 1000;
            spec.it_value = spec.it_interval;
            timer_settime(timer, 0, &spec, nullptr);
            timers.push_back(timer);
            names[tid] = name;
        }
        std::printf("sampler: sampling %zu threads every %u us\n", timers.size(), intervalUs);
        active.store(true, std::memory_order_release);
    }

    void Stop() {
        active.store(false, std::memory_order_release);
        for (auto timer : timers) timer_delete(timer);
        timers.clear();
    }

    void Write() const {
        std::ofstream out(path);
        const auto count = std::min(next.load(), samples.size());
        for (std::size_t i = 0; i < count; ++i) {
            const auto& sample = samples[i];
            const auto found = names.find(sample.tid);
            out << (found != names.end() ? found->second : std::to_string(sample.tid));
            for (int f = 0; f < sample.frames; ++f) {
                Dl_info info{};
                if (dladdr(sample.pcs[f], &info) != 0 && info.dli_fname != nullptr) {
                    char text[32];
                    std::snprintf(text, sizeof(text), "+0x%llx", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(sample.pcs[f]) - reinterpret_cast<std::uintptr_t>(info.dli_fbase)));
                    out << ';' << info.dli_fname << text;
                } else {
                    out << ";?0x" << std::hex << reinterpret_cast<std::uintptr_t>(sample.pcs[f]) << std::dec;
                }
            }
            out << '\n';
        }
        std::printf("sampler: %zu samples written to %s\n", count, path.c_str());
    }

private:
    static Sampler*& Instance() {
        static Sampler* instance = nullptr;
        return instance;
    }

    static void Handle(int, siginfo_t*, void*) {
        auto* self = Instance();
        if (self == nullptr || !self->active.load(std::memory_order_acquire)) return;
        const auto index = self->next.fetch_add(1, std::memory_order_relaxed);
        if (index >= self->samples.size()) return;
        auto& sample = self->samples[index];
        sample.tid = static_cast<pid_t>(syscall(SYS_gettid));
        sample.frames = backtrace(sample.pcs, Depth);
    }

    std::string path;
    std::vector<Sample> samples;
    std::atomic<std::size_t> next{0};
    std::atomic<bool> active{false};
    std::vector<timer_t> timers;
    std::map<pid_t, std::string> names;
};

std::map<std::string, std::uint64_t> HeldNs() {
    std::map<std::string, std::uint64_t> held;
    for (const auto& total : AgcDriver::GuestMemory::GpuHoldTotals()) held[total.holder + "/" + total.site] += total.ns;
    return held;
}
#endif

double Median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

void Cost(const GuestBlock& block, std::uint32_t windows) {
#ifdef __linux__
    const char* samplePath = std::getenv("DRAW_RECORD_COST_SAMPLES");
    std::unique_ptr<Sampler> sampler = samplePath != nullptr ? std::make_unique<Sampler>(samplePath) : nullptr;
#endif
    const char* sizes = std::getenv("DRAW_RECORD_COST_SIZES");
    for (const auto draws : {64u, 512u, 2048u}) {
        if (sizes != nullptr && std::string(sizes).find(std::to_string(draws)) == std::string::npos) continue;
        auto commands = Commands(block, draws);
        Submit(commands);
        std::vector<double> perDraw;
#ifdef __linux__
        const auto threads = DriverThreads();
        std::vector<std::vector<double>> cpu(threads.size());
        std::map<std::string, std::vector<double>> held;
        if (sampler != nullptr && draws == 2048u) sampler->Start(1000);
#endif
        for (std::uint32_t window = 0; window < windows; ++window) {
#ifdef __linux__
            std::vector<std::uint64_t> cpuBefore;
            for (const auto& thread : threads) cpuBefore.push_back(ThreadCpuNs(thread.first));
            const auto heldBefore = HeldNs();
#endif
            perDraw.push_back(Submit(commands) / draws);
#ifdef __linux__
            for (std::size_t t = 0; t < threads.size(); ++t) cpu[t].push_back(static_cast<double>(ThreadCpuNs(threads[t].first) - cpuBefore[t]) / 1e3 / draws);
            for (const auto& [key, ns] : HeldNs()) {
                const auto found = heldBefore.find(key);
                held[key].push_back(static_cast<double>(ns - (found != heldBefore.end() ? found->second : 0)) / 1e3 / draws);
            }
#endif
        }
#ifdef __linux__
        if (sampler != nullptr && draws == 2048u) sampler->Stop();
#endif
        std::printf("cost: %u draws per submission, %u warm windows: median %.2f us per draw (min %.2f, max %.2f)\n", draws, windows, Median(perDraw), *std::min_element(perDraw.begin(), perDraw.end()), *std::max_element(perDraw.begin(), perDraw.end()));
#ifdef __linux__
        std::string split;
        char text[96];
        for (std::size_t t = 0; t < threads.size(); ++t) {
            std::snprintf(text, sizeof(text), " %s cpu %.2f", threads[t].second.c_str(), Median(cpu[t]));
            split += text;
        }
        for (auto& [key, values] : held) {
            values.resize(windows, 0.0);
            if (Median(values) < 0.005) continue;
            std::snprintf(text, sizeof(text), " held %s %.2f", key.c_str(), Median(values));
            split += text;
        }
        std::printf("split: %u draws, us per draw (median of %u windows):%s\n", draws, windows, split.c_str());
#endif
    }
#ifdef __linux__
    if (sampler != nullptr) sampler->Write();
#endif
}

}

int main(int argc, char** argv) {
    try {
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
        }
        Register(VertexHeader, VertexCode.data(), sizeof(VertexCode), 2);
        Register(PixelHeader, PixelCode.data(), sizeof(PixelCode), 1);
        GuestBlock block;
        Prepare(block);
        Check(block);
        if (argc > 2 && std::string(argv[1]) == "--cost") Cost(block, static_cast<std::uint32_t>(std::stoul(argv[2])));
        const auto mt = AgcDriver::Graphics::MtRecord::Counted();
        AgcDriverShutdown_nid_postfix();
        if (AgcDriver::Graphics::MtRecord::Threads() != 0) std::printf("mt-record: %u threads, %llu draws recorded in %llu secondary command buffers over %llu render passes\n", AgcDriver::Graphics::MtRecord::Threads(), static_cast<unsigned long long>(mt.draws), static_cast<unsigned long long>(mt.chunks), static_cast<unsigned long long>(mt.runs));
        std::puts("draw record cost: 16 draws sampled 16 textures, 3 vertex buffers and 2 constant buffers");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        try { AgcDriverShutdown_nid_postfix(); } catch (...) {}
        return 1;
    }
}
