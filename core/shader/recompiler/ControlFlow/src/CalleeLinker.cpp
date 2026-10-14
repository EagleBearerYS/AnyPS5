#include "ControlFlow/CalleeLinker.hpp"
#include "RdnaDecoder/RdnaInstructionDecoder.hpp"
#include <algorithm>
#include <cstdio>
#include <exception>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ShaderRecompiler {

namespace {

std::string hexText(std::uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(value));
    return text;
}

std::vector<std::uint32_t> readCallee(const CalleeMemory& memory, std::uint64_t address, std::uint32_t linkRegister) {
    constexpr std::uint64_t page = 4096u;
    constexpr std::size_t limitWords = 16384u;
    if (address % 4u != 0u) throw std::runtime_error("its callee address " + hexText(address) + " is not dword aligned");
    std::vector<std::uint32_t> words;
    while (words.size() < limitWords) {
        const auto next = address + words.size() * 4u;
        const auto count = static_cast<std::size_t>((page - next % page) / 4u);
        const auto old = words.size();
        words.resize(old + count);
        if (!memory.readCode(memory.context, next, std::span(words).subspan(old))) throw std::runtime_error("its callee at " + hexText(address) + " is not readable at " + hexText(next));
        if (const auto length = UserDataCalleeLength(words, linkRegister)) {
            words.resize(*length);
            return words;
        }
    }
    throw std::runtime_error("its callee at " + hexText(address) + " does not return within " + std::to_string(limitWords * 4u) + " bytes");
}

std::vector<std::uint32_t> readWords(const CalleeMemory& memory, std::uint64_t address, std::uint64_t count, const char* what) {
    const auto aligned = address & ~std::uint64_t{3u};
    const auto bytes = count * sizeof(std::uint32_t);
    std::vector<std::uint32_t> words(static_cast<std::size_t>(count));
    if (aligned > std::numeric_limits<std::uint64_t>::max() - bytes || !memory.readData(memory.context, aligned, words)) {
        throw std::runtime_error(std::string("its ") + what + " at " + hexText(address) + " + " + hexText(bytes) + " is not readable");
    }
    return words;
}

std::vector<TableCallee> readTableCallees(const RecompileRequest& request, const TableCall& call, const CalleeMemory& memory) {
    constexpr std::uint64_t limitBytes = 64u << 20u;
    constexpr std::size_t limitCallees = 64u;
    const auto slot = call.userDataRegister - request.context.userDataBaseRegister;
    const auto pointer = static_cast<std::uint64_t>(request.context.userData[slot]) | static_cast<std::uint64_t>(request.context.userData[slot + 1u]) << 32u;
    const auto descriptor = readWords(memory, pointer + call.descriptorOffset, 4u, "call table descriptor");
    const auto stride = (descriptor[1] >> 16u) & 0x3fffu;
    const auto bytes = stride == 0u ? static_cast<std::uint64_t>(descriptor[2]) : static_cast<std::uint64_t>(stride) * descriptor[2];
    const auto base = (static_cast<std::uint64_t>(descriptor[0]) | static_cast<std::uint64_t>(descriptor[1]) << 32u) & 0xffffffffffffull;
    if (bytes > limitBytes) throw std::runtime_error("its call table of " + std::to_string(bytes) + " bytes is larger than the " + std::to_string(limitBytes) + " bytes that are searched for callees");
    const auto table = bytes < sizeof(std::uint32_t) ? std::vector<std::uint32_t>{} : readWords(memory, base, bytes / sizeof(std::uint32_t), "call table");
    if (memory.observeTable != nullptr) memory.observeTable(memory.context, call, base & ~std::uint64_t{3u}, table);
    const auto targetDword = (call.entryOffset - call.recordOffset) / 4u;
    std::vector<bool> held(call.recordDwords, false);
    held[targetDword] = held[targetDword + 1u] = true;
    for (const auto& reg : call.recordRegisters) held[reg.dword] = true;
    std::map<std::vector<std::uint32_t>, std::uint64_t> records;
    for (std::uint64_t offset = call.recordOffset; offset + (targetDword + 2u) * 4u <= bytes; offset += call.entryStride) {
        std::vector<std::uint32_t> record(call.recordDwords, 0u);
        for (std::uint32_t dword = 0; dword < call.recordDwords; ++dword) {
            if (held[dword] && offset / 4u + dword < table.size()) record[dword] = table[offset / 4u + dword];
        }
        const auto target = static_cast<std::uint64_t>(record[targetDword + 1u]) << 32u | record[targetDword];
        if (target != 0u) records.emplace(std::move(record), target);
        if (call.entryStride == 0u) break;
    }
    if (records.size() > limitCallees) throw std::runtime_error("its call table holds " + std::to_string(records.size()) + " distinct records; at most " + std::to_string(limitCallees) + " are linked");
    std::map<std::uint64_t, std::vector<std::uint32_t>> codes;
    std::vector<TableCallee> callees;
    for (const auto& [record, target] : records) {
        auto code = codes.find(target);
        if (code == codes.end()) code = codes.emplace(target, readCallee(memory, target, call.linkRegister)).first;
        callees.push_back({target, record, code->second});
    }
    return callees;
}

}

LinkedCode LinkCallees(const RecompileRequest& request, const CalleeMemory& memory) {
    if (request.shader.stage != ShaderStage::Compute) return {};
    const auto code = request.shader.code;
    if (std::none_of(code.begin(), code.end(), [](std::uint32_t word) { return (word & 0xff80ff00u) == 0xbe802100u; })) return {};
    const auto program = RdnaInstructionDecoder{}.Decode(code);
    const SwappcInfo swappc{false, request.context.userDataBaseRegister, static_cast<std::uint32_t>(request.context.userData.size()), request.context.waveSize};
    const auto calls = FindUserDataCalls(program, swappc);
    const auto tableCalls = FindTableCalls(program, swappc);
    if (calls.empty() && tableCalls.empty()) return {};
    std::vector<std::vector<std::uint32_t>> callees;
    for (const auto& call : calls) {
        const auto slot = call.userDataRegister - swappc.userDataBaseRegister;
        const auto address = static_cast<std::uint64_t>(request.context.userData[slot]) | static_cast<std::uint64_t>(request.context.userData[slot + 1u]) << 32u;
        try {
            callees.push_back(readCallee(memory, address, call.linkRegister));
        } catch (const std::exception& error) {
            throw std::runtime_error("compute shader " + hexText(request.shader.codeAddress) + " calls user data s[" + std::to_string(call.userDataRegister) + ":" +
                std::to_string(call.userDataRegister + 1u) + "] at program counter " + hexText(call.programCounter) + ", but " + error.what());
        }
    }
    std::vector<std::vector<TableCallee>> tableCallees;
    for (const auto& call : tableCalls) {
        try {
            tableCallees.push_back(readTableCallees(request, call, memory));
        } catch (const std::exception& error) {
            throw std::runtime_error("compute shader " + hexText(request.shader.codeAddress) + " calls a target loaded from a table at program counter " + hexText(call.programCounter) + ", but " + error.what());
        }
    }
    return LinkCalls(program, calls, callees, tableCalls, tableCallees);
}

}
