#include "BdaShader.hpp"
#include "BdaAbi.hpp"
#include "IntermediateRepresentation/IrMetadata/ShaderStage.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Resources.hpp"
#include "prx/libSceAgcDriver/Graphics/include/BdaPageTable.hpp"
#include "BdaPageReference.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <random>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using namespace AgcDriver::Graphics;

class Pipeline {
public:
    Pipeline(const Context& context, std::span<const std::uint32_t> code, const std::array<Buffer*, 3>& buffers) : context(context) {
        try {
            std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
            for (std::uint32_t i = 0; i < bindings.size(); ++i) bindings[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
            VkDescriptorSetLayoutCreateInfo setInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            setInfo.bindingCount = bindings.size();
            setInfo.pBindings = bindings.data();
            Check(context.Function<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(context.device, &setInfo, nullptr, &setLayout), "vkCreateDescriptorSetLayout");
            const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3};
            VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            poolInfo.maxSets = 1;
            poolInfo.poolSizeCount = 1;
            poolInfo.pPoolSizes = &size;
            Check(context.Function<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(context.device, &poolInfo, nullptr, &pool), "vkCreateDescriptorPool");
            VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, pool, 1, &setLayout};
            Check(context.Function<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(context.device, &allocation, &set), "vkAllocateDescriptorSets");
            for (std::uint32_t i = 0; i < buffers.size(); ++i) {
                const VkDescriptorBufferInfo buffer{buffers[i]->Handle(), 0, buffers[i]->Bytes().size()};
                VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                write.dstSet = set;
                write.dstBinding = i;
                write.descriptorCount = 1;
                write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                write.pBufferInfo = &buffer;
                context.Function<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(context.device, 1, &write, 0, nullptr);
            }
            VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layoutInfo.setLayoutCount = 1;
            layoutInfo.pSetLayouts = &setLayout;
            Check(context.Function<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(context.device, &layoutInfo, nullptr, &layout), "vkCreatePipelineLayout");
            VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            moduleInfo.codeSize = code.size_bytes();
            moduleInfo.pCode = code.data();
            Check(context.Function<PFN_vkCreateShaderModule>("vkCreateShaderModule")(context.device, &moduleInfo, nullptr, &module), "vkCreateShaderModule");
            VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            pipelineInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module, "main", nullptr};
            pipelineInfo.layout = layout;
            Check(context.Function<PFN_vkCreateComputePipelines>("vkCreateComputePipelines")(context.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline), "vkCreateComputePipelines");
        } catch (...) { release(); throw; }
    }

    ~Pipeline() { release(); }

    void Run(std::uint32_t groups) {
        CommandBatch batch(context);
        const auto commands = batch.Handle();
        VkMemoryBarrier upload{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
        context.Function<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(commands, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &upload, 0, nullptr, 0, nullptr);
        context.Function<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        context.Function<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(commands, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
        context.Function<PFN_vkCmdDispatch>("vkCmdDispatch")(commands, groups, 1, 1);
        VkMemoryBarrier download{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};
        context.Function<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &download, 0, nullptr, 0, nullptr);
        batch.SubmitAndWait();
    }

private:
    void release() noexcept {
        if (pipeline) context.Function<PFN_vkDestroyPipeline>("vkDestroyPipeline")(context.device, pipeline, nullptr);
        if (module) context.Function<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(context.device, module, nullptr);
        if (layout) context.Function<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(context.device, layout, nullptr);
        if (pool) context.Function<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(context.device, pool, nullptr);
        if (setLayout) context.Function<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(context.device, setLayout, nullptr);
    }

    const Context& context;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkShaderModule module = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
};

}

static void RunWrittenRangeDecodeTests() {
    namespace Abi = ShaderRecompiler::BdaAbi;
    std::vector<std::uint32_t> words(Abi::FaultBufferBytes / sizeof(std::uint32_t));
    std::vector<std::pair<std::uint64_t, std::size_t>> marked;
    const auto decode = [&] {
        marked.clear();
        return Abi::ForEachWrittenRange(words.data(), [&](std::uint64_t address, std::size_t bytes) { marked.emplace_back(address, bytes); });
    };
    const auto claim = [&](std::uint32_t index, std::uint32_t slot, std::uint64_t address, std::uint32_t mask) {
        words[Abi::WrittenListWord + index] = slot;
        words[Abi::WrittenSlotsWord + 2 * slot] = Abi::WrittenChunkKey(address);
        words[Abi::WrittenSlotsWord + 2 * slot + 1] = mask;
        words[Abi::WrittenCountWord] = std::max(words[Abi::WrittenCountWord], index + 1);
    };
    constexpr std::uint64_t page = std::uint64_t{1} << Abi::WrittenPageShift;
    Require(decode() && marked.empty(), "an empty written-page record marked pages");
    constexpr std::uint64_t top = 0x7ffffffe0000ULL;
    claim(0, 5, top, 0b1011u);
    Require(decode() && marked == std::vector<std::pair<std::uint64_t, std::size_t>>{{top, 2 * page}, {top + 3 * page, page}}, "written pages near the top of the address space were not decoded as written");
    std::fill(words.begin(), words.end(), 0u);
    constexpr std::uint64_t high = 0x200000001000ULL;
    claim(0, Abi::WrittenPageSlots - 1, high, 1u << ((high >> Abi::WrittenPageShift) & (Abi::WrittenChunkPages - 1u)));
    Require(decode() && marked == std::vector<std::pair<std::uint64_t, std::size_t>>{{high, page}}, "a written page above 16 TiB was decoded at another address");
    std::fill(words.begin(), words.end(), 0u);
    constexpr std::uint64_t chunk = 0x3e0000000ULL;
    claim(0, 7, chunk, 0xffffffffu);
    claim(1, 8, chunk + (std::uint64_t{1} << Abi::WrittenChunkShift), 1u);
    claim(2, 9, chunk - (std::uint64_t{1} << Abi::WrittenChunkShift), 0x80000000u);
    Require(decode() && marked == std::vector<std::pair<std::uint64_t, std::size_t>>{{chunk, 32 * page}, {chunk + 32 * page, page}, {chunk - page, page}}, "whole, first-page and last-page chunks were not decoded exactly");
    words[Abi::WrittenListWord + 1] = Abi::WrittenPageSlots;
    Require(!decode(), "a claim outside the slots was accepted");
    std::fill(words.begin(), words.end(), 0u);
    words[Abi::WrittenCountWord] = 1;
    Require(!decode(), "a claim of an empty slot was accepted");
    words[Abi::WrittenCountWord] = Abi::WrittenPageSlots + 1;
    Require(!decode(), "more claims than slots were accepted");
}

static std::shared_ptr<const BdaPageTable> PagesFor(const Context& context, bool paged, std::span<const ShaderRecompiler::BdaAbi::Range> ranges) {
    return paged ? BdaPageTable::Build(context, ranges) : nullptr;
}

static std::uint64_t PageTableAddress(const std::shared_ptr<const BdaPageTable>& pages) {
    return pages != nullptr ? pages->Address() : 0u;
}

static std::unique_ptr<Buffer> MakeBinding(const Context& context, std::span<const ShaderRecompiler::BdaAbi::Range> own, std::span<const ShaderRecompiler::BdaAbi::Grant> grants, std::uint64_t pageTable, std::uint32_t extraCount = 0) {
    namespace Abi = ShaderRecompiler::BdaAbi;
    auto binding = std::make_unique<Buffer>(context, sizeof(Abi::Header) + own.size_bytes() + grants.size_bytes(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    const Abi::Header header{Abi::TableVersion, static_cast<std::uint32_t>(own.size()) + extraCount, static_cast<std::uint32_t>(grants.size()), 0, pageTable, 0};
    std::memcpy(binding->Bytes().data(), &header, sizeof(header));
    if (!own.empty()) std::memcpy(binding->Bytes().data() + sizeof(header), own.data(), own.size_bytes());
    if (!grants.empty()) std::memcpy(binding->Bytes().data() + sizeof(header) + own.size_bytes(), grants.data(), grants.size_bytes());
    return binding;
}

static std::unique_ptr<Buffer> MakeBinding(const Context& context, bool paged, const std::shared_ptr<const BdaPageTable>& pages, std::span<const ShaderRecompiler::BdaAbi::Range> ranges, std::span<const ShaderRecompiler::BdaAbi::Grant> grants = {}, std::uint32_t extraCount = 0) {
    return MakeBinding(context, paged ? std::span<const ShaderRecompiler::BdaAbi::Range>{} : ranges, grants, PageTableAddress(pages), extraCount);
}

static void RunBdaDwordWriteTests(const Context& context, bool paged) {
    RunWrittenRangeDecodeTests();
    namespace Abi = ShaderRecompiler::BdaAbi;
    constexpr std::uint64_t guest = 0x7fff12340f80ULL;
    constexpr std::size_t firstBytes = 160;
    Buffer first(context, firstBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    Buffer second(context, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    Buffer readOnly(context, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    const std::array<Abi::Range, 3> ranges{{
        {guest, guest + firstBytes, first.DeviceAddress(), Abi::Read | Abi::Write, 0},
        {guest + firstBytes, guest + firstBytes + 16, second.DeviceAddress(), Abi::Read | Abi::Write, 0},
        {guest + firstBytes + 16, guest + firstBytes + 32, readOnly.DeviceAddress(), Abi::Read, 0},
    }};
    Buffer fault(context, Abi::FaultBufferBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Buffer output(context, 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    const std::array<std::uint32_t, 4> values{0x11223344u, 0x55667788u, 0x99aabbccu, 0xddeeff00u};
    const auto bytesAt = [&](std::uint64_t address) -> std::byte* {
        for (std::size_t index = 0; index < ranges.size(); ++index) {
            if (address < ranges[index].begin || address >= ranges[index].end) continue;
            auto& buffer = index == 0 ? first : index == 1 ? second : readOnly;
            return buffer.Bytes().data() + (address - ranges[index].begin);
        }
        return nullptr;
    };
    const auto notedBytes = [&](std::uint64_t first, std::uint64_t last) {
        std::uint64_t covered = 0, total = 0;
        const auto* faultWords = reinterpret_cast<const std::uint32_t*>(fault.Bytes().data());
        Require(Abi::ForEachWrittenRange(faultWords, [&](std::uint64_t begin, std::size_t bytes) {
            total += bytes;
            for (const auto address : {first, last}) covered += address >= begin && address < begin + bytes ? 1u : 0u;
        }), "a BDA dword write left an invalid written-page record");
        return std::pair{covered, total};
    };
    const auto pages = PagesFor(context, paged, ranges);
    const auto write = [&](std::uint64_t address, std::uint32_t dwords, std::uint32_t storedBytes, Abi::FaultReason reason, std::uint32_t groups = 1, std::vector<Abi::Grant> grants = {}) {
        const auto table = MakeBinding(context, paged, pages, ranges, grants);
        std::memset(fault.Bytes().data(), 0, fault.Bytes().size());
        for (auto* buffer : {&first, &second, &readOnly}) std::memset(buffer->Bytes().data(), 0xa5, buffer->Bytes().size());
        Pipeline pipeline(context, MakeBdaDwordWriteTestShader(address, dwords, values.data()), {table.get(), &fault, &output});
        pipeline.Run(groups);
        Abi::Fault report{};
        std::memcpy(&report, fault.Bytes().data(), sizeof(report));
        for (std::uint32_t byte = 0; byte < dwords * 4u; ++byte) {
            const auto* stored = bytesAt(address + byte);
            const auto expected = byte < storedBytes ? static_cast<std::byte>(values[byte / 4u] >> ((byte % 4u) * 8u)) : std::byte{0xa5};
            Require(stored != nullptr && *stored == expected, "BDA dword write stored incorrect bytes at offset " + std::to_string(byte) + " of guest + " + std::to_string(address - guest) + (paged ? " (paged, " : " (") + std::to_string(grants.size()) + " write grants, fault reason " + std::to_string(static_cast<std::uint32_t>(report.reason)) + " at guest + " + std::to_string(report.address - guest) + " bytes " + std::to_string(report.bytes) + ")");
        }
        Require(*bytesAt(address - 1) == std::byte{0xa5}, "BDA dword write touched the byte before it");
        if (address + dwords * 4u < guest + firstBytes + 32) Require(*bytesAt(address + dwords * 4u) == std::byte{0xa5}, "BDA dword write touched the byte after it");
        if (static_cast<std::uint32_t>(reason) == 0) {
            Require(report.state == Abi::FaultState::Empty, "a mapped BDA dword write published a fault");
        } else {
            Require(report.state == Abi::FaultState::Ready && report.reason == reason && report.instruction == 0x1234, "a BDA dword write did not publish its fault");
        }
        const auto [covered, total] = notedBytes(address, address + (storedBytes != 0 ? storedBytes - 1u : 0u));
        if (storedBytes == 0) {
            Require(total == 0, "a BDA dword write that stored nothing noted pages");
        } else {
            const auto pages = ((address + storedBytes - 1u) >> Abi::WrittenPageShift) - (address >> Abi::WrittenPageShift) + 1u;
            Require(covered == 2 && total == pages << Abi::WrittenPageShift, "a BDA dword write did not note exactly its written pages");
            const auto chunks = ((address + storedBytes - 1u) >> Abi::WrittenChunkShift) - (address >> Abi::WrittenChunkShift) + 1u;
            std::uint32_t claims = 0;
            std::memcpy(&claims, fault.Bytes().data() + Abi::WrittenCountWord * sizeof(std::uint32_t), sizeof(claims));
            Require(claims == chunks, "a BDA dword write claimed a written-page slot more than once per chunk");
        }
    };
    write(guest + 16, 4, 16, static_cast<Abi::FaultReason>(0));
    write(guest + 20, 4, 16, static_cast<Abi::FaultReason>(0));
    write(guest + 8, 2, 8, static_cast<Abi::FaultReason>(0));
    write(guest + 12, 2, 8, static_cast<Abi::FaultReason>(0));
    write(guest + 32, 3, 12, static_cast<Abi::FaultReason>(0));
    write(guest + 0x78, 4, 16, static_cast<Abi::FaultReason>(0));
    write(guest + 0x7c, 2, 8, static_cast<Abi::FaultReason>(0));
    write(guest + 41, 4, 16, static_cast<Abi::FaultReason>(0));
    write(guest + firstBytes - 8, 4, 16, static_cast<Abi::FaultReason>(0));
    write(guest + firstBytes + 12, 4, 4, Abi::FaultReason::Permission);
    write(guest + 0x7c, 2, 8, static_cast<Abi::FaultReason>(0), 64);
    constexpr auto readOnlyBegin = guest + firstBytes + 16;
    write(guest + firstBytes + 12, 4, 16, static_cast<Abi::FaultReason>(0), 1, {{readOnlyBegin, readOnlyBegin + 16}});
    write(guest + firstBytes + 12, 4, 8, Abi::FaultReason::Permission, 1, {{readOnlyBegin, readOnlyBegin + 4}});
    write(guest + firstBytes + 12, 2, 4, Abi::FaultReason::Permission, 1, {{readOnlyBegin + 1, readOnlyBegin + 16}});
    write(readOnlyBegin + 4, 2, 8, static_cast<Abi::FaultReason>(0), 1, {{guest, guest + 8}, {readOnlyBegin + 4, readOnlyBegin + 12}});
    write(readOnlyBegin + 4, 2, 4, Abi::FaultReason::Permission, 1, {{guest, guest + 8}, {readOnlyBegin + 4, readOnlyBegin + 11}});
}

static void RunBdaReadTests(const Context& context, bool paged) {
    namespace Abi = ShaderRecompiler::BdaAbi;
    Buffer first(context, 3, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    Buffer second(context, 2, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    first.Bytes()[0] = std::byte{0x11};
    first.Bytes()[1] = std::byte{0x22};
    first.Bytes()[2] = std::byte{0x33};
    second.Bytes()[0] = std::byte{0x44};
    second.Bytes()[1] = std::byte{0x55};
    constexpr std::uint64_t guest = 0x7fff12340001ULL;
    std::array<Abi::Range, 2> ranges{{{guest, guest + 3, first.DeviceAddress(), Abi::Read, 0}, {guest + 3, guest + 5, second.DeviceAddress(), Abi::Read, 0}}};
    Buffer fault(context, sizeof(Abi::Fault), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Buffer output(context, 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    const auto run = [&](std::uint64_t address, std::uint32_t bits, std::uint32_t expected, Abi::FaultReason reason, std::uint32_t extraCount = 0, std::uint32_t groups = 1, std::int64_t offset = 0) {
        const auto pages = PagesFor(context, paged, ranges);
        const auto table = MakeBinding(context, paged, pages, ranges, {}, extraCount);
        std::memset(fault.Bytes().data(), 0, fault.Bytes().size());
        const std::uint32_t sentinel = 0xdeadbeef;
        std::memcpy(output.Bytes().data(), &sentinel, sizeof(sentinel));
        Pipeline pipeline(context, MakeBdaTestShader(address, bits, offset), {table.get(), &fault, &output});
        pipeline.Run(groups);
        Abi::Fault report{};
        std::uint32_t result = 0;
        std::memcpy(&report, fault.Bytes().data(), sizeof(report));
        std::memcpy(&result, output.Bytes().data(), sizeof(result));
        if (static_cast<std::uint32_t>(reason) == 0) {
            Require(report.state == Abi::FaultState::Empty && result == expected, "BDA GPU read produced incorrect data or a fault");
        } else {
            Require(report.state == Abi::FaultState::Ready && report.reason == reason && report.instruction == 0x1234, "BDA GPU fault was not published correctly");
            Require(result == sentinel, "faulting BDA shader continued to output a substitute value");
        }
    };
    run(guest, 8, 0x11, static_cast<Abi::FaultReason>(0));
    run(guest + 1, 16, 0x3322, static_cast<Abi::FaultReason>(0));
    run(guest + 1, 32, 0x55443322, static_cast<Abi::FaultReason>(0));
    run(guest + 5, 8, 0, Abi::FaultReason::Unmapped, 0, 64);
    run(guest - 1, 8, 0, Abi::FaultReason::Unmapped);
    run(std::numeric_limits<std::uint64_t>::max() - 1, 32, 0, Abi::FaultReason::Overflow);
    run(guest, 8, 0, Abi::FaultReason::InvalidTable, 1);
    run(std::numeric_limits<std::uint64_t>::max() - 2, 8, 0, Abi::FaultReason::Overflow, 0, 1, 4);
    run(1, 8, 0, Abi::FaultReason::Overflow, 0, 1, -4);
    Buffer words(context, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    const auto read = [&](std::uint64_t address, std::uint32_t dwords, bool coherent, bool stops, std::uint32_t value, bool faults) {
        const auto pages = PagesFor(context, paged, ranges);
        const auto table = MakeBinding(context, paged, pages, ranges);
        std::memset(fault.Bytes().data(), 0, fault.Bytes().size());
        const std::array<std::uint32_t, 4> sentinel{0xdeadbeef, 0xdeadbeef, 0xdeadbeef, 0xdeadbeef};
        std::memcpy(words.Bytes().data(), sentinel.data(), sizeof(sentinel));
        Pipeline pipeline(context, MakeBdaDwordReadTestShader(address, dwords, coherent, stops), {table.get(), &fault, &words});
        pipeline.Run(1);
        Abi::Fault report{};
        std::array<std::uint32_t, 4> result{};
        std::memcpy(&report, fault.Bytes().data(), sizeof(report));
        std::memcpy(result.data(), words.Bytes().data(), sizeof(result));
        auto expected = sentinel;
        if (!faults || !stops) {
            expected[0] = value;
            for (std::uint32_t dword = 1; dword < dwords; ++dword) expected[dword] = 0;
        }
        if (faults) {
            Require(report.state == Abi::FaultState::Ready && report.reason == Abi::FaultReason::Unmapped && report.address == guest + 5 && report.bytes == 1 && report.instruction == 0x1234, "BDA dword read did not publish its first fault");
        } else {
            Require(report.state == Abi::FaultState::Empty, "mapped BDA dword read published a fault");
        }
        Require(result == expected, faults && stops ? "faulting BDA dword read continued to output a substitute value" : "BDA dword read produced incorrect data");
    };
    for (const bool stops : {true, false}) {
        for (const bool coherent : {false, true}) {
            read(guest + 1, 1, coherent, stops, 0x55443322, false);
            read(guest + 3, 1, coherent, stops, 0x5544, true);
            read(guest + 1, 4, coherent, stops, 0x55443322, true);
        }
    }
    Buffer wide(context, 32, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    for (std::uint32_t dword = 0; dword < 8u; ++dword) {
        const std::uint32_t value = 0xa0b0c000u + dword;
        std::memcpy(wide.Bytes().data() + dword * 4u, &value, sizeof(value));
    }
    constexpr std::uint64_t spanGuest = 0x7fff56780000ULL;
    const std::array<Abi::Range, 3> spanRanges{{ranges[0], ranges[1], {spanGuest, spanGuest + 32, wide.DeviceAddress(), Abi::Read, 0}}};
    const auto span = [&](std::uint64_t address, std::int32_t offset, std::uint32_t extracted, bool coherent, bool stops, std::array<std::uint32_t, 4> values, Abi::FaultReason reason = {}, std::uint64_t faultAddress = 0, std::uint32_t faultBytes = 0) {
        const auto pages = PagesFor(context, paged, spanRanges);
        const auto spanTable = MakeBinding(context, paged, pages, spanRanges);
        std::memset(fault.Bytes().data(), 0, fault.Bytes().size());
        const std::array<std::uint32_t, 4> sentinel{0xdeadbeef, 0xdeadbeef, 0xdeadbeef, 0xdeadbeef};
        std::memcpy(words.Bytes().data(), sentinel.data(), sizeof(sentinel));
        Pipeline pipeline(context, MakeBdaSpanReadTestShader(address, static_cast<std::uint32_t>(offset), extracted, coherent, stops), {spanTable.get(), &fault, &words});
        pipeline.Run(1);
        Abi::Fault report{};
        std::array<std::uint32_t, 4> result{};
        std::memcpy(&report, fault.Bytes().data(), sizeof(report));
        std::memcpy(result.data(), words.Bytes().data(), sizeof(result));
        const bool faults = static_cast<std::uint32_t>(reason) != 0u;
        if (faults) {
            Require(report.state == Abi::FaultState::Ready && report.reason == reason && report.address == faultAddress && report.bytes == faultBytes && report.instruction == 0x1234, "BDA span read did not publish its first fault");
        } else {
            Require(report.state == Abi::FaultState::Empty, "mapped BDA span read published a fault");
        }
        Require(result == (faults && stops ? sentinel : values), faults && stops ? "faulting BDA span read continued to output a substitute value" : "BDA span read produced incorrect data");
    };
    const auto at = [&](std::uint32_t dword) { return 0xa0b0c000u + dword; };
    for (const bool stops : {true, false}) {
        for (const bool coherent : {false, true}) {
            span(spanGuest + 16, -12, 0b1010u, coherent, stops, {0, at(2), 0, at(4)});
            span(spanGuest, 4, 0b0101u, coherent, stops, {at(1), 0, at(3), 0});
            span(spanGuest + 24, 0, 0b0011u, coherent, stops, {at(6), at(7), 0, 0});
            span(spanGuest + 20, 8, 0b0001u, coherent, stops, {at(7), 0, 0, 0});
            span(spanGuest + 28, 0, 0b0101u, coherent, stops, {at(7), 0, 0, 0}, Abi::FaultReason::Unmapped, spanGuest + 36, 1);
            span(guest, 1, 0b1001u, coherent, stops, {0x55443322, 0, 0, 0}, Abi::FaultReason::Unmapped, guest + 13, 1);
            span(4, -8, 0b0001u, coherent, stops, {0, 0, 0, 0}, Abi::FaultReason::Overflow, 4, 0);
        }
    }
    ranges[0].permissions = 0;
    run(guest, 8, 0, Abi::FaultReason::Permission);
    RunBdaDwordWriteTests(context, paged);
}

static void RunBdaPageTableTests(const Context& context) {
    namespace Abi = ShaderRecompiler::BdaAbi;
    namespace Ref = BdaPageReference;
    struct Probe {
        std::uint64_t address;
        std::uint32_t bytes;
        std::uint32_t permission;
    };
    Buffer fault(context, Abi::FaultBufferBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    const auto translate = [&](std::span<const Abi::Range> own, std::span<const Abi::Grant> grants, std::uint64_t pageTable, std::span<const Probe> probes) {
        const auto root = MakeBinding(context, own, grants, pageTable);
        const auto count = static_cast<std::uint32_t>(probes.size());
        Buffer io(context, std::size_t{count} * 8u * sizeof(std::uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        auto* words = reinterpret_cast<std::uint32_t*>(io.Bytes().data());
        for (std::uint32_t index = 0; index < count; ++index) {
            words[4u * index] = static_cast<std::uint32_t>(probes[index].address);
            words[4u * index + 1u] = static_cast<std::uint32_t>(probes[index].address >> 32u);
            words[4u * index + 2u] = probes[index].bytes;
            words[4u * index + 3u] = probes[index].permission;
        }
        std::memset(fault.Bytes().data(), 0, fault.Bytes().size());
        Pipeline pipeline(context, MakeBdaTranslateTestShader(count), {root.get(), &fault, &io});
        pipeline.Run((count + 63u) / 64u);
        Abi::Fault report{};
        std::memcpy(&report, fault.Bytes().data(), sizeof(report));
        Require(report.state == Abi::FaultState::Empty, "a BDA translation published a fault");
        std::vector<Ref::Translation> results(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            const auto* result = words + 4u * count + 4u * index;
            results[index] = {result[0] | (static_cast<std::uint64_t>(result[1]) << 32u), result[2]};
        }
        return results;
    };
    const auto hex = [](std::uint64_t value) {
        char text[24];
        std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(value));
        return std::string(text);
    };
    const auto probesOf = [](std::span<const Abi::Range> list, std::initializer_list<std::uint32_t> sizes) {
        std::vector<Probe> probes;
        for (const auto address : Ref::Probes(list)) {
            for (const auto bytes : sizes) {
                for (const auto permission : {Abi::Read, Abi::Write, Abi::Read | Abi::Write}) probes.push_back({address, bytes, permission});
            }
        }
        return probes;
    };
    const auto check = [&](const std::string& name, std::span<const Abi::Range> list, const std::shared_ptr<const BdaPageTable>& pages, std::span<const Abi::Range> held, std::span<const Abi::Grant> grants = {}, std::span<const Abi::Grant> bound = {}, bool boundGiven = false) {
        auto probes = probesOf(list, {1u, 4u, 16u, 64u, 0xfffffff0u});
        const auto own = Ref::Without(list, held);
        const auto gpuGrants = boundGiven ? bound : grants;
        for (const auto& set : {grants, gpuGrants}) {
            for (const auto& grant : set) {
                for (const auto edge : {grant.begin, grant.end}) {
                    for (const std::int64_t delta : {-17, -16, -4, -1, 0, 1, 4, 16}) {
                        for (const auto bytes : {1u, 4u, 16u}) {
                            for (const auto permission : {Abi::Write, Abi::Read | Abi::Write}) probes.push_back({edge + static_cast<std::uint64_t>(delta), bytes, permission});
                        }
                    }
                }
            }
        }
        const auto results = translate(own, gpuGrants, PageTableAddress(pages), probes);
        const auto alone = pages != nullptr ? translate({}, gpuGrants, pages->Address(), probes) : std::vector<Ref::Translation>{};
        const auto leaves = Ref::EncodeLeaves(held, [](std::uint32_t window, std::span<const Abi::Range> ranges) { return EncodeBdaWindow(window, ranges); });
        const Ref::Leaves none;
        std::size_t differs = 0;
        for (std::size_t index = 0; index < probes.size(); ++index) {
            const auto& probe = probes[index];
            const auto expected = Ref::Translate(leaves, own, probe.address, probe.bytes, probe.permission, gpuGrants);
            Require(results[index] == expected, name + ": the GPU translated " + hex(probe.address) + " (" + std::to_string(probe.bytes) + " bytes, permission " + std::to_string(probe.permission) + ") to " + hex(results[index].device) + " reason " + std::to_string(results[index].reason) + ", expected " + hex(expected.device) + " reason " + std::to_string(expected.reason));
            if (!(results[index] == Ref::Translate(none, list, probe.address, probe.bytes, probe.permission, grants))) ++differs;
            if (pages == nullptr) continue;
            const auto paged = Ref::Translate(none, held, probe.address, probe.bytes, probe.permission, gpuGrants);
            Require(alone[index] == paged, name + ": the GPU page table alone translated " + hex(probe.address) + " (" + std::to_string(probe.bytes) + " bytes, permission " + std::to_string(probe.permission) + ") to " + hex(alone[index].device) + " reason " + std::to_string(alone[index].reason) + ", its ranges to " + hex(paged.device) + " reason " + std::to_string(paged.reason));
        }
        return differs;
    };
    std::mt19937_64 random(0x9a6e7ab1eu);
    for (const auto base : {0x1fffd0000ull, 0x7ff6e4600000ull, 0x2000001234ull}) {
        const auto table = Ref::RandomTable(random, base, 60u);
        const auto grants = Ref::RandomGrants(random, table.list, 8u);
        Require(check("list alone", table.list, nullptr, {}) == 0u, "the BDA list lookup differs from its reference");
        Require(check("list alone with write grants", table.list, nullptr, {}, grants) == 0u, "the BDA list lookup with write grants differs from its reference");
        Require(check("random pages", table.list, BdaPageTable::Build(context, table.pages), table.pages) == 0u, "the page table and the own ranges translated a random table unlike its ranges");
        Require(check("random pages with write grants", table.list, BdaPageTable::Build(context, table.pages), table.pages, grants) == 0u, "the page table and the own ranges with write grants translated a random table unlike its ranges");
        auto shifted = grants;
        for (auto& grant : shifted) ++grant.end;
        Require(check("wrong write grants", table.list, BdaPageTable::Build(context, table.pages), table.pages, grants, shifted, true) != 0u, "write grants one byte too long went unnoticed");
    }
    constexpr std::uint64_t base = 0x3fffff0000ull;
    const std::vector<Abi::Range> shapes{
        {base + 0x100u, base + 0x3000u, 0x100000000ull, Abi::Read, 0u},
        {base + 0x3000u, base + 0x9001u, 0x200000000ull, Abi::Read | Abi::Write, 0u},
        {base + 0x9001u, base + 0x9002u, 0x300000000ull, Abi::Read, 0u},
        {base + 0x9010u, base + 0x10000u, 0x400000000ull, 0u, 0u},
        {base + 0x10000u, base + 0x10000u + 0x240000000ull, 0x500000000ull, Abi::Read | Abi::Write, 0u},
    };
    Require(check("partial and split pages", shapes, BdaPageTable::Build(context, shapes), shapes) == 0u, "the page table changed a BDA translation of partial and split pages");
    const std::vector<Abi::Range> shared{shapes[0], shapes[2], shapes[4]};
    Require(check("split pages shared with own ranges", shapes, BdaPageTable::Build(context, shared), shared) == 0u, "the page table and the own ranges of one page translated unlike their ranges");
    auto wrong = shapes;
    wrong[1].deviceAddress += 0x40u;
    Require(check("wrong translation", shapes, BdaPageTable::Build(context, wrong), wrong) != 0u, "a page table with a wrong translation went unnoticed");
    auto writable = shapes;
    writable[0].permissions |= Abi::Write;
    Require(check("wrong permission", shapes, BdaPageTable::Build(context, writable), writable) != 0u, "a page table that made a read-only range writable went unnoticed");
    constexpr std::uint64_t split = 0x6000001000ull;
    constexpr std::uint64_t splitDevice = 0xa00000000ull;
    const std::vector<Abi::Range> whole{{split, split + 0x30000u, splitDevice, Abi::Read, 0u}};
    const std::vector<Abi::Grant> middle{{split + 0x10010u, split + 0x10050u}};
    const auto wholePages = BdaPageTable::Build(context, whole);
    Require(check("write grant inside a read-only range", whole, wholePages, whole, middle) == 0u, "a write grant inside a read-only range translated unlike its reference");
    const auto grantProbes = probesOf(std::vector<Abi::Range>{{split + 0x10010u, split + 0x10050u, splitDevice + 0x10010u, Abi::Read, 0u}}, {1u, 4u, 16u, 64u});
    const auto granted = translate({}, middle, wholePages->Address(), grantProbes);
    const auto refused = translate({}, {}, wholePages->Address(), grantProbes);
    std::size_t permitted = 0;
    for (std::size_t index = 0; index < grantProbes.size(); ++index) {
        const auto& probe = grantProbes[index];
        const bool inside = probe.address >= split && probe.address + probe.bytes <= split + 0x30000u;
        const bool covered = probe.address >= middle[0].begin && probe.address + probe.bytes <= middle[0].end;
        if (!inside) continue;
        if (probe.permission == Abi::Read) {
            Require(granted[index] == Ref::Translation{splitDevice + (probe.address - split), 0u} && refused[index] == granted[index], "a read inside a read-only range with a write grant at " + hex(probe.address) + " was not translated");
            continue;
        }
        Require(refused[index] == Ref::Fault(Abi::FaultReason::Permission), "a store to a read-only range without a write grant at " + hex(probe.address) + " was not a permission fault");
        Require(granted[index] == (covered ? Ref::Translation{splitDevice + (probe.address - split), 0u} : Ref::Fault(Abi::FaultReason::Permission)), "a store at " + hex(probe.address) + " (" + std::to_string(probe.bytes) + " bytes) was not decided by the write grant");
        if (covered) ++permitted;
    }
    Require(permitted != 0u, "no probe was permitted by the write grant");
    constexpr std::uint64_t window = 0x500000000ull;
    const Abi::Range kept{window + 0x200000000ull, window + 0x200040000ull, 0x600000000ull, Abi::Read, 0u};
    const std::vector<Abi::Range> before{{window + 0x10u, window + 0x8000u, 0x700000000ull, Abi::Read, 0u}, {window + 0x8000u, window + 0x30000u, 0x800000000ull, Abi::Read | Abi::Write, 0u}, kept};
    const std::vector<Abi::Range> after{{window + 0x10u, window + 0x8000u, 0x700000000ull, Abi::Read, 0u}, {window + 0x9000u, window + 0x9100u, 0x900000000ull, Abi::Read, 0u}, kept};
    const auto first = BdaPageTable::Build(context, before);
    const auto second = BdaPageTable::Build(context, after);
    const auto keptWindow = static_cast<std::uint32_t>(kept.begin >> Abi::WindowShift);
    const auto changedWindow = static_cast<std::uint32_t>(window >> Abi::WindowShift);
    Require(first->LeafAddress(keptWindow) != 0u && first->LeafAddress(keptWindow) == second->LeafAddress(keptWindow), "a page table update did not share the leaf of an unchanged window");
    Require(first->LeafAddress(changedWindow) != second->LeafAddress(changedWindow) && first->Address() != second->Address(), "a page table update changed the table it was derived from");
    Require(check("before the update", before, first, before) == 0u, "the page table before an update changed a BDA translation");
    Require(check("after the update", after, second, after) == 0u, "the page table after an update changed a BDA translation");
    Require(check("before the update, again", before, first, before) == 0u, "an update changed the page table a recorded dispatch still uses");
}

static void RunBdaWrittenPageTests(const Context& context) {
    namespace Abi = ShaderRecompiler::BdaAbi;
    constexpr std::uint64_t guest = 0x7ffe00000000ULL;
    constexpr std::uint64_t page = std::uint64_t{1} << Abi::WrittenPageShift;
    struct Case {
        const char* name;
        BdaLaneStore store;
        std::uint64_t stride;
        std::uint64_t offset;
        std::uint64_t active;
    };
    const auto bytesOf = [](BdaLaneStore store) { return store == BdaLaneStore::Byte ? 1u : store == BdaLaneStore::Short ? 2u : store == BdaLaneStore::Dwordx4 ? 16u : 4u; };
    const std::array<Case, 10> cases{{
        {"unaligned dwords across pages, alternate lanes", BdaLaneStore::Dword, 0x1000u, 0xffeu, 0xaaaaaaaaaaaaaaaaull},
        {"unaligned runtime dwords across pages, other lanes", BdaLaneStore::RuntimeDword, 0x1000u, 0xffdu, 0x5555555555555555ull},
        {"dwordx4 across chunks, scattered lanes", BdaLaneStore::Dwordx4, 0x20000u, 0x1fff8u, 0x8421084210842108ull},
        {"unaligned dwordx4 across pages", BdaLaneStore::Dwordx4, 0x3000u, 0x2ff6u, 0xffffffff00000000ull},
        {"aligned dwords of all lanes in one page", BdaLaneStore::Dword, 16u, 0u, ~0ull},
        {"bytes of every third lane", BdaLaneStore::Byte, 0x1001u, 1u, 0x9249249249249249ull},
        {"shorts across pages", BdaLaneStore::Short, 0x2000u, 0xfffu, 0x00000000ffff0000ull},
        {"atomics of all lanes", BdaLaneStore::Atomic, 0x3000u, 0x10u, ~0ull},
        {"one lane", BdaLaneStore::Dword, 0x20000u, 0x1fffeu, 1ull << 37u},
        {"no lane", BdaLaneStore::Dword, 0x1000u, 0xffeu, 0ull},
    }};
    Buffer fault(context, Abi::FaultBufferBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Buffer output(context, 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    for (const bool paged : {false, true}) {
        for (const auto& test : cases) {
            const auto bytes = bytesOf(test.store);
            const auto span = test.offset + 64u * test.stride + 64u;
            Buffer memory(context, static_cast<std::size_t>(span), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
            const std::array<Abi::Range, 1> ranges{{{guest, guest + span, memory.DeviceAddress(), Abi::Read | Abi::Write, 0}}};
            const auto pages = PagesFor(context, paged, ranges);
            const auto table = MakeBinding(context, paged, pages, ranges);
            std::memset(memory.Bytes().data(), 0, memory.Bytes().size());
            std::memset(fault.Bytes().data(), 0, fault.Bytes().size());
            Pipeline pipeline(context, MakeBdaLaneStoreTestShader(guest + test.offset, test.stride, test.active, test.store), {table.get(), &fault, &output});
            pipeline.Run(1);
            const std::string label = std::string("written pages, ") + test.name + (paged ? " (paged): " : ": ");
            Abi::Fault report{};
            std::memcpy(&report, fault.Bytes().data(), sizeof(report));
            Require(report.state == Abi::FaultState::Empty, label + "a mapped store published a fault");
            std::set<std::uint64_t> expectedPages;
            std::set<std::uint32_t> expectedChunks;
            std::vector<std::uint8_t> expected(memory.Bytes().size(), 0u);
            for (std::uint32_t lane = 0; lane < 64u; ++lane) {
                if (((test.active >> lane) & 1u) == 0u) continue;
                const auto address = guest + test.offset + lane * test.stride;
                for (std::uint32_t byte = 0; byte < bytes; ++byte) {
                    expectedPages.insert((address + byte) >> Abi::WrittenPageShift);
                    expectedChunks.insert(Abi::WrittenChunkKey(address + byte));
                    expected[address + byte - guest] = static_cast<std::uint8_t>(BdaLaneStoreValue >> ((byte % 4u) * 8u));
                }
            }
            Require(std::memcmp(memory.Bytes().data(), expected.data(), expected.size()) == 0, label + "the stores of the active lanes landed incorrectly or an inactive lane stored");
            std::array<std::uint32_t, Abi::FaultBufferBytes / sizeof(std::uint32_t)> words{};
            std::memcpy(words.data(), fault.Bytes().data(), sizeof(words));
            Require(words[Abi::WrittenOverflowWord] == 0, label + "the written-page table overflowed");
            std::set<std::uint64_t> recorded;
            Require(Abi::ForEachWrittenRange(words.data(), [&](std::uint64_t begin, std::size_t length) {
                for (std::uint64_t at = begin; at < begin + length; at += page) Require(recorded.insert(at >> Abi::WrittenPageShift).second, label + "a page is recorded twice");
            }), label + "invalid written-page record");
            Require(recorded == expectedPages, label + "the records name " + std::to_string(recorded.size()) + " pages, the active lanes wrote " + std::to_string(expectedPages.size()));
            Require(words[Abi::WrittenCountWord] == expectedChunks.size(), label + "the records claim " + std::to_string(words[Abi::WrittenCountWord]) + " chunks, the active lanes wrote " + std::to_string(expectedChunks.size()));
        }
    }
}

void RunBdaExecutionTests(const Context& context) {
    RunBdaPageTableTests(context);
    for (const bool paged : {false, true}) RunBdaReadTests(context, paged);
    RunBdaWrittenPageTests(context);
}

void RunBdaStoreExecutionTests(const Context& context) {
    namespace Abi = ShaderRecompiler::BdaAbi;
    Buffer first(context, 2, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    Buffer second(context, 3, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    Buffer fault(context, Abi::FaultBufferBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    Buffer output(context, 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    constexpr std::uint64_t guest = 0x7fff1235fffdULL;
    constexpr std::uint32_t writable = Abi::Read | Abi::Write;
    std::array<Abi::Range, 2> ranges{};
    std::vector<Abi::Grant> grants;
    bool runtime = false;
    bool paged = false;
    const auto run = [&](const char* name, std::uint64_t address, std::uint32_t bits, bool coherent, const std::array<std::uint8_t, 5>& expected, std::initializer_list<std::uint64_t> writtenAddresses, Abi::FaultReason reason = {}, std::uint64_t faultAddress = 0, std::uint32_t faultBytes = 0) {
        const std::string label = std::string(runtime ? "BDA runtime write " : "BDA store ") + name + (coherent ? " coherent" : "") + (paged ? " paged: " : ": ");
        std::memset(first.Bytes().data(), 0xcd, first.Bytes().size());
        std::memset(second.Bytes().data(), 0xcd, second.Bytes().size());
        const auto pages = PagesFor(context, paged, ranges);
        const auto table = MakeBinding(context, paged, pages, ranges, grants);
        std::memset(fault.Bytes().data(), 0, fault.Bytes().size());
        std::memset(output.Bytes().data(), 0, output.Bytes().size());
        Pipeline pipeline(context, runtime ? MakeBdaWriteTestShader(address, 0xa1b2c3d4u, bits, coherent) : MakeBdaStoreTestShader(address, 0xa1b2c3d4u, bits, coherent), {table.get(), &fault, &output});
        pipeline.Run(1);
        std::array<std::uint8_t, 5> actual{};
        std::memcpy(actual.data(), first.Bytes().data(), 2);
        std::memcpy(actual.data() + 2, second.Bytes().data(), 3);
        Require(actual == expected, label + "incorrect stored bytes or writes outside the mapped span");
        Abi::Fault report{};
        std::memcpy(&report, fault.Bytes().data(), sizeof(report));
        if (reason == Abi::FaultReason{}) {
            Require(report.state == Abi::FaultState::Empty && report.reason == Abi::FaultReason{} && report.address == 0 && report.bytes == 0 && report.stage == 0 && report.instruction == 0 && report.reserved == 0, label + "mapped store published a fault");
        } else {
            Require(report.state == Abi::FaultState::Ready && report.reason == reason && report.address == faultAddress && report.bytes == faultBytes && report.stage == static_cast<std::uint32_t>(ShaderRecompiler::IrShaderStage::Compute) && report.instruction == 0x1234 && report.reserved == 0, label + "incorrect first fault metadata");
        }
        std::array<std::uint32_t, Abi::FaultBufferBytes / sizeof(std::uint32_t)> words{};
        std::memcpy(words.data(), fault.Bytes().data(), sizeof(words));
        Require(words[Abi::WrittenOverflowWord] == 0, label + "written-page table overflowed");
        std::set<std::uint64_t> recordedPages;
        Require(Abi::ForEachWrittenRange(words.data(), [&](std::uint64_t address, std::size_t bytes) {
            for (std::size_t offset = 0; offset < bytes; offset += std::size_t{1} << Abi::WrittenPageShift) {
                Require(recordedPages.insert((address + offset) >> Abi::WrittenPageShift).second, label + "written-page table contains duplicate pages");
            }
        }), label + "invalid written-page record");
        std::set<std::uint64_t> expectedPages;
        std::set<std::uint32_t> expectedChunks;
        for (const auto writtenAddress : writtenAddresses) {
            expectedPages.insert(writtenAddress >> Abi::WrittenPageShift);
            expectedChunks.insert(Abi::WrittenChunkKey(writtenAddress));
        }
        Require(recordedPages == expectedPages, label + "incorrect written-page set");
        Require(words[Abi::WrittenCountWord] == expectedChunks.size(), label + "incorrect written-chunk claim count");
        std::uint32_t completed = 0;
        std::memcpy(&completed, output.Bytes().data(), sizeof(completed));
        Require(completed == 1, label + "store did not finish its per-byte accesses");
    };
    for (const auto& [coherent, writes, withPages] : std::initializer_list<std::tuple<bool, bool, bool>>{{false, false, false}, {false, true, false}, {true, false, false}, {true, true, false}, {false, false, true}, {false, true, true}, {true, false, true}, {true, true, true}}) {
        runtime = writes;
        paged = withPages;
        ranges = {{{guest, guest + 2, first.DeviceAddress(), writable, 0}, {guest + 2, guest + 5, second.DeviceAddress(), writable, 0}}};
        run("byte", guest + 1, 8, coherent, {0xcd, 0xd4, 0xcd, 0xcd, 0xcd}, {guest + 1});
        run("split short", guest + 1, 16, coherent, {0xcd, 0xd4, 0xc3, 0xcd, 0xcd}, {guest + 1, guest + 2});
        run("split unaligned dword", guest, 32, coherent, {0xd4, 0xc3, 0xb2, 0xa1, 0xcd}, {guest, guest + 3});
        ranges[1].permissions = Abi::Read;
        run("read-only byte", guest + 2, 8, coherent, {0xcd, 0xcd, 0xcd, 0xcd, 0xcd}, {}, Abi::FaultReason::Permission, guest + 2, 1);
        run("read-only short suffix", guest + 1, 16, coherent, {0xcd, 0xd4, 0xcd, 0xcd, 0xcd}, {guest + 1}, Abi::FaultReason::Permission, guest + 2, 1);
        run("read-only suffix", guest, 32, coherent, {0xd4, 0xc3, 0xcd, 0xcd, 0xcd}, {guest}, Abi::FaultReason::Permission, guest + 2, 1);
        if (!coherent) {
            grants = {{guest + 2, guest + 5}};
            run("granted byte", guest + 2, 8, coherent, {0xcd, 0xcd, 0xd4, 0xcd, 0xcd}, {guest + 2});
            run("granted suffix", guest, 32, coherent, {0xd4, 0xc3, 0xb2, 0xa1, 0xcd}, {guest, guest + 3});
            grants = {{guest + 3, guest + 5}};
            run("partly granted suffix", guest, 32, coherent, {0xd4, 0xc3, 0xcd, 0xa1, 0xcd}, {guest, guest + 3}, Abi::FaultReason::Permission, guest + 2, 1);
            grants = {{guest, guest + 3}};
            run("grant ending inside a store", guest + 1, 32, coherent, {0xcd, 0xd4, 0xc3, 0xcd, 0xcd}, {guest + 1, guest + 2}, Abi::FaultReason::Permission, guest + 3, 1);
            grants.clear();
        }
        ranges[0].end = guest + 1;
        ranges[1].permissions = writable;
        run("unmapped middle byte", guest, 32, coherent, {0xd4, 0xcd, 0xb2, 0xa1, 0xcd}, {guest, guest + 3}, Abi::FaultReason::Unmapped, guest + 1, 1);
        ranges = {{{guest + 3, guest + 5, first.DeviceAddress(), writable, 0}, {guest + 5, guest + 8, second.DeviceAddress(), writable, 0}}};
        run("split aligned dword", guest + 3, 32, coherent, {0xcd, 0xcd, 0xcd, 0xcd, 0xcd}, {}, Abi::FaultReason::Unmapped, guest + 3, 4);
    }
}
