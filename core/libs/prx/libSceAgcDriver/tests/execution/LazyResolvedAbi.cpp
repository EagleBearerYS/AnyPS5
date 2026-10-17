#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/ShaderPreparationScope.hpp"
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
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <random>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <span>
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
        Require(block != nullptr, "lazy resolved ABI: cannot allocate the guest block");
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
    for (std::uint32_t draw = 0; draw < MaxDraws; ++draw) std::memcpy(block.Bytes(ObjectOffset + draw * ObjectBytes), &one, sizeof(one));
}

void Append(std::vector<std::uint32_t>& commands, std::uint32_t opcode, std::initializer_list<std::uint32_t> payload) {
    commands.push_back(0xc0000000u | (static_cast<std::uint32_t>(payload.size() - 1) << 16u) | (opcode << 8u));
    commands.insert(commands.end(), payload);
}

std::vector<std::uint32_t> Commands(const Layout& layout, std::uint32_t draws, std::uintptr_t pixel = reinterpret_cast<std::uintptr_t>(PixelCode.data()), std::uint32_t blend = 0) {
    const auto& block = layout.data;
    const auto color = layout.target.At(ColorOffset);
    const auto vertex = reinterpret_cast<std::uintptr_t>(VertexCode.data());
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
        {0x3b8, 0x9000000u | (RenderTargetTileMode << 14u)}, {0x1e0, blend},
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
    Require(sceAgcDriverSubmitDcb(&packet) == 0, "lazy resolved ABI: the draw submission failed");
    AgcDriverWaitIdle_nid_postfix();
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
}

unsigned ColorByte(const GuestBlock& block) {
    AgcDriver::GuestMemory::FlushGpuWrites(static_cast<std::uintptr_t>(block.At(ColorOffset)), ColorBytes);
    unsigned brightest = 0;
    for (std::size_t i = 0; i < ColorBytes; ++i) brightest = std::max(brightest, static_cast<unsigned>(block.Bytes(ColorOffset)[i]));
    return brightest;
}

void Check(const Layout& layout) {
    std::memset(layout.target.Bytes(ColorOffset), 0, ColorBytes);
    auto commands = Commands(layout, 16);
    Submit(commands);
    const auto byte = ColorByte(layout.target);
    Require(byte == Textures * TexelByte, "lazy resolved ABI: 16 draws sampling 16 textures of byte " + std::to_string(TexelByte) + " wrote " + std::to_string(byte) + ", expected " + std::to_string(Textures * TexelByte));
}


std::vector<std::uint32_t> SyntheticPixelCode(std::uint32_t index, std::uint32_t blocks) {
    std::vector<std::uint32_t> code;
    code.reserve(PixelCode.size() + 2 + blocks * 8);
    code.push_back(0x7e1802ffu);
    code.push_back(0x3f800000u + index);
    for (const auto word : PixelCode) {
        if (word == 0xf800180fu) {
            for (std::uint32_t block = 0; block < blocks; ++block) {
                for (const auto multiply : {0x10101018u, 0x10121218u, 0x10141418u, 0x10161618u, 0x10101019u, 0x10121219u, 0x10141419u, 0x10161619u}) code.push_back(multiply);
            }
        }
        code.push_back(word);
    }
    return code;
}

struct SyntheticHeader {
    Shader shader{};
    ShaderUserData userData{};
    std::array<ShaderRegister, 4> registers{};
    std::array<ShaderRegister, 1> context{{{0x1b6, 0}}};
};

struct Synthetic {
    SyntheticHeader header;
    std::uint32_t* code = nullptr;
};

class ProgramArena {
public:
    explicit ProgramArena(std::size_t size) : bytes((size + 65535u) & ~std::size_t{65535}) {
#ifdef _WIN32
        base = static_cast<std::uint8_t*>(GuestArena::GuestArenaAllocate_nid_postfix(bytes, 65536));
        if (base != nullptr) GuestArena::GuestArenaCommit_nid_postfix(base, bytes, PAGE_READWRITE, bytes);
#else
        void* mapped = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        base = mapped != MAP_FAILED ? static_cast<std::uint8_t*>(mapped) : nullptr;
#endif
        Require(base != nullptr, "lazy resolved ABI: cannot allocate the program arena");
        GuestAllocations::Mutation().Add(base, bytes, true, true, true);
    }
    ProgramArena(const ProgramArena&) = delete;
    ProgramArena& operator=(const ProgramArena&) = delete;
    std::uint32_t* Place(std::span<const std::uint32_t> code) {
        const auto aligned = (used + 255u) & ~std::size_t{255};
        Require(aligned + code.size_bytes() <= bytes, "lazy resolved ABI: the program arena is full");
        auto* destination = reinterpret_cast<std::uint32_t*>(base + aligned);
        std::memcpy(destination, code.data(), code.size_bytes());
        used = aligned + code.size_bytes();
        return destination;
    }

private:
    std::size_t bytes;
    std::size_t used = 0;
    std::uint8_t* base = nullptr;
};

void RegisterSynthetic(Synthetic& program, ProgramArena& arena, const std::vector<std::uint32_t>& code) {
    program.code = arena.Place(code);
    const auto address = reinterpret_cast<std::uintptr_t>(program.code);
    auto& header = program.header;
    header.registers = {{{0x008, static_cast<std::uint32_t>(address >> 8u)}, {0x009, static_cast<std::uint32_t>(address >> 40u)}, {0x00b, 8u << 1u}, {0x00a, 0}}};
    header.shader.file_header = 0x34333231u;
    header.shader.version = 0x18u;
    header.shader.header_size = sizeof(SyntheticHeader);
    header.shader.shader_size = static_cast<std::uint32_t>(code.size() * sizeof(std::uint32_t));
    header.shader.code = program.code;
    header.shader.type = 1;
    header.shader.user_data = &header.userData;
    header.shader.sh_registers = header.registers.data();
    header.shader.num_sh_registers = static_cast<std::uint32_t>(header.registers.size());
    header.shader.cx_registers = header.context.data();
    header.shader.num_cx_registers = static_cast<std::uint32_t>(header.context.size());
    AgcDriverRegisterShader_nid_postfix(&header.shader);
}

constexpr std::uint32_t Programs = 4;
constexpr std::uint32_t Drawn = 2;

void SetEnvironment(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

void ClearEnvironment(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

void Settle(std::span<const Synthetic> programs) {
    std::vector<const Shader*> registered;
    for (const auto& program : programs) registered.push_back(&program.header.shader);
    AgcDriverEndShaderPreparation_nid_postfix(AgcDriverBeginShaderPreparation_nid_postfix(registered.data(), registered.size()));
    AgcDriverWaitIdle_nid_postfix();
}

std::vector<std::uint8_t> Draw(const Layout& layout, const Synthetic& program) {
    std::memset(layout.target.Bytes(ColorOffset), 0, ColorBytes);
    auto commands = Commands(layout, 1, reinterpret_cast<std::uintptr_t>(program.code));
    Submit(commands);
    AgcDriver::GuestMemory::FlushGpuWrites(static_cast<std::uintptr_t>(layout.target.At(ColorOffset)), ColorBytes);
    return {layout.target.Bytes(ColorOffset), layout.target.Bytes(ColorOffset) + ColorBytes};
}

std::size_t Entries(const Synthetic& program) {
    return AgcDriver::DriverDetail::Driver::Get().PreparedEntries(&program.header.shader);
}

void EagerReference(const Layout& layout, const std::filesystem::path& output) {
    ProgramArena arena(1u << 20u);
    std::vector<Synthetic> programs(1);
    RegisterSynthetic(programs.front(), arena, SyntheticPixelCode(1, 0));
    AgcDriverResolveShaderAbi_nid_postfix(&programs.front().header.shader, {}, {});
    Settle(programs);
    Require(Entries(programs.front()) != 0, "lazy resolved ABI: eager preparation left the resolved shader unprepared");
    const auto bytes = Draw(layout, programs.front());
    std::ofstream file(output, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    Require(file.good(), "lazy resolved ABI: cannot write the eager reference");
}

void LazyResolution(const Layout& layout, const std::filesystem::path& reference) {
    std::vector<std::uint8_t> expected(ColorBytes);
    {
        std::ifstream file(reference, std::ios::binary);
        file.read(reinterpret_cast<char*>(expected.data()), static_cast<std::streamsize>(expected.size()));
        Require(file.gcount() == static_cast<std::streamsize>(expected.size()), "lazy resolved ABI: the eager reference is missing or short");
    }
    ProgramArena arena(1u << 20u);
    std::vector<Synthetic> programs(Programs);
    for (std::uint32_t index = 0; index < Programs; ++index) {
        RegisterSynthetic(programs[index], arena, SyntheticPixelCode(index + 1, 0));
        AgcDriverResolveShaderAbi_nid_postfix(&programs[index].header.shader, {}, {});
    }
    Settle(programs);
    for (std::uint32_t index = 0; index < Programs; ++index) {
        const auto entries = Entries(programs[index]);
        Require(entries == 0, "lazy resolved ABI: ResolveShaderAbi prepared " + std::to_string(entries) + " entries for shader " + std::to_string(index) + " under APS5_LAZY_SHADER_PREPARE");
    }
    std::vector<std::uint8_t> first;
    for (std::uint32_t index = 0; index < Drawn; ++index) {
        auto bytes = Draw(layout, programs[index]);
        if (index == 0) first = std::move(bytes);
        Require(Entries(programs[index]) != 0, "lazy resolved ABI: drawn shader " + std::to_string(index) + " holds no prepared entries");
    }
    for (std::uint32_t index = Drawn; index < Programs; ++index) {
        const auto entries = Entries(programs[index]);
        Require(entries == 0, "lazy resolved ABI: shader " + std::to_string(index) + ", which never drew, holds " + std::to_string(entries) + " prepared entries");
    }
    Require(std::ranges::any_of(first, [](std::uint8_t byte) { return byte != 0; }), "lazy resolved ABI: the drawn shader wrote nothing");
    Require(first == expected, "lazy resolved ABI: the shader prepared at its first draw drew other bytes than with eager preparation");
    std::printf("lazy resolved ABI: %u of %u ABI-resolved shaders drew; the %u that never drew hold no prepared entries, and a drawn one drew the same %zu bytes as with eager preparation\n", Drawn, Programs, Programs - Drawn, first.size());
}

}

int main(int argc, char** argv) {
    const bool eager = argc == 2 && std::string(argv[1]) == "--eager-reference";
    std::filesystem::path reference;
    try {
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
        }
        if (eager) {
            const char* path = std::getenv("LAZY_RESOLVED_ABI_REFERENCE");
            Require(path != nullptr, "lazy resolved ABI: the eager child has no reference path");
            reference = path;
        } else {
            reference = std::filesystem::temp_directory_path() / ("aps5-lazy-resolved-abi-" + std::to_string(std::random_device{}()) + ".bin");
            SetEnvironment("LAZY_RESOLVED_ABI_REFERENCE", reference.string());
            ClearEnvironment("APS5_LAZY_SHADER_PREPARE");
            const std::string command = "\"" + std::string(argv[0]) + "\" --eager-reference";
            Require(std::system(command.c_str()) == 0, "lazy resolved ABI: the eager reference process failed");
            SetEnvironment("APS5_LAZY_SHADER_PREPARE", "1");
        }
        Register(VertexHeader, VertexCode.data(), sizeof(VertexCode), 2);
        Register(PixelHeader, PixelCode.data(), sizeof(PixelCode), 1);
        GuestBlock data;
        GuestBlock textures;
        GuestBlock target;
        const Layout layout{data, textures, target};
        Prepare(layout);
        Check(layout);
        if (eager) EagerReference(layout, reference);
        else LazyResolution(layout, reference);
        AgcDriverShutdown_nid_postfix();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        try {
            AgcDriverShutdown_nid_postfix();
        } catch (...) {
        }
        if (!eager && !reference.empty()) {
            std::error_code ignored;
            std::filesystem::remove(reference, ignored);
        }
        return 1;
    }
    if (!eager) {
        std::error_code ignored;
        std::filesystem::remove(reference, ignored);
    }
    return 0;
}
