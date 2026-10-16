#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "prx/libSceAgcDriver/Graphics/include/VertexInput.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "Recompiler.hpp"
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
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;
using Path = AgcDriver::Graphics::IndirectDrawPath;

constexpr std::uint32_t Width = 64;
constexpr std::uint32_t Height = 32;
constexpr std::uint32_t Cells = 4;
constexpr std::uint32_t BaseVertexSgpr = 16;
constexpr std::uint32_t NoLocation = 0x280u;
constexpr std::uint32_t Stride = 16;
constexpr std::uint32_t Vertices = 9;
constexpr std::uint32_t Records = Vertices + 2;
std::uint32_t DescriptorRecords = Records;
constexpr std::size_t BlockBytes = 65536;
constexpr std::size_t TailBytes = 4096;
constexpr std::size_t VertexOffset = BlockBytes - Vertices * Stride;
constexpr std::size_t RecordOffset = 0;
constexpr std::size_t IndexOffset = 256;
constexpr std::byte Kept{0x40};

alignas(256) constexpr std::array<std::uint32_t, 12> VertexCode{
    0xd70f7d05u, 0x00020a10u,
    0xf4080506u, 0xfa000000u,
    0x02060b08u,
    0xbf8cc07fu,
    0xe00c2000u, 0x80050803u,
    0xbf8c3f70u,
    0xf80008cfu, 0x0b0a0908u,
    0xbf810000u,
};

alignas(256) constexpr std::array<std::uint32_t, 20> ZeroAttributeCode{
    0xd70f7d05u, 0x00020a10u,
    0xf4080506u, 0xfa000000u,
    0xf4080606u, 0xfa000010u,
    0x02060b08u,
    0xbf8cc07fu,
    0xe00c2000u, 0x80050803u,
    0xe00c2000u, 0x80060c03u,
    0xbf8c3f70u,
    0x0610110cu, 0x0612120du, 0x0614140eu, 0x0616160fu,
    0xf80008cfu, 0x0b0a0908u,
    0xbf810000u,
};

alignas(256) constexpr std::array<std::uint32_t, 4> PixelCode{0x7e0e02f2u, 0xf800180fu, 0x07070707u, 0xbf810000u};

alignas(256) std::array<std::byte, Width * Height * 4> Pixels{};
alignas(256) std::array<std::uint32_t, 8> BufferTable{};
constexpr std::size_t ZeroRecordOffset = 0x1000;

class TailBlock {
public:
    TailBlock() {
        const auto reserved = 2 * BlockBytes + TailBytes;
#ifdef _WIN32
        reservation = VirtualAlloc(nullptr, reserved, MEM_RESERVE, PAGE_NOACCESS);
        Require(reservation != nullptr, "cannot reserve a guest block");
        data = reinterpret_cast<std::byte*>((reinterpret_cast<std::uintptr_t>(reservation) + BlockBytes - 1) & ~(BlockBytes - 1));
        Require(VirtualAlloc(data, BlockBytes, MEM_COMMIT, PAGE_READWRITE) != nullptr, "cannot commit a guest block");
#else
        reservation = mmap(nullptr, reserved, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        Require(reservation != MAP_FAILED, "cannot map a guest block");
        data = reinterpret_cast<std::byte*>((reinterpret_cast<std::uintptr_t>(reservation) + BlockBytes - 1) & ~(BlockBytes - 1));
        Require(mprotect(data + BlockBytes, TailBytes, PROT_NONE) == 0, "cannot protect the guest block's tail");
#endif
        std::memset(data, 0, BlockBytes);
        GuestAllocations::Mutation mutation;
        mutation.Add(data, BlockBytes, true, true, true);
    }
    ~TailBlock() {
        AgcDriver::Graphics::StorageTexture::FlushPending(Address(), BlockBytes, nullptr, "test release");
        {
            GuestAllocations::Mutation mutation;
            mutation.Remove(data);
        }
#ifdef _WIN32
        VirtualFree(reservation, 0, MEM_RELEASE);
#else
        munmap(reservation, 2 * BlockBytes + TailBytes);
#endif
    }
    TailBlock(const TailBlock&) = delete;
    TailBlock& operator=(const TailBlock&) = delete;
    std::uintptr_t Address() const { return reinterpret_cast<std::uintptr_t>(data); }
    std::byte* At(std::size_t offset) const { return data + offset; }

private:
    void* reservation = nullptr;
    std::byte* data = nullptr;
};

std::array<float, 4> Corner(std::uint32_t cell, std::uint32_t corner) {
    constexpr float pixel = 2.0f / static_cast<float>(Width);
    const float left = -1.0f + 0.5f * static_cast<float>(cell) + pixel;
    const float right = -1.0f + 0.5f * static_cast<float>(cell + 1) - pixel;
    if (corner == 0) return {left, -1.0f, 0.0f, 1.0f};
    if (corner == 1) return {right, -1.0f, 0.0f, 1.0f};
    return {left, 1.0f, 0.0f, 1.0f};
}

void PlaceVertices(const TailBlock& block) {
    for (std::uint32_t vertex = 0; vertex < Vertices; ++vertex) {
        const auto position = Corner(1u + vertex / 3u, vertex % 3);
        std::memcpy(block.At(VertexOffset + vertex * Stride), position.data(), sizeof(position));
    }
}

std::array<std::uint32_t, 4> VertexDescriptor(std::uintptr_t address, std::uint32_t records = DescriptorRecords, std::uint32_t select = 0) {
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (Stride << 16u), records, 0x0104dfacu | (select << 28u)};
}

std::vector<std::uint32_t> UserData() {
    std::vector<std::uint32_t> userData(10, 0u);
    const auto table = reinterpret_cast<std::uintptr_t>(BufferTable.data());
    userData[4] = static_cast<std::uint32_t>(table);
    userData[5] = static_cast<std::uint32_t>(table >> 32u);
    return userData;
}

AgcDriver::DriverDetail::DrawProgram Program(const std::vector<std::uint32_t>& userData) {
    AgcDriver::DriverDetail::DrawProgram program{};
    program.firstUserSgpr = 8;
    program.userData = userData;
    return program;
}

ShaderRecompiler::RecompileResult Vertex(AgcDriver::VulkanDevice& device, std::uintptr_t vertexAddress, const std::vector<std::uint32_t>& userData, int zeroSelect, std::uintptr_t zeroAddress) {
    const auto descriptor = VertexDescriptor(vertexAddress);
    std::copy(descriptor.begin(), descriptor.end(), BufferTable.begin());
    const auto zero = VertexDescriptor(zeroAddress, 0, zeroSelect < 0 ? 0u : static_cast<std::uint32_t>(zeroSelect));
    std::copy(zero.begin(), zero.end(), BufferTable.begin() + 4);
    const bool zeroAttribute = zeroSelect >= 0;
    const std::span<const std::uint32_t> code = zeroAttribute ? std::span<const std::uint32_t>(ZeroAttributeCode) : std::span<const std::uint32_t>(VertexCode);
    ShaderRecompiler::ShaderVertexStageInfo info{};
    info.resourcesNum = zeroAttribute ? 2 : 1;
    info.resources[0].fields = descriptor;
    info.resourcesDst[0] = {8, 4, 0, 0};
    info.resources[1].fields = zero;
    info.resourcesDst[1] = {12, 4, 1, 0};
    info.fetchAttribReg = 6;
    info.fetchBufferReg = 4;
    info.fetchEmbedded = true;
    const std::array<ShaderRecompiler::MemoryRegion, 2> memory{{
        {reinterpret_cast<std::uintptr_t>(code.data()), std::as_bytes(code)},
        {reinterpret_cast<std::uintptr_t>(BufferTable.data()), std::as_bytes(std::span(BufferTable))},
    }};
    ShaderRecompiler::RecompileRequest request{
        {ShaderStage::Vertex, reinterpret_cast<std::uintptr_t>(code.data()), code, 0, {}},
        {32, 8, userData, std::nullopt, std::nullopt, info, memory},
        device.Target(),
        {0, 0, 0, 64}
    };
    request.useCache = false;
    auto result = ShaderRecompiler::Recompile(request);
    Require(result.vertexAttributes.size() == (zeroAttribute ? 2u : 1u), "the fetches did not become vertex attributes");
    Require(result.vertexOffsetSgpr == static_cast<std::int32_t>(BaseVertexSgpr), "the fetch add of s16 was not folded into the draw (vertex " + std::to_string(result.vertexOffsetSgpr) + ")");
    return result;
}

AgcDriver::Graphics::State State() {
    AgcDriver::Graphics::State state{};
    state.stages = {AgcDriver::Graphics::ShaderPath::Vertex, 0u, 32u, 32u, std::nullopt, std::nullopt};
    state.color = {reinterpret_cast<std::uintptr_t>(Pixels.data()), {Width, Height}, VK_FORMAT_R8G8B8A8_UNORM, Pixels.size(), 0xe4u};
    state.colors = {state.color};
    state.hasColorTarget = true;
    state.renderExtent = {Width, Height};
    state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    state.viewport = {0.0f, static_cast<float>(Height), static_cast<float>(Width), -static_cast<float>(Height), 0.0f, 1.0f};
    state.negativeOneToOne = false;
    state.scissor = {{0, 0}, {Width, Height}};
    state.cullMode = VK_CULL_MODE_NONE;
    state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    state.blend.colorWriteMask = 15u;
    state.blends = {state.blend};
    return state;
}

bool Covered(std::uint32_t cell) {
    for (std::uint32_t y = 0; y < Height; ++y) {
        for (std::uint32_t x = cell * (Width / Cells); x < (cell + 1) * (Width / Cells); ++x) {
            if (Pixels[(y * Width + x) * 4] != Kept) return true;
        }
    }
    return false;
}

std::string Picture() {
    std::string text;
    for (std::uint32_t y = 0; y < Height; y += 4) {
        for (std::uint32_t x = 0; x < Width; ++x) text += Pixels[(y * Width + x) * 4] != Kept ? '#' : '.';
        text += '\n';
    }
    return text;
}

struct Case {
    std::string what;
    bool indexed;
    std::array<std::uint32_t, 5> record;
    std::array<std::uint16_t, 3> indices;
    std::uint32_t cell;
    int zeroSelect = -1;
    bool direct = false;
};

class Fixture {
public:
    explicit Fixture(std::shared_ptr<AgcDriver::VulkanDevice> device) : device(std::move(device)) {
        ShaderRecompiler::ShaderPixelStageInfo info{};
        info.inputAddr = ShaderRecompiler::PixelInputBit(ShaderRecompiler::PixelInput::PositionX) | ShaderRecompiler::PixelInputBit(ShaderRecompiler::PixelInput::PositionY);
        info.posX = true;
        info.posY = true;
        info.targetOutputMode[0] = 9;
        info.targetExportMapping.fill(0xe4u);
        pixelInfo = info;
        PlaceVertices(block);
    }

    void Submit(const Case& test, std::uintptr_t vertexAddress, std::uint32_t repeats = 1) {
        const auto userData = UserData();
        const auto vertex = Vertex(*device, vertexAddress, userData, test.zeroSelect, block.Address() + ZeroRecordOffset);
        if (test.zeroSelect >= 0) {
            const auto zero = std::find_if(vertex.vertexAttributes.begin(), vertex.vertexAttributes.end(), [](const auto& attribute) { return attribute.resource.fields[2] == 0; });
            Require(zero != vertex.vertexAttributes.end(), "no vertex attribute has 0 records");
            if (test.direct) Require(!AgcDriver::Graphics::VertexFetchOutOfRange(*zero), "the direct draw would bind the format-sized zero buffer, read out of bounds past its first vertex, instead of a zero-filled copy of every vertex it fetches");
            else Require(AgcDriver::Graphics::IndirectVertexFetchOutOfRange(*zero), "the GPU-side draw would not bind the zero buffer for a fetch past 0 records");
        }
        const auto vertexPush = static_cast<std::uint32_t>(vertex.pushConstants.size());
        const std::vector<std::uint32_t> pixelUserData(8, 0u);
        const std::array<ShaderRecompiler::MemoryRegion, 1> pixelMemory{{{reinterpret_cast<std::uintptr_t>(PixelCode.data()), std::as_bytes(std::span(PixelCode))}}};
        ShaderRecompiler::RecompileRequest fragment{
            {ShaderStage::Fragment, reinterpret_cast<std::uintptr_t>(PixelCode.data()), PixelCode, 0, {}},
            {32u, 0, pixelUserData, std::nullopt, pixelInfo, std::nullopt, pixelMemory},
            device->Target(),
            PixelPushLayout(vertexPush, device->Target())
        };
        fragment.useCache = false;
        const auto pixel = ShaderRecompiler::Recompile(fragment);
        const std::array<AgcDriver::Graphics::CompiledShader, 2> shaders{{{ShaderStage::Vertex, &vertex, 0u}, {ShaderStage::Fragment, &pixel, PixelPushOffset(vertexPush, device->Target())}}};

        const std::uint32_t recordBytes = test.indexed ? 20u : 16u;
        std::memcpy(block.At(RecordOffset), test.record.data(), recordBytes);
        std::memcpy(block.At(IndexOffset), test.indices.data(), sizeof(test.indices));
        const auto indexAddress = block.Address() + IndexOffset;
        AgcDriver::Pm4::DrawParameters draw{test.indexed ? indexAddress : 0u, test.indexed ? static_cast<std::uint32_t>(test.indices.size()) : 0u, test.indexed ? 2u : 0u, 0, 0, test.indexed, 0, 0};
        if (test.direct) draw = {0, test.record[0], 0, test.record[1], 0, false, test.record[2], test.record[3]};
        else draw.indirect = AgcDriver::Pm4::DrawParameters::IndirectDraw{block.Address() + RecordOffset, test.indexed ? 0x25u : 0x24u, recordBytes, recordBytes, 1u, false, 0u, 0x10u, NoLocation, NoLocation, false, 0u};
        if (!test.direct) {
            draw.indirect->baseVertexSgpr = static_cast<std::int32_t>(BaseVertexSgpr);
            const auto path = AgcDriver::DriverDetail::Driver::ClassifyIndirectDraw(vertex, State(), Program(userData), device, draw, false);
            Require(!path, test.what + ": classified to the CPU path (" + std::string(path ? AgcDriver::Graphics::IndirectDrawPathName(*path) : "") + ")");
        }
        Pixels.fill(Kept);
        const auto state = State();
        const auto start = std::chrono::steady_clock::now();
        for (std::uint32_t repeat = 0; repeat < repeats; ++repeat) {
            device->Draw(state, draw, shaders);
            if (repeat % 64 == 63) device->WaitIdle();
        }
        drawUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / repeats;
        device->WaitIdle();
        AgcDriver::GuestMemory::FlushGpuWrites(reinterpret_cast<std::uintptr_t>(Pixels.data()), Pixels.size());
    }

    std::string Draw(const Case& test) {
        try {
            Submit(test, block.Address() + VertexOffset);
        } catch (const std::exception& error) {
            return test.what + ": " + error.what() + "\n";
        }
        for (std::uint32_t cell = 0; cell < Cells; ++cell) {
            if (Covered(cell) != (cell == test.cell)) return test.what + ": cell " + std::to_string(cell) + " is " + (cell == test.cell ? "empty" : "drawn") + "\n" + Picture();
        }
        std::cout << test.what << (test.direct ? ": drew directly" : ": drew") << " from the vertices at the block's end\n" << Picture();
        return {};
    }

    std::string DrawUnreadableBase(const Case& test) {
        try {
            Submit(test, block.Address() + BlockBytes);
        } catch (const std::exception& error) {
            const std::string message = error.what();
            if (message.find("guest memory is not readable") == std::string::npos) return test.what + ": failed with \"" + message + "\", expected the unreadable guest memory failure\n";
            std::cout << test.what << ": " << message << '\n';
            return {};
        }
        return test.what + ": drew although its vertex descriptor's base is unreadable\n";
    }

    void Cost(std::uint32_t repeats) {
        const Case test{"DRAW_INDIRECT, first vertex 3", false, {3, 1, 3, 0, 0}, {0, 0, 0}, 2};
        const auto address = block.Address() + VertexOffset;
        for (const auto records : {Vertices, Records}) {
            DescriptorRecords = records;
            const auto bytes = static_cast<std::size_t>(records) * Stride;
            const auto queryStart = std::chrono::steady_clock::now();
            bool whole = true;
            for (std::uint32_t repeat = 0; repeat < repeats; ++repeat) whole = AgcDriver::GuestMemory::DescribeCommitted(address, bytes).whole && whole;
            const auto queryUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - queryStart).count() / repeats;
            std::string outcome = "drew";
            try {
                Submit(test, address, repeats);
            } catch (const std::exception& error) {
                outcome = error.what();
                drawUs = 0;
            }
            std::printf("cost: %u records (%s), %u draws: %.2f us per Graphics Draw call; DescribeCommitted over the %zu bytes %.3f us per query; %s\n", records, whole ? "wholly readable" : "past the block's end", repeats, drawUs, bytes, queryUs, outcome.c_str());
        }
        DescriptorRecords = Records;
        TitleQuery(repeats);
    }

    static void TitleQuery(std::uint32_t repeats) {
        constexpr std::size_t readable = 0x280000;
        constexpr std::size_t declared = 0x280020;
#ifdef _WIN32
        auto* region = static_cast<std::byte*>(VirtualAlloc(nullptr, readable + TailBytes, MEM_RESERVE, PAGE_NOACCESS));
        Require(region != nullptr && VirtualAlloc(region, readable, MEM_COMMIT, PAGE_READWRITE) != nullptr, "cannot map the title-sized range");
#else
        auto* region = static_cast<std::byte*>(mmap(nullptr, readable + TailBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        Require(region != MAP_FAILED && mprotect(region + readable, TailBytes, PROT_NONE) == 0, "cannot map the title-sized range");
#endif
        {
            GuestAllocations::Mutation mutation;
            mutation.Add(region, readable, true, true, true);
        }
        const auto address = reinterpret_cast<std::uintptr_t>(region);
        std::size_t prefix = 0;
        const auto start = std::chrono::steady_clock::now();
        for (std::uint32_t repeat = 0; repeat < repeats; ++repeat) {
            const auto committed = AgcDriver::GuestMemory::DescribeCommitted(address, declared);
            prefix = !committed.ranges.empty() && committed.ranges.front().first == address ? static_cast<std::size_t>(committed.ranges.front().second - address) : 0;
        }
        const auto queryUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / repeats;
        std::printf("cost: the title's shape, 0x%zx bytes declared and 0x%zx readable: DescribeCommitted %.3f us per query, readable prefix 0x%zx\n", declared, readable, queryUs, prefix);
        {
            GuestAllocations::Mutation mutation;
            mutation.Remove(region);
        }
#ifdef _WIN32
        VirtualFree(region, 0, MEM_RELEASE);
#else
        munmap(region, readable + TailBytes);
#endif
    }

    std::shared_ptr<AgcDriver::VulkanDevice> device;

private:
    double drawUs = 0;
    TailBlock block;
    ShaderRecompiler::ShaderPixelStageInfo pixelInfo{};
};

}

int main(int argc, char** argv) {
    try {
        std::shared_ptr<AgcDriver::VulkanDevice> device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        Fixture fixture(device);
        if (argc > 2 && std::string(argv[1]) == "--cost") {
            fixture.Cost(static_cast<std::uint32_t>(std::stoul(argv[2])));
            return 0;
        }
        std::string failures;
        failures += fixture.Draw({"DRAW_INDIRECT, first vertex 3", false, {3, 1, 3, 0, 0}, {0, 0, 0}, 2});
        failures += fixture.Draw({"DRAW_INDEX_INDIRECT, indices 0 1 2 inside the block", true, {3, 1, 0, 0, 0}, {0, 1, 2}, 1});
        failures += fixture.Draw({"DRAW_INDEX_INDIRECT, indices 0 1 2, base vertex 3", true, {3, 1, 0, 3, 0}, {0, 1, 2}, 2});
        failures += fixture.Draw({"DRAW_INDIRECT, first vertex 6, a second attribute with 0 records, stride 16, OOB select 0", false, {3, 1, 6, 0, 0}, {0, 0, 0}, 3, 0});
        failures += fixture.Draw({"DRAW_INDIRECT, first vertex 6, a second attribute with 0 records, stride 16, OOB select 1", false, {3, 1, 6, 0, 0}, {0, 0, 0}, 3, 1});
        failures += fixture.Draw({"direct draw, first vertex 6, a second attribute with 0 records, stride 16, OOB select 0", false, {3, 1, 6, 0, 0}, {0, 0, 0}, 3, 0, true});
        failures += fixture.Draw({"direct draw, first vertex 6, a second attribute with 0 records, stride 16, OOB select 1", false, {3, 1, 6, 0, 0}, {0, 0, 0}, 3, 1, true});
        failures += fixture.DrawUnreadableBase({"DRAW_INDIRECT whose vertex descriptor starts past the block's end", false, {3, 1, 0, 0, 0}, {0, 0, 0}, 1});
        if (!failures.empty()) {
            std::cerr << failures;
            return 1;
        }
        std::puts("indirect draws whose vertex descriptors run past readable guest memory drew from the readable prefix");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
