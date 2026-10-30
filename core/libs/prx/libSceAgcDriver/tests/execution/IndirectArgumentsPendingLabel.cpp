#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/VulkanDevice.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Draw.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Recorder.hpp"
#include "prx/libc/include/GuestAllocations.hpp"
#include "Recompiler.hpp"
#include "VulkanTestDevice.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

using AgcDriver::Graphics::Require;
using ShaderRecompiler::ShaderStage;
using Path = AgcDriver::Graphics::IndirectDrawPath;
using Rule = AgcDriver::Pm4::DrawParameters::IndirectDraw::Rule;
using Recorder = AgcDriver::Graphics::Recorder;

constexpr std::uint32_t BaseVertexSgpr = 16;
constexpr std::uint32_t StartInstanceSgpr = 17;
constexpr std::uint32_t Vertices = 3;
constexpr std::uint32_t Instances = 2;
constexpr std::uint32_t Slots = Vertices * Instances;
constexpr std::uint32_t Unwritten = 0xdeadbeefu;
constexpr std::uint32_t NoLocation = 0x280u;
constexpr std::uint32_t ConstantBaseVertex = 0x51u;
constexpr std::uint32_t RecordBytes = 20;
constexpr std::uint32_t GraphicsQueue = 0;

alignas(256) constexpr std::array<std::uint32_t, 30> CopyingCode{
    0x7e020305, 0x7e040308,
    0xd70f6a05, 0x00020a10,
    0xd70f6a08, 0x00021011,
    0xf4080506, 0xfa000000,
    0xf4080606, 0xfa000010,
    0x02060b08,
    0xbf8cc07f,
    0xe0002000, 0x80050a03,
    0xe0002000, 0x80060b03,
    0xbf8c3f70,
    0x4a08170a, 0x34080884,
    0x7e180301, 0x7e1a0302, 0x7e1c0305, 0x7e1e0308,
    0xe0781000, 0x80020c04,
    0x7e200280, 0x7e2202f2,
    0xf80008cf, 0x11101010,
    0xbf810000,
};

alignas(256) constexpr std::array<std::uint32_t, 30> PostAddCode{
    0x7e020280, 0x7e040280,
    0xd70f7d05, 0x00020a10,
    0xd70f7d08, 0x00021011,
    0xf4080506, 0xfa000000,
    0xf4080606, 0xfa000010,
    0x02060b08,
    0xbf8cc07f,
    0xe0002000, 0x80050a03,
    0xe0002000, 0x80060b03,
    0xbf8c3f70,
    0x4a08170a, 0x34080884,
    0x7e180301, 0x7e1a0302, 0x7e1c0305, 0x7e1e0308,
    0xe0781000, 0x80020c04,
    0x7e200280, 0x7e2202f2,
    0xf80008cf, 0x11101010,
    0xbf810000,
};

alignas(256) constexpr std::array<std::uint32_t, 5> PixelCode{0x7e0002f2u, 0x7e020280u, 0xf800180fu, 0x00010100u, 0xbf810000u};

constexpr std::uint32_t Width = 64;
constexpr std::uint32_t Height = 32;
constexpr std::size_t TargetBytes = Width * Height * 4;
constexpr std::size_t OutputOffset = 4096;
constexpr std::size_t TargetOffset = 32768;
constexpr std::size_t BlockBytes = 65536;
alignas(256) std::array<std::uint32_t, 2048> VertexSlots{};
alignas(256) std::array<std::uint32_t, 2048> InstanceSlots{};
alignas(256) std::array<std::uint32_t, 8> BufferTable{};
alignas(256) constexpr std::array<std::uint16_t, 4> Indices{2, 0, 1, 0};

constexpr std::array<std::uint32_t, 5> Produced{Vertices, Instances, 0, 700, 40};
constexpr std::array<std::uint32_t, 5> Stale{Vertices, 0, 0, 700, 40};

std::array<std::uint32_t, 4> SlotDescriptor(const void* data, std::uint32_t records) {
    const auto address = reinterpret_cast<std::uintptr_t>(data);
    return {static_cast<std::uint32_t>(address), static_cast<std::uint32_t>((address >> 32u) & 0xffffu) | (4u << 16u), records, 0x01014facu};
}

struct Shape {
    const char* what;
    std::span<const std::uint32_t> code;
    bool inPlace;
    std::uint32_t baseVertex;
    std::uint32_t startInstance;
};

constexpr Shape Constant{"constant offsets", CopyingCode, false, ConstantBaseVertex, 0};
constexpr Shape InPlace{"offsets in place", PostAddCode, true, Produced[3], Produced[4]};

class Fixture {
public:
    explicit Fixture(std::shared_ptr<AgcDriver::VulkanDevice> device) : device(std::move(device)) {
        const auto vertex = SlotDescriptor(VertexSlots.data(), static_cast<std::uint32_t>(VertexSlots.size()));
        const auto instance = SlotDescriptor(InstanceSlots.data(), static_cast<std::uint32_t>(InstanceSlots.size()));
        std::copy(vertex.begin(), vertex.end(), BufferTable.begin());
        std::copy(instance.begin(), instance.end(), BufferTable.begin() + 4);
        block = static_cast<std::byte*>(::operator new(BlockBytes, std::align_val_t{BlockBytes}));
        GuestAllocations::Mutation mutation;
        mutation.Add(block, BlockBytes, true, true, true);
        userData.assign(10, 0u);
        const auto output = reinterpret_cast<std::uintptr_t>(Output().data());
        const std::array<std::uint32_t, 4> descriptor{static_cast<std::uint32_t>(output), static_cast<std::uint32_t>((output >> 32u) & 0xffffu), static_cast<std::uint32_t>(Output().size_bytes()), 0x31016facu};
        std::copy(descriptor.begin(), descriptor.end(), userData.begin());
        const auto table = reinterpret_cast<std::uintptr_t>(BufferTable.data());
        userData[4] = static_cast<std::uint32_t>(table);
        userData[5] = static_cast<std::uint32_t>(table >> 32u);
        userData[BaseVertexSgpr - 8u] = ConstantBaseVertex;
        userData[StartInstanceSgpr - 8u] = 0;
    }

    ShaderRecompiler::RecompileResult Vertex(const Shape& shape) const {
        ShaderRecompiler::ShaderVertexStageInfo info{};
        info.resourcesNum = 2;
        info.resources[0].fields = {BufferTable[0], BufferTable[1], BufferTable[2], BufferTable[3]};
        info.resources[1].fields = {BufferTable[4], BufferTable[5], BufferTable[6], BufferTable[7]};
        info.resourcesDst[0] = {10, 1, 0, 0};
        info.resourcesDst[1] = {11, 1, 1, 1};
        info.fetchAttribReg = 6;
        info.fetchBufferReg = 4;
        info.fetchEmbedded = true;
        const std::array<ShaderRecompiler::MemoryRegion, 2> memory{{
            {reinterpret_cast<std::uintptr_t>(shape.code.data()), std::as_bytes(shape.code)},
            {reinterpret_cast<std::uintptr_t>(BufferTable.data()), std::as_bytes(std::span(BufferTable))}
        }};
        ShaderRecompiler::RecompileRequest request{
            {ShaderStage::Vertex, reinterpret_cast<std::uintptr_t>(shape.code.data()), shape.code, 0, {}},
            {32, 8, userData, std::nullopt, std::nullopt, info, memory},
            device->Target(),
            {0, 0, 0, 64}
        };
        request.useCache = false;
        auto result = ShaderRecompiler::Recompile(request);
        Require(result.vertexOffsetSgpr == static_cast<std::int32_t>(BaseVertexSgpr) && result.instanceOffsetSgpr == static_cast<std::int32_t>(StartInstanceSgpr), "the fetch adds of s16 and s17 were not folded into the draw");
        return result;
    }

    AgcDriver::Pm4::DrawParameters IndirectDraw(const Shape& shape, const ShaderRecompiler::RecompileResult& vertex) const {
        AgcDriver::Pm4::DrawParameters draw{reinterpret_cast<std::uintptr_t>(Indices.data()), static_cast<std::uint32_t>(Indices.size()), 2, 0, 0, true, 0, 0};
        const auto baseVertexLocation = shape.inPlace ? 0x10u : NoLocation;
        const auto startInstanceLocation = shape.inPlace ? 0x11u : NoLocation;
        draw.indirect = AgcDriver::Pm4::DrawParameters::IndirectDraw{Arguments(), 0x25u, RecordBytes, RecordBytes, 1u, false, 0u, baseVertexLocation, startInstanceLocation, NoLocation, false, 0u};
        if (shape.inPlace) {
            draw.indirect->baseVertexSgpr = static_cast<std::int32_t>(BaseVertexSgpr);
            draw.indirect->startInstanceSgpr = static_cast<std::int32_t>(StartInstanceSgpr);
        }
        AgcDriver::DriverDetail::DrawProgram program{};
        program.firstUserSgpr = 8;
        program.userData = userData;
        const auto path = AgcDriver::DriverDetail::Driver::ClassifyIndirectDraw(vertex, State(), program, device, draw, false);
        Require(!path.has_value(), std::string("the indexed indirect draw with ") + shape.what + " was classified to the CPU path (" + (path ? AgcDriver::Graphics::IndirectDrawPathName(*path) : "") + ")");
        const auto rule = shape.inPlace ? Rule::InPlace : Rule::Constant;
        Require(draw.indirect->vertexRule == rule && draw.indirect->instanceRule == rule, std::string("the indexed indirect draw with ") + shape.what + " was given other offset rules");
        return draw;
    }

    AgcDriver::Graphics::State State() const {
        AgcDriver::Graphics::State state{};
        state.stages = {AgcDriver::Graphics::ShaderPath::Vertex, 0u, 32u, 32u, std::nullopt, std::nullopt};
        state.color = {reinterpret_cast<std::uintptr_t>(block + TargetOffset), {Width, Height}, VK_FORMAT_R8G8B8A8_UNORM, TargetBytes, 0xe4u};
        state.colors = {state.color};
        state.hasColorTarget = true;
        state.renderExtent = {Width, Height};
        state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        state.viewport = {0.0f, static_cast<float>(Height), static_cast<float>(Width), -static_cast<float>(Height), 0.0f, 1.0f};
        state.scissor = {{0, 0}, {Width, Height}};
        state.cullMode = VK_CULL_MODE_NONE;
        state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        state.blend.colorWriteMask = 15u;
        state.blends = {state.blend};
        return state;
    }

    void Draw(const ShaderRecompiler::RecompileResult& vertex, const AgcDriver::Pm4::DrawParameters& draw) {
        const auto vertexPush = static_cast<std::uint32_t>(vertex.pushConstants.size());
        ShaderRecompiler::ShaderPixelStageInfo pixelInfo{};
        pixelInfo.wave32 = true;
        pixelInfo.targetOutputMode[0] = 9u;
        pixelInfo.targetExportMapping.fill(0xe4u);
        ShaderRecompiler::RecompileRequest pixelRequest{{ShaderStage::Fragment, reinterpret_cast<std::uintptr_t>(PixelCode.data()), PixelCode, 0, {}}, {32u, 0u, {}, std::nullopt, pixelInfo, std::nullopt, {}}, device->Target(), PixelPushLayout(vertexPush, device->Target())};
        pixelRequest.useCache = false;
        const auto pixel = ShaderRecompiler::Recompile(pixelRequest);
        const std::array<AgcDriver::Graphics::CompiledShader, 2> shaders{{{ShaderStage::Vertex, &vertex, 0u}, {ShaderStage::Fragment, &pixel, PixelPushOffset(vertexPush, device->Target())}}};
        const auto output = Output();
        std::fill(output.begin(), output.end(), Unwritten);
        AgcDriver::GuestMemory::MarkWritten(reinterpret_cast<std::uintptr_t>(output.data()), output.size_bytes());
        device->Draw(State(), draw, shaders);
        recordedIntoBatch = Recorder::Active()->Recording();
        device->WaitIdle();
    }

    void Place(const Shape& shape) const {
        VertexSlots.fill(Unwritten);
        InstanceSlots.fill(Unwritten);
        for (std::uint32_t vertex = 0; vertex < Vertices; ++vertex) VertexSlots[shape.baseVertex + Indices[vertex]] = vertex;
        for (std::uint32_t instance = 0; instance < Instances; ++instance) InstanceSlots[shape.startInstance + instance] = instance * Vertices;
    }

    void StoreRecord(std::span<const std::uint32_t> record) const {
        std::copy(record.begin(), record.end(), reinterpret_cast<std::uint32_t*>(block));
        AgcDriver::GuestMemory::MarkWritten(Arguments(), record.size_bytes());
    }

    std::string Check(const std::string& what, const Shape& shape) const {
        std::string failures;
        if (!recordedIntoBatch) failures += what + ": the draw was not recorded into the open batch\n";
        const auto output = Output();
        for (std::uint32_t instance = 0; instance < Instances; ++instance) {
            for (std::uint32_t vertex = 0; vertex < Vertices; ++vertex) {
                const auto* actual = output.data() + (instance * Vertices + vertex) * 4u;
                const std::array<std::uint32_t, 2> expected{Indices[vertex] + shape.baseVertex, instance + shape.startInstance};
                const bool raw = shape.inPlace || (actual[0] == Indices[vertex] && actual[1] == instance);
                if (raw && actual[2] == expected[0] && actual[3] == expected[1]) continue;
                char line[320];
                std::snprintf(line, sizeof(line), "%s, vertex %u instance %u: wrote %u %u %u %u, the produced record draws vertex %u instance %u\n", what.c_str(), vertex, instance, actual[0], actual[1], actual[2], actual[3], expected[0], expected[1]);
                failures += line;
            }
        }
        return failures;
    }

    std::uint64_t Arguments() const {
        return reinterpret_cast<std::uintptr_t>(block);
    }

    std::span<std::uint32_t> Output() const {
        return {reinterpret_cast<std::uint32_t*>(block + OutputOffset), 4 * Slots};
    }

    std::shared_ptr<AgcDriver::VulkanDevice> device;

private:
    std::byte* block = nullptr;
    std::vector<std::uint32_t> userData;
    bool recordedIntoBatch = false;
};

std::uint64_t CpuPaths(const std::array<std::uint64_t, static_cast<std::size_t>(Path::Count)>& counts) {
    return std::accumulate(counts.begin() + 1, counts.end(), std::uint64_t{0});
}

std::string LabelStoredOnGpu(Fixture& fixture, const Shape& shape) {
    const std::string what = std::string("an indexed indirect draw with ") + shape.what + " whose record a label stores on the GPU earlier in its batch";
    const auto vertex = fixture.Vertex(shape);
    const auto draw = fixture.IndirectDraw(shape, vertex);
    fixture.StoreRecord(Stale);
    fixture.Place(shape);
    std::lock_guard lock(AgcDriver::GuestMemory::GpuMutex());
    auto* recorder = Recorder::Active();
    Require(recorder != nullptr, "no active recorder");
    recorder->Commands();
    Require(fixture.device->WriteLabelOnGpu(fixture.Arguments(), std::as_bytes(std::span(Produced)), 1, GraphicsQueue, false) == 0, "the label over the record was not recorded on the GPU");
    Require(recorder->PendingLabelIn(fixture.Arguments(), RecordBytes) && !recorder->CompletionLabelIn(fixture.Arguments(), RecordBytes), "the label over the record is not pending on the GPU");
    const auto waits = Recorder::ThreadHookWaits();
    const auto before = AgcDriver::Graphics::IndirectDrawCounts();
    fixture.Draw(vertex, draw);
    const auto after = AgcDriver::Graphics::IndirectDrawCounts();
    std::string failures = fixture.Check(what, shape);
    const auto waited = Recorder::ThreadHookWaits() - waits;
    if (waited != 0) failures += what + ": the CPU waited for the GPU " + std::to_string(waited) + " times to read the record, expected 0\n";
    const auto gpu = after[0] - before[0];
    const auto cpu = CpuPaths(after) - CpuPaths(before);
    if (gpu != 1 || cpu != 0) failures += what + ": drawn " + std::to_string(gpu) + " times from its record on the GPU and " + std::to_string(cpu) + " times from a CPU read, expected once on the GPU\n";
    std::cout << what << ": " << (failures.empty() ? "passed" : "failed") << '\n';
    return failures;
}

std::string LabelStoredByCompletion(Fixture& fixture, const Shape& shape) {
    const std::string what = std::string("an indexed indirect draw with ") + shape.what + " whose record counts a label stores by a completion of its batch";
    const auto vertex = fixture.Vertex(shape);
    const auto draw = fixture.IndirectDraw(shape, vertex);
    fixture.StoreRecord(Stale);
    fixture.Place(shape);
    std::lock_guard lock(AgcDriver::GuestMemory::GpuMutex());
    auto* recorder = Recorder::Active();
    Require(recorder != nullptr, "no active recorder");
    recorder->Commands();
    const std::array<std::uint32_t, 1> marker{1};
    recorder->AfterCompletions(fixture.Arguments() + 64, std::as_bytes(std::span(marker)), 2, GraphicsQueue, false);
    Require(Recorder::PendingCompletionStores() != 0, "no completion store is pending before the label");
    const std::array<std::uint32_t, 2> counts{Produced[0], Produced[1]};
    Require(fixture.device->WriteLabelOnGpu(fixture.Arguments(), std::as_bytes(std::span(counts)), 3, GraphicsQueue, false) == 5, "the label over the record's counts was not stored by a completion");
    const auto before = AgcDriver::Graphics::IndirectDrawCounts();
    fixture.Draw(vertex, draw);
    const auto after = AgcDriver::Graphics::IndirectDrawCounts();
    std::string failures = fixture.Check(what, shape);
    const auto label = static_cast<std::size_t>(Path::PendingLabelOrCopy);
    const auto cpu = after[label] - before[label];
    if (cpu != 1 || after[0] != before[0]) failures += what + ": drawn " + std::to_string(after[0] - before[0]) + " times from its record on the GPU and " + std::to_string(cpu) + " times from a CPU read behind the label, expected once from a CPU read\n";
    std::cout << what << ": " << (failures.empty() ? "passed" : "failed") << '\n';
    return failures;
}

}

int main() {
    try {
#ifdef _WIN32
        _putenv_s("APS5_PROFILE_DRAW", "1");
#else
        setenv("APS5_PROFILE_DRAW", "1", 1);
#endif
        std::shared_ptr<AgcDriver::VulkanDevice> device = OpenVulkanTestDevice();
        if (!device) return VulkanTestSkipped;
        Fixture fixture(device);
        std::string failures = LabelStoredOnGpu(fixture, Constant);
        if (device->DrawIndirectSupport().firstInstance) failures += LabelStoredOnGpu(fixture, InPlace);
        else std::cout << "the device draws no indirect first instance: the in-place case is not run\n";
        failures += LabelStoredByCompletion(fixture, Constant);
        if (!failures.empty()) {
            std::cerr << failures;
            return 1;
        }
        std::cout << "indirect arguments pending label tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
