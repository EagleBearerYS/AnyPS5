#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
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
#include <csignal>
#include <ctime>
#include <dirent.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <fstream>
#include <map>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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
constexpr std::uint32_t MaxObjectSets = 8;
constexpr std::size_t ColorOffset = (ObjectOffset + MaxObjectSets * MaxDraws * ObjectBytes + 65535) & ~static_cast<std::size_t>(65535);
constexpr std::size_t ColorBytes = 65536;
constexpr std::uint32_t RenderTargetTileMode = 0x1b;
constexpr std::uint32_t MaxRecords = 4;
constexpr std::uint32_t RecordBytes = 16;
constexpr std::size_t RecordOffset = ColorOffset + ColorBytes;
constexpr std::uint32_t RingSlots = 3;
constexpr std::size_t RingOffset = RecordOffset + MaxDraws * MaxRecords * RecordBytes;
constexpr std::size_t RingStride = 0x400;
constexpr std::size_t RingFrameDataOffset = 0x300;
constexpr std::uint32_t BaseVertexLocation = 0x8e;
constexpr std::uint32_t StartInstanceLocation = 0x8f;
constexpr std::size_t BlockBytes = (RingOffset + RingSlots * RingStride + 65535) & ~static_cast<std::size_t>(65535);

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

alignas(256) constexpr std::array<std::uint32_t, 40> VertexCodeUser8 = [] {
    auto code = VertexCode;
    for (const std::size_t load : {10u, 12u, 14u}) code[load] |= 4u;
    return code;
}();

const std::array<std::uint32_t, 40>& VertexProgram() {
    static const bool userAtEight = [] {
        const char* text = std::getenv("DRAW_RECORD_COST_VERTEX_SGPR");
        return text != nullptr && std::string(text) == "8";
    }();
    return userAtEight ? VertexCodeUser8 : VertexCode;
}

alignas(256) constexpr std::array<std::uint32_t, 41> VertexIndirectCode = [] {
    std::array<std::uint32_t, 41> code{};
    std::size_t n = 0;
    for (std::size_t word = 0; word < VertexCodeUser8.size(); ++word) {
        code[n++] = word == 8 ? 0x7e06020au : VertexCodeUser8[word];
        if (word == 9) code[n++] = 0x0606060bu;
    }
    return code;
}();

std::uint32_t IndirectRecords() {
    static const std::uint32_t records = [] {
        const char* text = std::getenv("DRAW_RECORD_COST_INDIRECT");
        const auto parsed = text != nullptr ? std::strtoul(text, nullptr, 10) : 0ul;
        return static_cast<std::uint32_t>(std::min<unsigned long>(parsed, MaxRecords));
    }();
    return records;
}

std::uintptr_t VertexAddress() {
    return IndirectRecords() != 0 ? reinterpret_cast<std::uintptr_t>(VertexIndirectCode.data()) : reinterpret_cast<std::uintptr_t>(VertexProgram().data());
}

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

alignas(256) constexpr std::array<std::uint32_t, PixelWords + 1> PixelValueCode = [] {
    std::array<std::uint32_t, PixelWords + 1> code{};
    std::size_t n = 0;
    for (const auto word : PixelCode) {
        if (word == 0xf800180fu) code[n++] = 0x06101002u;
        code[n++] = word;
    }
    return code;
}();

bool ObjectValues() {
    static const bool values = [] {
        const char* text = std::getenv("DRAW_RECORD_COST_OBJECT_MODE");
        return text != nullptr && std::string(text) == "value";
    }();
    return values;
}

std::uintptr_t PixelProgram() {
    return ObjectValues() ? reinterpret_cast<std::uintptr_t>(PixelValueCode.data()) : reinterpret_cast<std::uintptr_t>(PixelCode.data());
}

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

struct Layout {
    const GuestBlock& data;
    const GuestBlock& textures;
    const GuestBlock& target;
};

std::array<std::uint32_t, 4> BufferDescriptor(std::uint64_t address, std::uint32_t stride, std::uint32_t records, std::uint32_t word3) {
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (stride << 16u), records, word3};
}

void Prepare(const Layout& layout) {
    const auto& block = layout.data;
    for (std::uint32_t texture = 0; texture < Textures; ++texture) {
        const auto address = layout.textures.At(TextureOffset + texture * TextureBytes);
        std::memset(layout.textures.Bytes(TextureOffset + texture * TextureBytes), static_cast<int>(TexelByte), TextureBytes);
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
    for (std::uint32_t object = 0; object < MaxObjectSets * MaxDraws; ++object) std::memcpy(block.Bytes(ObjectOffset + object * ObjectBytes), &one, sizeof(one));
    for (std::uint32_t slot = 0; slot < RingSlots; ++slot) {
        const auto ring = RingOffset + slot * RingStride;
        std::memcpy(block.Bytes(ring), block.Bytes(SrtOffset), FrameConstantsOffset);
        const auto constants = BufferDescriptor(block.At(ring + RingFrameDataOffset), 0, 256, 0x31016facu);
        std::memcpy(block.Bytes(ring + FrameConstantsOffset), constants.data(), sizeof(constants));
        std::memcpy(block.Bytes(ring + RingFrameDataOffset), &one, sizeof(one));
    }
}

void WriteRecords(const GuestBlock& block, std::uint32_t draws, std::uint32_t frame) {
    for (std::uint32_t draw = 0; draw < draws; ++draw) {
        for (std::uint32_t record = 0; record < IndirectRecords(); ++record) {
            const auto serial = (frame * MaxDraws + draw) * MaxRecords + record;
            const std::array<std::uint32_t, 4> words{3u, 1u, serial * 3u, serial + 1u};
            std::memcpy(block.Bytes(RecordOffset + (static_cast<std::size_t>(draw) * MaxRecords + record) * RecordBytes), words.data(), sizeof(words));
        }
    }
}

void Append(std::vector<std::uint32_t>& commands, std::uint32_t opcode, std::initializer_list<std::uint32_t> payload) {
    commands.push_back(0xc0000000u | (static_cast<std::uint32_t>(payload.size() - 1) << 16u) | (opcode << 8u));
    commands.insert(commands.end(), payload);
}

std::uint32_t ObjectSets() {
    static const std::uint32_t sets = [] {
        const char* text = std::getenv("DRAW_RECORD_COST_OBJECT_SETS");
        const auto parsed = text != nullptr ? std::strtoul(text, nullptr, 10) : 1ul;
        return static_cast<std::uint32_t>(std::clamp<unsigned long>(parsed, 1ul, MaxObjectSets));
    }();
    return sets;
}

std::vector<std::uint32_t> Commands(const Layout& layout, std::uint32_t draws, std::uint32_t set = 0) {
    const auto& block = layout.data;
    const auto color = layout.target.At(ColorOffset);
    const auto indirect = IndirectRecords();
    const auto vertex = VertexAddress();
    const auto pixel = PixelProgram();
    const auto srt = block.At(indirect != 0 ? RingOffset + (set % RingSlots) * RingStride : SrtOffset);
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
    std::vector<std::pair<std::uint32_t, std::uint32_t>> shader{
        {0xc8, static_cast<std::uint32_t>(vertex >> 8u)}, {0xc9, static_cast<std::uint32_t>(vertex >> 40u)}, {0x8b, (indirect != 0 ? 4u : 2u) << 1u},
        {0x8c, static_cast<std::uint32_t>(table)}, {0x8d, static_cast<std::uint32_t>(table >> 32u)},
        {0x008, static_cast<std::uint32_t>(pixel >> 8u)}, {0x009, static_cast<std::uint32_t>(pixel >> 40u)}, {0x00b, 8u << 1u},
        {0x00c, static_cast<std::uint32_t>(srt)}, {0x00d, static_cast<std::uint32_t>(srt >> 32u)}, {0x00e, 0}, {0x00f, 0},
    };
    if (indirect != 0) shader.insert(shader.begin() + 5, {{BaseVertexLocation, 0}, {StartInstanceLocation, 0}});
    std::vector<std::uint32_t> commands;
    Append(commands, 0x79, {0x242, 4});
    if (indirect != 0) {
        const auto records = block.At(RecordOffset);
        Append(commands, 0x79, {0x24a, 0});
        Append(commands, 0x11, {1, static_cast<std::uint32_t>(records), static_cast<std::uint32_t>(records >> 32u)});
    }
    for (const auto& [offset, value] : context) Append(commands, 0x69, {offset, value});
    for (const auto& [offset, value] : shader) Append(commands, 0x76, {offset, value});
    for (std::uint32_t draw = 0; draw < draws; ++draw) {
        const auto objectIndex = ObjectValues() ? std::size_t{0} : static_cast<std::size_t>(set) * MaxDraws + draw;
        const auto object = BufferDescriptor(block.At(ObjectOffset + objectIndex * ObjectBytes), 0, 16, 0x31016facu);
        Append(commands, 0x76, {0x010, object[0], object[1], object[2], object[3]});
        if (ObjectValues()) Append(commands, 0x76, {0x00e, set * MaxDraws + draw + 1u});
        const auto offset = draw * MaxRecords * RecordBytes;
        if (indirect == 1) Append(commands, 0x24, {offset, BaseVertexLocation, StartInstanceLocation, 2});
        else if (indirect != 0) Append(commands, 0x2c, {offset, BaseVertexLocation, StartInstanceLocation, 0, indirect, 0, 0, RecordBytes, 2});
        else Append(commands, 0x2d, {3, 2});
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

void Check(const Layout& layout, std::uint32_t set = 0, std::uint32_t frame = 0) {
    std::memset(layout.target.Bytes(ColorOffset), 0, ColorBytes);
    if (IndirectRecords() != 0) WriteRecords(layout.data, 16, frame);
    auto commands = Commands(layout, 16, set);
    Submit(commands);
    const auto byte = ColorByte(layout.target);
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

class Contender {
public:
    Contender(std::uint32_t holdUs, std::uint32_t periodUs) : holdUs(holdUs), periodUs(periodUs) {
        thread = std::thread([this] { run(); });
    }

    Contender(const Contender&) = delete;
    Contender& operator=(const Contender&) = delete;

    ~Contender() {
        stop.store(true, std::memory_order_release);
        thread.join();
    }

    std::uint64_t Holds() const { return holds.load(std::memory_order_relaxed); }
    double WaitedUs() const { return static_cast<double>(waitedNs.load(std::memory_order_relaxed)) / 1e3; }

private:
    void run() {
        while (!stop.load(std::memory_order_acquire)) {
            const auto asked = std::chrono::steady_clock::now();
            {
                AgcDriver::GuestMemory::TagGpuLockSite(AgcDriver::GuestMemory::GpuLockSite::Dispatch);
                std::lock_guard lock(AgcDriver::GuestMemory::GpuMutex());
                const auto got = std::chrono::steady_clock::now();
                waitedNs.fetch_add(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(got - asked).count()), std::memory_order_relaxed);
                while (std::chrono::steady_clock::now() - got < std::chrono::microseconds(holdUs)) {}
            }
            holds.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::microseconds(periodUs > holdUs ? periodUs - holdUs : 0));
        }
    }

    std::uint32_t holdUs;
    std::uint32_t periodUs;
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> holds{0};
    std::atomic<std::uint64_t> waitedNs{0};
    std::thread thread;
};

double Median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

std::string ProcessMemory() {
#ifdef __linux__
    std::ifstream status("/proc/self/status");
    std::string line, text;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) == 0 || line.rfind("RssAnon:", 0) == 0 || line.rfind("RssFile:", 0) == 0 || line.rfind("RssShmem:", 0) == 0) {
            std::string compact;
            bool space = false;
            for (const char c : line) {
                if (c == ' ' || c == '\t') space = !compact.empty() && compact.back() != ':';
                else {
                    if (space) compact += ' ';
                    compact += c;
                    space = false;
                }
            }
            text += (text.empty() ? "" : ", ") + compact;
        }
    }
    return text;
#else
    return "n/a";
#endif
}

std::vector<std::uint32_t> IndirectSizes() {
    const char* count = std::getenv("DRAW_RECORD_COST_DRAWS");
    if (count != nullptr) return {static_cast<std::uint32_t>(std::clamp<unsigned long>(std::strtoul(count, nullptr, 10), 1ul, MaxDraws))};
    const char* sizes = std::getenv("DRAW_RECORD_COST_SIZES");
    std::vector<std::uint32_t> chosen;
    for (const auto draws : {64u, 512u, 2048u}) {
        if (sizes == nullptr || std::string(sizes).find(std::to_string(draws)) != std::string::npos) chosen.push_back(draws);
    }
    return chosen;
}

void SyncFrame(const Layout& layout) {
    if (std::getenv("DRAW_RECORD_COST_GPU_SYNC") != nullptr) AgcDriver::GuestMemory::FlushGpuWrites(static_cast<std::uintptr_t>(layout.target.At(ColorOffset)), ColorBytes);
}

void PrintBreakdown(const char* when) {
    const auto parts = AgcDriver::DriverDetail::Driver::Get().DrawCacheBreakdown();
    const auto bounded = AgcDriver::DriverDetail::Driver::Get().CpuIndirectCacheSize();
    const auto counters = AgcDriver::DriverDetail::Driver::Get().DrawCacheCounters();
    const auto mib = [](std::uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); };
    std::string ranks;
    for (std::size_t count = 0; count < parts.variantsPerStage.size(); ++count) {
        if (parts.variantsPerStage[count] != 0) ranks += " " + std::to_string(count) + ":" + std::to_string(parts.variantsPerStage[count]);
    }
    const auto total = parts.resultBytes + parts.spirvBytes + parts.memoryBytes + parts.variantBytes + parts.decodeBytes;
    std::printf("draw cache parts (%s): %zu entries, %zu stages, %zu variants (stages by variant count:%s), %zu recipes; pinned results %zu distinct %.2f MiB plus SPIR-V %zu modules %.2f MiB; captured memory %zu ShaderMemory objects, %zu pages, %.2f MiB (captured regions %.2f MiB of it); variants %.2f MiB; decode and state %zu decodes %.2f MiB; total %.2f MiB, %.1f KiB per entry; the cache's own ~%.2f MiB; CPU-path indirect budget: %zu entries counted at %.2f of %.2f MiB, %llu refused, %llu swept; %s\n", when, parts.entries, parts.stages, parts.variants, ranks.c_str(), parts.recipes, parts.results, mib(parts.resultBytes), parts.spirvModules, mib(parts.spirvBytes), parts.memories, parts.pages, mib(parts.memoryBytes), mib(parts.capturedBytes), mib(parts.variantBytes), parts.decodes, mib(parts.decodeBytes), mib(total), parts.entries != 0 ? static_cast<double>(total) / 1024.0 / static_cast<double>(parts.entries) : 0.0, mib(parts.accountedBytes), bounded.entries, mib(bounded.bytes), mib(bounded.budget), static_cast<unsigned long long>(counters.cpuIndirectRefused), static_cast<unsigned long long>(counters.cpuIndirectSwept), ProcessMemory().c_str());
    std::fflush(stdout);
}

void IndirectCost(const Layout& layout, std::uint32_t windows) {
    auto& driver = AgcDriver::DriverDetail::Driver::Get();
    std::uint32_t frame = 1;
    std::printf("before any indirect frame: %s; sizeof(RecompileResult) %zu, sizeof(DispatchVariant) %zu, sizeof(DrawDecode) %zu\n", ProcessMemory().c_str(), sizeof(ShaderRecompiler::RecompileResult), sizeof(AgcDriver::DriverDetail::DispatchVariant), sizeof(AgcDriver::DriverDetail::DrawDecode));
    for (const auto draws : IndirectSizes()) {
        std::vector<std::vector<std::uint32_t>> commandSets;
        for (std::uint32_t slot = 0; slot < RingSlots; ++slot) commandSets.push_back(Commands(layout, draws, slot));
        const auto cold = driver.DrawCacheCounters();
        std::vector<double> warm;
        for (std::uint32_t window = 0; window < 2 * RingSlots; ++window, ++frame) {
            WriteRecords(layout.data, draws, frame);
            warm.push_back(Submit(commandSets[frame % RingSlots]) / draws);
            SyncFrame(layout);
        }
        PrintBreakdown("after the warm-up frames");
        const auto before = driver.DrawCacheCounters();
        std::vector<double> perDraw;
        for (std::uint32_t window = 0; window < windows; ++window, ++frame) {
            WriteRecords(layout.data, draws, frame);
            perDraw.push_back(Submit(commandSets[frame % RingSlots]) / draws);
            SyncFrame(layout);
        }
        const auto after = driver.DrawCacheCounters();
        const auto footprint = driver.DrawCacheSize();
        const auto count = [](std::uint64_t value) { return static_cast<unsigned long long>(value); };
        const auto lookups = after.lookups - before.lookups;
        const auto absent = after.absent - before.absent;
        const auto hits = after.hits - before.hits;
        const auto validated = lookups - absent;
        const auto validateUs = after.validateUs - before.validateUs;
        std::printf("indirect cost: %u draws per frame, %u records per packet, %u frames after %u warm-up frames (ring of %u): median %.2f us per draw packet (min %.2f, max %.2f), %.2f us per record; warm-up frames median %.2f us per draw packet (%llu lookups, %llu hits)\n", draws, IndirectRecords(), windows, 2 * RingSlots, RingSlots, Median(perDraw), *std::min_element(perDraw.begin(), perDraw.end()), *std::max_element(perDraw.begin(), perDraw.end()), Median(perDraw) / IndirectRecords(), Median(warm), count(before.lookups - cold.lookups), count(before.hits - cold.hits));
        std::printf("indirect draw cache: %llu lookups, %llu no entry (%.3f per lookup), %llu hits (%.1f%% reuse), %llu inserts, validation %.1f us per validated lookup and %.1f us per hit (APS5_PROFILE_DRAW only); %zu entries holding %.2f MiB (%.1f KiB each)\n", count(lookups), count(absent), lookups != 0 ? static_cast<double>(absent) / static_cast<double>(lookups) : 0.0, count(hits), lookups != 0 ? 100.0 * static_cast<double>(hits) / static_cast<double>(lookups) : 0.0, count(after.inserts - before.inserts), validated != 0 ? validateUs / static_cast<double>(validated) : 0.0, hits != 0 ? validateUs / static_cast<double>(hits) : 0.0, footprint.entries, static_cast<double>(footprint.variantBytes) / (1024.0 * 1024.0), footprint.entries != 0 ? static_cast<double>(footprint.variantBytes) / 1024.0 / static_cast<double>(footprint.entries) : 0.0);
        PrintBreakdown("after the measured frames");
        Check(layout, frame % RingSlots, frame);
        ++frame;
    }
}

void Cost(const Layout& layout, std::uint32_t windows) {
    const auto texture = layout.textures.At(TextureOffset);
    std::printf("write watch (%s layout): available %d, data block covered %d, watched %d, covers the textures %d, CollectWrites %llu\n", &layout.data == &layout.textures ? "shared" : "split", GuestWriteWatch::GuestWriteWatchAvailable_nid_postfix() ? 1 : 0, GuestWriteWatch::GuestWriteWatchCovers_nid_postfix(static_cast<std::uintptr_t>(layout.data.At(0)), 4096) ? 1 : 0, AgcDriver::GuestMemory::WriteWatched() ? 1 : 0, GuestWriteWatch::GuestWriteWatchCovers_nid_postfix(static_cast<std::uintptr_t>(texture), Textures * TextureBytes) ? 1 : 0, static_cast<unsigned long long>(AgcDriver::GuestMemory::CollectWrites(texture, TextureBytes)));
#ifdef __linux__
    const char* samplePath = std::getenv("DRAW_RECORD_COST_SAMPLES");
    std::unique_ptr<Sampler> sampler = samplePath != nullptr ? std::make_unique<Sampler>(samplePath) : nullptr;
#endif
    const char* contend = std::getenv("DRAW_RECORD_COST_CONTEND");
    std::unique_ptr<Contender> contender;
    if (contend != nullptr) {
        const std::string text(contend);
        const auto colon = text.find(':');
        contender = std::make_unique<Contender>(static_cast<std::uint32_t>(std::stoul(text.substr(0, colon))), static_cast<std::uint32_t>(std::stoul(text.substr(colon + 1))));
    }
    const char* sizes = std::getenv("DRAW_RECORD_COST_SIZES");
    for (const auto draws : {64u, 512u, 2048u}) {
        if (sizes != nullptr && std::string(sizes).find(std::to_string(draws)) == std::string::npos) continue;
        std::vector<std::vector<std::uint32_t>> commandSets;
        for (std::uint32_t set = 0; set < ObjectSets(); ++set) commandSets.push_back(Commands(layout, draws, set));
        for (auto& commands : commandSets) Submit(commands);
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
            perDraw.push_back(Submit(commandSets[window % commandSets.size()]) / draws);
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
    if (contender != nullptr) std::printf("contender: %llu GPU mutex holds of %s us, waited %.1f us each for the mutex\n", static_cast<unsigned long long>(contender->Holds()), contend, contender->Holds() == 0 ? 0.0 : contender->WaitedUs() / static_cast<double>(contender->Holds()));
}

}

int main(int argc, char** argv) {
    try {
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
        }
        if (IndirectRecords() != 0) Register(VertexHeader, VertexIndirectCode.data(), sizeof(VertexIndirectCode), 2);
        else Register(VertexHeader, VertexProgram().data(), sizeof(VertexCode), 2);
        if (ObjectValues()) Register(PixelHeader, PixelValueCode.data(), sizeof(PixelValueCode), 1);
        else Register(PixelHeader, PixelCode.data(), sizeof(PixelCode), 1);
        const char* layoutName = std::getenv("DRAW_RECORD_COST_LAYOUT");
        const bool shared = layoutName != nullptr && std::string(layoutName) == "shared";
        GuestBlock data;
        std::unique_ptr<GuestBlock> textures = shared ? nullptr : std::make_unique<GuestBlock>();
        std::unique_ptr<GuestBlock> target = shared ? nullptr : std::make_unique<GuestBlock>();
        const Layout layout{data, shared ? data : *textures, shared ? data : *target};
        Prepare(layout);
        Check(layout);
        if (argc > 2 && std::string(argv[1]) == "--cost" && IndirectRecords() != 0) IndirectCost(layout, static_cast<std::uint32_t>(std::stoul(argv[2])));
        else if (argc > 2 && std::string(argv[1]) == "--cost") Cost(layout, static_cast<std::uint32_t>(std::stoul(argv[2])));
        AgcDriverShutdown_nid_postfix();
        std::puts("draw record cost: 16 draws sampled 16 textures, 3 vertex buffers and 2 constant buffers");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        try { AgcDriverShutdown_nid_postfix(); } catch (...) {}
        return 1;
    }
}
