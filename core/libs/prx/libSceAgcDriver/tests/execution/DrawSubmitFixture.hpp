#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_TESTS_EXECUTION_DRAWSUBMITFIXTURE_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_TESTS_EXECUTION_DRAWSUBMITFIXTURE_HPP

#include "DeviceTableFixture.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include <bit>
#include <cstring>

namespace DrawSubmit {

using DeviceTable::Expect;
using DeviceTable::Hex;
using NewPath::Commands;
using NewPath::High;
using NewPath::Low;
using NewPath::Program;

constexpr std::uint32_t SetContextRegister = 0x69u;
constexpr std::uint32_t SetUconfigRegister = 0x79u;
constexpr std::uint8_t VertexShaderType = 2;
constexpr std::uint8_t PixelShaderType = 1;
constexpr std::uint32_t CenterAndPosition = 0x302u;
constexpr std::uint32_t Index16 = 0u;
constexpr std::uint32_t Index32 = 1u;

namespace Isa {

using namespace DeviceTable::Asm;

constexpr std::uint32_t BufferLoadFormatXyzw = 0x03u;
constexpr std::uint32_t BufferLoadDwordX4 = 0x0eu;
constexpr std::uint32_t SLoadDwordX2 = 0x01u;
constexpr std::uint32_t SLoadDwordX8 = 0x03u;
constexpr std::uint32_t SBufferLoadDword = 0x08u;
constexpr std::uint32_t PositionTarget = 12u;
constexpr std::uint32_t ColorTarget = 0u;
constexpr std::uint32_t InlineHalf = 0xf0u;
constexpr std::uint32_t InlineOne = 0xf2u;

constexpr std::array<std::uint32_t, 2> Indexed(std::uint32_t opcode, std::uint32_t vdata, std::uint32_t vaddr, std::uint32_t resource, std::uint32_t offset = 0) {
    return {0xe0000000u | (opcode << 18u) | 0x2000u | (offset & 0xfffu), (ZeroOffset << 24u) | ((resource / 4u) << 16u) | (vdata << 8u) | vaddr};
}

constexpr std::array<std::uint32_t, 2> Export(std::uint32_t target, std::uint32_t first, bool color) {
    return {0xf8000800u | (color ? 0x1000u : 0u) | (target << 4u) | 0xfu, first | ((first + 1u) << 8u) | ((first + 2u) << 16u) | ((first + 3u) << 24u)};
}

}

struct alignas(8) Header {
    Shader shader{};
    ShaderUserData userData{};
};

inline void Register(Header& header, const Program& program, std::uint8_t type) {
    header.shader.file_header = 0x34333231u;
    header.shader.version = 0x18u;
    header.shader.header_size = sizeof(Header);
    header.shader.shader_size = static_cast<std::uint32_t>(program.size * sizeof(std::uint32_t));
    header.shader.code = program.words.data();
    header.shader.type = type;
    header.shader.user_data = &header.userData;
    AgcDriverRegisterShader_nid_postfix(&header.shader);
}

inline std::uint64_t AddressOf(const void* pointer) {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

inline std::uint32_t Bits(float value) {
    return std::bit_cast<std::uint32_t>(value);
}

inline std::uint64_t StageCompiles() {
    return AgcDriver::DriverDetail::Driver::Get().DrawStageCompiles();
}

inline void ExpectStageCompiles(std::uint64_t before, std::uint64_t expected, const std::string& what) {
    const auto compiles = StageCompiles() - before;
    Expect(compiles == expected, what + ": the draws compiled " + std::to_string(compiles) + " stage programs, expected " + std::to_string(expected));
}

inline void Context(Commands& commands, std::uint32_t offset, std::uint32_t value) {
    commands.Append(SetContextRegister, {offset, value});
}

inline void Uconfig(Commands& commands, std::uint32_t offset, std::uint32_t value) {
    commands.Append(SetUconfigRegister, {offset, value});
}

struct Surface {
    const void* pixels;
    std::uint32_t width;
    std::uint32_t height;
};

inline void Baseline(Commands& commands, const Surface& color, std::uint32_t pixelInputs) {
    constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 34> context{{
        {0x2d5, 0x2000}, {0x1b6, 0}, {0x207, 0}, {0x200, 0}, {0x203, 0x800},
        {0x2dc, 0xaa00}, {0x2f8, 0}, {0x292, 2}, {0x293, 0},
        {0x80, 0}, {0x8d, 0}, {0x83, 0xffff}, {0x8c, 0xa},
        {0x2f9, 0x2d}, {0x313, 0x6000}, {0x30e, 0xffffffff}, {0x30f, 0xffffffff},
        {0x206, 0x43f}, {0x204, 0x80000}, {0x205, 0x240},
        {0x8e, 0xf}, {0x8f, 0xf}, {0x202, 0xcc0010},
        {0x1c4, 0}, {0x1c5, 9}, {0x1c3, 4}, {0x31c, 0x28028},
        {0x31b, 0}, {0x31d, 0}, {0x3b8, 0x9000000}, {0x1e0, 0},
        {0xc, 0}, {0x81, 0x80000000}, {0x90, 0x80000000},
    }};
    for (const auto& [offset, value] : context) Context(commands, offset, value);
    const auto address = AddressOf(color.pixels);
    const auto extent = (color.height << 16u) | color.width;
    Context(commands, 0x94, 0x80000000u);
    Context(commands, 0x0d, extent);
    Context(commands, 0x82, extent);
    Context(commands, 0x91, extent);
    Context(commands, 0x95, extent);
    Context(commands, 0x1b3, pixelInputs);
    Context(commands, 0x1b4, pixelInputs);
    Context(commands, 0x318, Low(address >> 8u));
    Context(commands, 0x390, Low(address >> 40u));
    Context(commands, 0x3b0, ((color.width - 1u) << 14u) | (color.height - 1u));
    Context(commands, 0x10f, Bits(static_cast<float>(color.width) / 2.0f));
    Context(commands, 0x110, Bits(static_cast<float>(color.width) / 2.0f));
    Context(commands, 0x111, Bits(-static_cast<float>(color.height) / 2.0f));
    Context(commands, 0x112, Bits(static_cast<float>(color.height) / 2.0f));
    Context(commands, 0x113, Bits(1.0f));
    Context(commands, 0x114, 0u);
    Context(commands, 0xb4, 0u);
    Context(commands, 0xb5, Bits(1.0f));
    Uconfig(commands, 0x242, 4u);
    Uconfig(commands, 0x24a, 0u);
}

inline void Programs(Commands& commands, const Program& vertex, std::uint32_t vertexUserWords, const Program& pixel, std::uint32_t pixelUserWords) {
    commands.Registers(0x0c8, {Low(vertex.Address() >> 8u), Low(vertex.Address() >> 40u)});
    commands.Registers(0x08b, {vertexUserWords << 1u});
    commands.Registers(0x008, {Low(pixel.Address() >> 8u), Low(pixel.Address() >> 40u)});
    commands.Registers(0x00b, {pixelUserWords << 1u});
}

inline void VertexUserData(Commands& commands, std::span<const std::uint32_t> words) {
    commands.Registers(0x08c, words);
}

inline void PixelUserData(Commands& commands, std::span<const std::uint32_t> words) {
    commands.Registers(0x00c, words);
}

inline void Instances(Commands& commands, std::uint32_t instances) {
    commands.Append(0x2fu, {instances});
}

inline void DrawAuto(Commands& commands, std::uint32_t vertices) {
    commands.Append(0x2du, {vertices, 2u});
}

inline void IndexType(Commands& commands, std::uint32_t type) {
    commands.Append(0x2au, {type});
}

inline void IndexBase(Commands& commands, std::uint64_t address) {
    commands.Append(0x26u, {Low(address), High(address)});
}

inline void IndexBufferSize(Commands& commands, std::uint32_t indices) {
    commands.Append(0x13u, {indices});
}

inline void DrawIndex(Commands& commands, std::uint64_t address, std::uint32_t count, std::uint32_t maxSize) {
    commands.Append(0x27u, {maxSize, Low(address), High(address), count, 0u});
}

inline void DrawIndexOffset(Commands& commands, std::uint32_t offset, std::uint32_t count, std::uint32_t maxSize) {
    commands.Append(0x35u, {maxSize, offset, count, 0u});
}

template<typename T>
std::vector<T> ReadBack(const T* data, std::size_t count) {
    std::vector<T> values(count);
    AgcDriver::GuestMemory::Read(AddressOf(data), std::as_writable_bytes(std::span(values)));
    return values;
}

inline bool SameScaled(std::uint32_t word, std::int32_t value, float scale) {
    const auto actual = std::bit_cast<float>(word);
    return std::isfinite(actual) && std::fabs(actual * scale - static_cast<float>(value)) < 0.01f;
}

inline void Cases(std::initializer_list<std::pair<const char*, void (*)()>> cases) {
    for (const auto& [name, run] : cases) {
        try {
            run();
        } catch (const std::exception& error) {
            Expect(false, std::string(name) + ": exception: " + error.what());
        }
    }
}

inline int Main(const char* name, void (*run)()) {
    {
        const auto device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
    }
    try {
        run();
    } catch (const std::exception& error) {
        Expect(false, std::string("exception: ") + error.what());
    }
    try {
        AgcDriverShutdown_nid_postfix();
    } catch (const std::exception& error) {
        Expect(false, std::string("shutdown: ") + error.what());
    }
    return DeviceTable::Finish(name);
}

}

#endif
