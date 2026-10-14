#include "prx/libSceAgcDriver/Graphics/include/BdaResources.hpp"
#include "prx/libSceAgcDriver/Graphics/include/BdaPageTable.hpp"
#include "ImageTableAbi.hpp"
#include "DeviceImageTableAbi.hpp"
#include "prx/libSceAgcDriver/Graphics/include/DeviceImageTable.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <optional>
#include <iomanip>
#include <sstream>

namespace AgcDriver::Graphics {

BdaResources::BdaResources(const Context& context) : deviceImages(context.deviceImages) {
    static_assert(std::endian::native == std::endian::little);
    Require(ShaderRecompiler::BdaAbi::FaultBufferBytes <= context.limits.maxStorageBufferRange, "BDA fault buffer exceeds storage buffer range limit");
    fault = std::make_unique<Buffer>(context, ShaderRecompiler::BdaAbi::FaultBufferBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    std::memset(fault->Bytes().data(), 0, ShaderRecompiler::BdaAbi::WrittenListWord * sizeof(std::uint32_t));
}

BdaResources::BdaResources(const Context& context, const GuestBufferMemory& memory) : BdaResources(context) {
    namespace Abi = ShaderRecompiler::BdaAbi;
    pages = memory.PageTable(context);
    ownRanges = memory.OwnRanges();
    writeGrants = memory.WriteGrants();
    Require(ownRanges.size() <= std::numeric_limits<std::uint32_t>::max() && writeGrants.size() <= std::numeric_limits<std::uint32_t>::max(), "BDA table range count overflow");
    tableBytes = sizeof(Abi::Header) + ownRanges.size() * sizeof(Abi::Range) + writeGrants.size() * sizeof(Abi::Grant);
    Require(tableBytes <= context.limits.maxStorageBufferRange, "BDA descriptors exceed storage buffer range limit");
    for (const auto& range : ownRanges) Require((range.permissions & Abi::Write) == 0 || range.end <= Abi::WrittenAddressLimit, "a writable BDA range ends past the addresses the written-page record can hold");
    for (const auto& grant : writeGrants) Require(grant.end <= Abi::WrittenAddressLimit, "a BDA write grant ends past the addresses the written-page record can hold");
    ValidateBdaRangeList(ownRanges);
    ValidateBdaGrants(writeGrants);
    table = std::make_unique<Buffer>(context, tableBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    const Abi::Header header{Abi::TableVersion, static_cast<std::uint32_t>(ownRanges.size()), static_cast<std::uint32_t>(writeGrants.size()), 0, pages != nullptr ? pages->Address() : 0u, 0};
    auto* bytes = table->Bytes().data();
    std::memcpy(bytes, &header, sizeof(header));
    if (!ownRanges.empty()) std::memcpy(bytes + sizeof(header), ownRanges.data(), ownRanges.size() * sizeof(Abi::Range));
    if (!writeGrants.empty()) std::memcpy(bytes + sizeof(header) + ownRanges.size() * sizeof(Abi::Range), writeGrants.data(), writeGrants.size() * sizeof(Abi::Grant));
}

VkDescriptorBufferInfo BdaResources::Table() const {
    Require(table != nullptr, "BDA page table was not requested");
    return {table->Handle(), 0, tableBytes};
}

VkDescriptorBufferInfo BdaResources::Fault() const {
    return {fault->Handle(), 0, ShaderRecompiler::BdaAbi::FaultBufferBytes};
}

namespace {
std::atomic<bool> loopGuardTripped{false};
}

bool LoopGuardTripped() {
    return loopGuardTripped.load(std::memory_order_relaxed);
}

void BdaResources::CheckFault() const {
    markWrittenPages();
    ShaderRecompiler::BdaAbi::Fault report{};
    std::memcpy(&report, fault->Bytes().data(), sizeof(report));
    if (report.state == ShaderRecompiler::BdaAbi::FaultState::Empty) {
        Require(static_cast<std::uint32_t>(report.reason) == 0 && report.address == 0 && report.bytes == 0 && report.stage == 0 && report.instruction == 0 && report.reserved == 0, "BDA fault record has data without publication");
        return;
    }
    Require(report.state == ShaderRecompiler::BdaAbi::FaultState::Ready && report.reserved == 0, "incomplete or invalid BDA fault record");
    Require(report.reason != ShaderRecompiler::BdaAbi::FaultReason::InvalidRectangle, "rect-list requires finite nondegenerate axis-aligned positions with equal positive W");
    if (report.reason == ShaderRecompiler::BdaAbi::FaultReason::LoopLimit) {
        // APS5_LOOP_GUARD: the shader left a loop that ran past the guard; the dispatch result is kept.
        loopGuardTripped.store(true, std::memory_order_relaxed);
        std::fprintf(stderr, "[gpu] loop guard: the loop exit at pc 0x%x of shader 0x%llx ran past %u evaluations\n", report.instruction, static_cast<unsigned long long>(report.address), report.bytes);
        std::memset(fault->Bytes().data(), 0, fault->Bytes().size());
        return;
    }
    if (report.reason == ShaderRecompiler::BdaAbi::FaultReason::ImageTable) {
        const auto table = std::find_if(imageTables.begin(), imageTables.end(), [&](const ImageTableFaults& candidate) { return candidate.stage == report.stage; });
        const auto* tables = table != imageTables.end() ? &*table : imageTables.size() == 1 ? &imageTables.front() : nullptr;
        std::ostringstream message;
        message << "image table: shader 0x" << std::hex << (tables != nullptr ? tables->shader : 0u) << std::dec << " stage " << report.stage << " pc 0x" << std::hex << report.instruction;
        if ((report.bytes & ShaderRecompiler::DeviceImageTableAbi::FaultFlag) != 0u) {
            const auto rejection = deviceImages != nullptr ? deviceImages->FindRejection(report.bytes & ~ShaderRecompiler::DeviceImageTableAbi::FaultFlag) : std::nullopt;
            message << " selected a descriptor the device image table rejected" << std::dec;
            if (rejection.has_value()) {
                message << " (words";
                for (const auto word : rejection->words) message << ' ' << std::hex << std::setw(8) << std::setfill('0') << word;
                message << std::dec << "): " << ShaderRecompiler::ImageTableAbi::PoisonReasonName(static_cast<ShaderRecompiler::ImageTableAbi::PoisonReason>(rejection->reason)) << ": " << rejection->message;
            } else {
                message << ": rejection " << (report.bytes & ~ShaderRecompiler::DeviceImageTableAbi::FaultFlag);
            }
            throw std::runtime_error(message.str());
        }
        message << " selected the descriptor at 0x" << report.address << std::dec;
        if (tables != nullptr && report.bytes < tables->poison.size()) {
            const auto& poison = tables->poison[report.bytes];
            if (poison.dwordCount != 0) {
                message << " (words";
                for (std::uint32_t index = 0; index < poison.dwordCount && index < poison.words.size(); ++index) message << ' ' << std::hex << std::setw(8) << std::setfill('0') << poison.words[index];
                message << std::dec << ")";
            }
            message << ": " << ShaderRecompiler::ImageTableAbi::PoisonReasonName(static_cast<ShaderRecompiler::ImageTableAbi::PoisonReason>(poison.reason));
            if (report.bytes < tables->messages.size() && !tables->messages[report.bytes].empty()) message << ": " << tables->messages[report.bytes];
        } else {
            message << ": poison entry " << report.bytes;
        }
        throw std::runtime_error(message.str());
    }
    std::ostringstream message;
    if (report.reason == ShaderRecompiler::BdaAbi::FaultReason::UnlinkedCall) {
        message << "shader stage " << report.stage << " called address 0x" << std::hex << report.address << " at program counter 0x" << report.instruction << ", which no linked callee of that call matches";
        throw std::runtime_error(message.str());
    }
    message << "BDA access failed: address=0x" << std::hex << report.address << " instruction=0x" << report.instruction << std::dec << " bytes=" << report.bytes << " stage=" << report.stage << " reason=" << static_cast<std::uint32_t>(report.reason);
    if (report.reason == ShaderRecompiler::BdaAbi::FaultReason::Permission && table != nullptr) {
        auto range = pages != nullptr ? pages->Find(report.address) : std::nullopt;
        if (!range.has_value()) {
            const auto found = std::upper_bound(ownRanges.begin(), ownRanges.end(), report.address, [](std::uint64_t value, const ShaderRecompiler::BdaAbi::Range& candidate) { return value < candidate.begin; });
            if (found != ownRanges.begin() && report.address < std::prev(found)->end) range = *std::prev(found);
        }
        if (range.has_value()) message << std::hex << "; the store hit 0x" << range->begin << "+0x" << range->end - range->begin << ", read-only in the BDA table: stores through GPU-selected descriptors and FLAT/GLOBAL addresses reach only writable ranges imported in place, not ones served by a mirror or a copy (past APS5_HOST_IMPORT_MIB, or refused by the driver)";
        message << std::dec << "; this dispatch's write grants: " << writeGrants.size() << std::hex;
        for (std::size_t index = 0; index < writeGrants.size() && index < 8u; ++index) message << " 0x" << writeGrants[index].begin << "-0x" << writeGrants[index].end;
    }
    throw std::runtime_error(message.str());
}

}

namespace AgcDriver::Graphics {

void BdaResources::SetImageTables(std::vector<ImageTableFaults> tables, std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges) {
    imageTables = std::move(tables);
    imageTableRanges = std::move(ranges);
}

void BdaResources::markWrittenPages() const {
    namespace Abi = ShaderRecompiler::BdaAbi;
    auto* words = reinterpret_cast<std::uint32_t*>(fault->Bytes().data());
    Require(words[Abi::WrittenOverflowWord] == 0, "more than " + std::to_string(Abi::WrittenPageSlots) + " chunks of " + std::to_string(Abi::WrittenChunkPages) + " pages stored to through the BDA table in one use are not implemented");
    const auto count = words[Abi::WrittenCountWord];
    if (count == 0) return;
    Require(Abi::ForEachWrittenRange(words, [&](std::uint64_t begin, std::size_t bytes) {
        const auto end = begin + bytes;
        for (const auto& [base, size] : imageTableRanges) {
            if (size != 0 && begin < base + size && base < end) {
                std::ostringstream message;
                message << "image table at 0x" << std::hex << base << " is written by its own dispatch";
                Require(false, message.str());
            }
        }
        GuestMemory::MarkWritten(begin, bytes);
    }), "invalid BDA written-page record");
    for (std::uint32_t claim = 0; claim < count; ++claim) {
        const auto slot = words[Abi::WrittenListWord + claim];
        words[Abi::WrittenSlotsWord + 2 * slot] = 0;
        words[Abi::WrittenSlotsWord + 2 * slot + 1] = 0;
    }
    words[Abi::WrittenCountWord] = 0;
}

}
