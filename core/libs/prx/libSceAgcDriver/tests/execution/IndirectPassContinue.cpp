#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "prx/libSceAgcDriver/Submit/include/Dcb.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;

constexpr std::byte Kept{0x40};
constexpr std::uint32_t BaseVertexLocation = 0x8c;
constexpr std::uint32_t StartInstanceLocation = 0x8d;
constexpr std::uint32_t TriangleList = 4;
constexpr std::uint32_t Stale = 0x7777u;
constexpr std::size_t BlockBytes = 65536;
constexpr std::uint32_t Drawn = 3;

alignas(256) constexpr std::array<std::uint32_t, 20> DepthCode{
    0x7e000d05u, 0x080200f4u, 0x10020300u, 0x100202f4u, 0x060202f3u, 0x060400f3u, 0x10040500u, 0x060404f3u,
    0x7e060208u, 0x7e0802f2u, 0x7e0a02ffu, 0x44800000u, 0xf80000d1u, 0x00000005u, 0xf80008cfu, 0x04030201u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u,
};
alignas(256) constexpr std::array<std::uint32_t, 8> PixelCode{0x7e000200u, 0x7e020280u, 0xf800180fu, 0x00010100u, 0xbf810000u, 0xbf810000u, 0xbf810000u, 0xbf810000u};
constexpr std::array<std::uint32_t, 9> FillKernel{0xd7460004u, 0x04010c08u, 0x7e000204u, 0x7e020205u, 0x7e040206u, 0x7e060207u, 0xe01c2000u, 0x80000004u, 0xbf810000u};

constexpr std::uint32_t TiledAttrib3 = 0x09000000u | (0x1au << 14u);

struct Header {
    Shader shader{};
    ShaderUserData userData{};
};

alignas(8) Header DepthHeader{};
alignas(8) Header PixelHeader{};

void Registered(Header& header, const std::uint32_t* code, std::size_t bytes, std::uint8_t type) {
    header.shader.file_header = 0x34333231u;
    header.shader.version = 0x18u;
    header.shader.header_size = sizeof(Header);
    header.shader.shader_size = static_cast<std::uint32_t>(bytes);
    header.shader.code = code;
    header.shader.type = type;
    header.shader.user_data = &header.userData;
    AgcDriverRegisterShader_nid_postfix(&header.shader);
}

void Append(std::vector<std::uint32_t>& commands, std::uint32_t opcode, std::initializer_list<std::uint32_t> payload) {
    commands.push_back(0xc0000000u | (static_cast<std::uint32_t>(payload.size() - 1) << 16u) | (opcode << 8u));
    commands.insert(commands.end(), payload);
}

class GuestBlock {
public:
    GuestBlock() : data(static_cast<std::byte*>(std::aligned_alloc(BlockBytes, BlockBytes))) {
        Require(data != nullptr, "cannot allocate a guest block");
        std::memset(data, 0, BlockBytes);
        GuestAllocations::Mutation mutation;
        mutation.Add(data, BlockBytes, true, true, true);
    }
    ~GuestBlock() {
        AgcDriver::Graphics::StorageTexture::FlushPending(Address(), BlockBytes, nullptr, "test release");
        {
            GuestAllocations::Mutation mutation;
            mutation.Remove(data);
        }
        std::free(data);
    }
    GuestBlock(const GuestBlock&) = delete;
    GuestBlock& operator=(const GuestBlock&) = delete;
    std::uintptr_t Address() const { return reinterpret_cast<std::uintptr_t>(data); }
    std::byte* Data() const { return data; }
    std::uint32_t* Words() const { return reinterpret_cast<std::uint32_t*>(data); }

private:
    std::byte* data;
};

void Fill(std::vector<std::uint32_t>& commands, const GuestBlock& code, std::uintptr_t base, std::uint32_t value) {
    const auto kernel = code.Address();
    Append(commands, 0x76, {0x20c, static_cast<std::uint32_t>(kernel >> 8u)});
    Append(commands, 0x76, {0x20d, static_cast<std::uint32_t>(kernel >> 40u) & 0xffu});
    Append(commands, 0x76, {0x207, 64});
    Append(commands, 0x76, {0x208, 1});
    Append(commands, 0x76, {0x209, 1});
    Append(commands, 0x76, {0x213, (8u << 1u) | 0x80u});
    Append(commands, 0x76, {0x240, static_cast<std::uint32_t>(base)});
    Append(commands, 0x76, {0x241, (static_cast<std::uint32_t>(base >> 32u) & 0xffffu) | (16u << 16u)});
    Append(commands, 0x76, {0x242, 1024u / 16u});
    Append(commands, 0x76, {0x243, 0x4bfacu});
    for (std::uint32_t word = 0; word < 4; ++word) Append(commands, 0x76, {0x244 + word, value});
    Append(commands, 0x15, {1, 1, 1, 0x41});
}

void Store(std::vector<std::uint32_t>& commands, std::uintptr_t address, std::uint32_t value) {
    commands.insert(commands.end(), {0xc0063700u, 0x00100200u, static_cast<std::uint32_t>(address), static_cast<std::uint32_t>(address >> 32u), value, value, value, value});
}

void DrawState(std::vector<std::uint32_t>& commands, std::uintptr_t records, const GuestBlock& target) {
    const auto vertex = reinterpret_cast<std::uintptr_t>(DepthCode.data());
    const auto pixel = reinterpret_cast<std::uintptr_t>(PixelCode.data());
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> context{
        {0x2d5, 0x2000},
        {0x1b6, 0}, {0x207, 0x01200000}, {0x200, 0}, {0x203, 0x800},
        {0x2dc, 0xaa00}, {0x2f8, 0}, {0x292, 2}, {0x293, 0},
        {0x80, 0}, {0x8d, 0}, {0x83, 0xffff}, {0x8c, 0xa},
        {0x2f9, 0x2d}, {0x313, 0x6000}, {0x30e, 0xffffffff}, {0x30f, 0xffffffff},
        {0x206, 0x43f}, {0x204, 0x80000}, {0x205, 0x240},
        {0x8e, 0xf}, {0x8f, 0xf}, {0x202, 0xcc0010},
        {0x1c4, 0}, {0x1c5, 9}, {0x1c3, 4}, {0x31c, 0x28028},
        {0x31b, 0}, {0x31d, 0}, {0x3b0, (63u << 14u) | 3u},
        {0x3b8, 0x9000000}, {0x1e0, 0},
        {0xc, 0}, {0xd, 0x40040},
        {0x81, 0x80000000}, {0x82, 0x40040},
        {0x90, 0x80000000}, {0x91, 0x40040},
        {0x94, 0x80000000}, {0x95, 0x40040},
        {0x10f, std::bit_cast<std::uint32_t>(32.0f)}, {0x110, std::bit_cast<std::uint32_t>(32.0f)},
        {0x111, std::bit_cast<std::uint32_t>(-2.0f)}, {0x112, std::bit_cast<std::uint32_t>(2.0f)},
        {0x113, std::bit_cast<std::uint32_t>(1.0f)}, {0x114, 0},
        {0xb4, 0}, {0xb5, std::bit_cast<std::uint32_t>(1.0f)},
        {0x2fe, 0}, {0x302, 0}, {0x306, 0}, {0x30a, 0},
    };
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> shader{
        {0xc8, static_cast<std::uint32_t>(vertex >> 8u)}, {0xc9, static_cast<std::uint32_t>(vertex >> 40u)}, {0x8b, 2u << 1u},
        {0x008, static_cast<std::uint32_t>(pixel >> 8u)}, {0x009, static_cast<std::uint32_t>(pixel >> 40u)}, {0x00b, 1u << 1u}, {0x00c, std::bit_cast<std::uint32_t>(1.0f)},
    };
    Append(commands, 0x79, {0x242, TriangleList});
    Append(commands, 0x79, {0x24a, 0});
    for (const auto& [offset, value] : context) Append(commands, 0x69, {offset, value});
    for (const auto& [offset, value] : shader) Append(commands, 0x76, {offset, value});
    for (std::uint32_t sgpr = 0; sgpr < 2; ++sgpr) Append(commands, 0x76, {BaseVertexLocation + sgpr, Stale});
    Append(commands, 0x11, {1, static_cast<std::uint32_t>(records), static_cast<std::uint32_t>(records >> 32u)});
    Append(commands, 0x69, {0x318, static_cast<std::uint32_t>(target.Address() >> 8u)});
    Append(commands, 0x69, {0x390, static_cast<std::uint32_t>(target.Address() >> 40u)});
    Append(commands, 0x69, {0x3b8, TiledAttrib3});
}

void Draw(std::vector<std::uint32_t>& commands) {
    Append(commands, 0x2c, {0, BaseVertexLocation, StartInstanceLocation, 0, 1, 0, 0, 16, 2});
}

void Submit(std::vector<std::uint32_t> commands) {
    Packet packet{commands.data(), static_cast<std::uint32_t>(commands.size()), 0, {}};
    Require(sceAgcDriverSubmitDcb(&packet) == 0, "the draw submission failed");
    AgcDriverWaitIdle_nid_postfix();
}

void Clear(const GuestBlock& target) {
    AgcDriver::GuestMemory::FlushGpuWrites(target.Address(), BlockBytes);
    AgcDriver::Graphics::StorageTexture::FlushPending(target.Address(), BlockBytes, nullptr, "test clear");
    std::memset(target.Data(), static_cast<int>(Kept), BlockBytes);
    AgcDriver::GuestMemory::MarkWritten(target.Address(), BlockBytes);
}

std::size_t Covered(const GuestBlock& target) {
    AgcDriver::GuestMemory::FlushGpuWrites(target.Address(), BlockBytes);
    return static_cast<std::size_t>(std::count_if(target.Data(), target.Data() + BlockBytes, [](std::byte value) { return value != Kept; }));
}

struct Passes {
    std::uint64_t begun;
    std::uint64_t continued;
    std::uint64_t barriers;
    std::uint64_t skipped;
};

Passes Since(const AgcDriver::Graphics::IndirectPassStats& before) {
    const auto after = AgcDriver::Graphics::IndirectPassCounters();
    return {after.passesBegun - before.passesBegun, after.passesContinued - before.passesContinued, after.barriersRecorded - before.barriersRecorded, after.barriersSkipped - before.barriersSkipped};
}

std::string Text(const Passes& passes) {
    return std::to_string(passes.begun) + " passes begun, " + std::to_string(passes.continued) + " continued, " + std::to_string(passes.barriers) + " argument barriers recorded, " + std::to_string(passes.skipped) + " skipped";
}

void RequirePasses(const Passes& passes, const Passes& expected, const std::string& what) {
    Require(passes.begun == expected.begun && passes.continued == expected.continued && passes.barriers == expected.barriers && passes.skipped == expected.skipped, what + ": " + Text(passes) + ", expected " + Text(expected));
}

bool FillsLand(const GuestBlock& code, const GuestBlock& records) {
    std::vector<std::uint32_t> commands;
    Fill(commands, code, records.Address(), 0x5a5a5a5au);
    Submit(std::move(commands));
    std::array<std::uint32_t, 4> stored{};
    AgcDriver::GuestMemory::Read(records.Address(), std::as_writable_bytes(std::span(stored)), 4);
    return stored == std::array<std::uint32_t, 4>{0x5a5a5a5au, 0x5a5a5a5au, 0x5a5a5a5au, 0x5a5a5a5au};
}

void RecordsWrittenBeforeThePass(const GuestBlock& code, const GuestBlock& records, const GuestBlock& target) {
    Clear(target);
    std::vector<std::uint32_t> commands;
    Fill(commands, code, records.Address(), Drawn);
    DrawState(commands, records.Address(), target);
    Draw(commands);
    Draw(commands);
    const auto before = AgcDriver::Graphics::IndirectPassCounters();
    Submit(std::move(commands));
    RequirePasses(Since(before), {1, 1, 1, 1}, "two GPU-side indirect draws of records a dispatch wrote before them");
    Require(Covered(target) != 0, "two GPU-side indirect draws of records a dispatch wrote before them drew nothing");
    std::cout << "records a dispatch wrote before the pass: one argument barrier, one pass for both draws\n";
}

void RecordsRewrittenBetweenDraws(const GuestBlock& code, const GuestBlock& records, const GuestBlock& target, bool dispatch) {
    const std::string what = dispatch ? "records a dispatch rewrote between two draws" : "records a WRITE_DATA (vkCmdUpdateBuffer) rewrote between two draws";
    Clear(target);
    std::vector<std::uint32_t> commands;
    Fill(commands, code, records.Address(), 0);
    DrawState(commands, records.Address(), target);
    Draw(commands);
    if (dispatch) Fill(commands, code, records.Address(), Drawn);
    else Store(commands, records.Address(), Drawn);
    Draw(commands);
    const auto before = AgcDriver::Graphics::IndirectPassCounters();
    Submit(std::move(commands));
    const auto passes = Since(before);
    Require(Covered(target) != 0, what + ": the second draw did not draw its new records");
    if (!dispatch && passes.begun + passes.continued == 1) {
        RequirePasses(passes, {1, 0, 1, 0}, what);
        std::cout << what << ": the second draw read its records on the CPU (a label store is pending over them)\n";
        return;
    }
    RequirePasses(passes, {2, 0, 2, 0}, what);
    std::cout << what << ": the pass broke and the barrier was recorded\n";
}

void RecordsWrittenByTheHostBetweenBatches(const GuestBlock& records, const GuestBlock& target) {
    const std::string what = "records the host rewrote between two batches";
    Clear(target);
    AgcDriver::GuestMemory::FlushGpuWrites(records.Address(), BlockBytes);
    for (std::uint32_t word = 0; word < 4; ++word) records.Words()[word] = 0;
    std::vector<std::uint32_t> first;
    DrawState(first, records.Address(), target);
    Draw(first);
    const auto before = AgcDriver::Graphics::IndirectPassCounters();
    Submit(std::move(first));
    for (std::uint32_t word = 0; word < 4; ++word) records.Words()[word] = Drawn;
    std::vector<std::uint32_t> second;
    DrawState(second, records.Address(), target);
    Draw(second);
    Submit(std::move(second));
    RequirePasses(Since(before), {2, 0, 2, 0}, what);
    Require(Covered(target) != 0, what + ": the second batch's draw did not draw the host's records");
    std::cout << what << ": the second batch recorded its own barrier\n";
}

}

int main() {
    try {
        {
            const auto device = OpenVulkanTestDevice();
            if (!device) return VulkanTestSkipped;
        }
        unsetenv("APS5_NO_INDIRECT_PASS_CONTINUE");
        Registered(DepthHeader, DepthCode.data(), sizeof(DepthCode), 2);
        Registered(PixelHeader, PixelCode.data(), sizeof(PixelCode), 1);
        const GuestBlock code;
        std::memcpy(code.Data(), FillKernel.data(), sizeof(FillKernel));
        const GuestBlock records;
        if (!FillsLand(code, records)) {
            std::cout << "skipped, the device does not store compute fills into guest memory\n";
            AgcDriverShutdown_nid_postfix();
            return 0;
        }
        const GuestBlock target;
        RecordsWrittenBeforeThePass(code, records, target);
        RecordsRewrittenBetweenDraws(code, records, target, true);
        RecordsRewrittenBetweenDraws(code, records, target, false);
        RecordsWrittenByTheHostBetweenBatches(records, target);
        AgcDriverShutdown_nid_postfix();
        std::puts("GPU-side indirect draws continue the open pass only while nothing wrote their records");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        AgcDriverShutdown_nid_postfix();
        return 1;
    }
}
