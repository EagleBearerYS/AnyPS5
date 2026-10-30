#include "prx/libSceAgcDriver/Execution/include/Driver/Driver.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Diagnostics.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Synchronization/SynchronizationStatistics.hpp"
#include "prx/libSceAgcDriver/Execution/include/GuestMemory.hpp"
#include "prx/libSceAgcDriver/Execution/include/Pm4.hpp"
#include "prx/libSceAgcDriver/Execution/include/Mutex.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace AgcDriver::DriverDetail {

void Driver::noteLabelStore(std::uint64_t address, std::span<const std::byte> bytes, std::uint64_t stamp, std::uint32_t queue, std::uint32_t opcode) {
    if (!KeepsLabelStore(address, bytes.size())) return;
    std::lock_guard lock(labelStoresMutex);
    for (std::size_t offset = 0; offset < bytes.size(); offset += 4) {
        auto& history = labelStores[address + offset];
        std::shift_right(history.begin(), history.end(), 1);
        history[0].stamp = stamp;
        history[0].queue = queue;
        history[0].opcode = opcode;
        std::memcpy(&history[0].value, bytes.data() + offset, 4);
    }
}

namespace {

std::uint64_t queueDword(std::uint32_t queue, std::uint64_t dword) {
    return dword | (static_cast<std::uint64_t>(queue) << 56u);
}

}

void Driver::noteLabelPass(std::span<const std::uint32_t> packet, std::uint32_t queue) {
    const auto opcode = (packet[0] >> 8u) & 0xffu;
    if ((opcode != 0x3c && opcode != 0x93) || packet.size() < 7 || ((packet[1] >> 4u) & 3u) != 1u) return;
    const auto bytes = Pm4::WaitAwaitedBytes(packet);
    const std::uint64_t address = (packet[2] & (bytes == 8 ? ~7u : ~3u)) | (static_cast<std::uint64_t>(packet[3]) << 32u);
    const auto passed = eventSerial.load(std::memory_order_acquire);
    std::lock_guard lock(labelStoresMutex);
    for (std::size_t offset = 0; offset < bytes; offset += 4) labelPasses[queueDword(queue, address + offset)] = passed;
}

void Driver::noteLabelIssue(std::span<const std::uint32_t> packet, std::uint32_t queue) {
    const auto take = [&](std::uint64_t address, std::uint64_t bytes) {
        for (std::uint64_t offset = 0; offset < bytes; offset += 4) {
            const auto key = queueDword(queue, address + offset);
            if (const auto pass = labelPasses.find(key); pass != labelPasses.end()) labelTaken[key] = pass->second;
        }
    };
    switch ((packet[0] >> 8u) & 0xffu) {
        case 0x37: case 0x49: {
            const auto label = Pm4::DecodeLabelWrite(packet);
            if (!label.has_value() || !KeepsLabelStore(label->address, label->Bytes().size())) return;
            std::lock_guard lock(labelStoresMutex);
            take(label->address, label->Bytes().size());
            return;
        }
        case 0x40: case 0x50: case 0x83: break;
        default: return;
    }
    auto stores = Pm4::DecodeStores(packet);
    std::erase_if(stores, [](const Pm4::PacketStore& store) { return store.conditional || !KeepsLabelStore(store.address, static_cast<std::size_t>(store.bytes)); });
    if (stores.empty()) return;
    std::lock_guard lock(labelStoresMutex);
    for (const auto& store : stores) take(store.address, store.bytes);
}

std::string Driver::describeLabelStores(std::uint64_t address, std::size_t bytes) {
    std::lock_guard lock(labelStoresMutex);
    std::string text;
    for (std::size_t offset = 0; offset < bytes; offset += 4) {
        if (offset != 0) text += "; stores at +" + std::to_string(offset) + " newest first:";
        const auto found = labelStores.find(address + offset);
        bool any = false;
        if (found != labelStores.end()) {
            for (const auto& store : found->second) {
                if (store.stamp == 0) continue;
                char item[96];
                std::snprintf(item, sizeof(item), "%s stamp %llu value 0x%x queue 0x%x opcode 0x%x", any ? "," : "", static_cast<unsigned long long>(store.stamp), store.value, store.queue, store.opcode);
                text += item;
                any = true;
            }
        }
        if (!any) text += " none recorded";
    }
    return text;
}

std::string Driver::describeSubmitCheck(const SubmitWaitCheck* check) {
    if (check == nullptr) return "no check at submission recorded";
    char text[128];
    switch (check->outcome) {
    case SubmitCheck::NotMemory:
        return "not checked at submission (not a memory wait)";
    case SubmitCheck::Inaccessible:
        return "not checked at submission (misaligned or inaccessible)";
    case SubmitCheck::EarlierWrite:
        return "not checked at submission (written earlier in the same submission)";
    case SubmitCheck::UnfinishedWrite:
        if (check->writerSerial != 0) std::snprintf(text, sizeof(text), "not checked at submission (queue 0x%x serial %llu writes it)", check->writerQueue, static_cast<unsigned long long>(check->writerSerial));
        else std::snprintf(text, sizeof(text), "not checked at submission (an unfinished queue 0x%x submission writes it)", check->writerQueue);
        return text;
    case SubmitCheck::Checked:
        std::snprintf(text, sizeof(text), "checked at submission: value 0x%llx", static_cast<unsigned long long>(check->value));
        return text;
    case SubmitCheck::Held:
        std::snprintf(text, sizeof(text), "held at submission: value 0x%llx", static_cast<unsigned long long>(check->value));
        return text;
    }
    return "unknown check at submission";
}

std::string Driver::describeWaitMoments(const SubmitWaitCheck* check) const {
    const auto nowMs = TraceMs();
    const auto now = static_cast<unsigned long long>(eventSerial.load(std::memory_order_acquire));
    const auto value = [](const WaitMoment& moment) {
        char text[32];
        if (moment.read) std::snprintf(text, sizeof(text), "0x%llx", static_cast<unsigned long long>(moment.value));
        else std::snprintf(text, sizeof(text), "unread");
        return std::string(text);
    };
    char text[512];
    if (check == nullptr) {
        std::snprintf(text, sizeof(text), "submission moments not recorded; now stamp %llu (+%.1f ms)", now, nowMs);
        return text;
    }
    char copied[192];
    if (check->copyRecorded) std::snprintf(copied, sizeof(copied), "%s at stamp %llu value %s (+%.1f ms)", check->rewindTail ? "tail copied" : "submitted by the title", static_cast<unsigned long long>(check->copied.stamp), value(check->copied).c_str(), check->copied.ms);
    else std::snprintf(copied, sizeof(copied), "%s", check->rewindTail ? "tail copy not recorded" : "submission copy not recorded");
    char kind[96];
    if (check->rewindTail) std::snprintf(kind, sizeof(kind), "rewind-tail, valid bit seen +%.1f ms", check->validSeenMs);
    else std::snprintf(kind, sizeof(kind), "submit");
    std::snprintf(text, sizeof(text), "%s, checked at stamp %llu value %s (+%.1f ms), kind %s; now stamp %llu (+%.1f ms)", copied, static_cast<unsigned long long>(check->checked.stamp), value(check->checked).c_str(), check->checked.ms, kind, now, nowMs);
    return text;
}

std::string Driver::describeLabelBlock(std::uint64_t address, const SubmitWaitCheck* check) {
    char text[448];
    const auto block = static_cast<unsigned long long>(address & ~std::uint64_t{0xffff});
    char atCheck[64];
    if (check != nullptr) std::snprintf(atCheck, sizeof(atCheck), "%llu", static_cast<unsigned long long>(check->writeGeneration));
    else std::snprintf(atCheck, sizeof(atCheck), "not recorded");
    const auto before = GuestMemory::StampsOf(address);
    const auto generation = static_cast<unsigned long long>(GuestMemory::TrackerGeneration());
    if (!before.watched) {
        std::snprintf(text, sizeof(text), "block 0x%llx: not watched, no write stamps; write generation at the check %s, tracker generation now %llu", block, atCheck, generation);
        return text;
    }
    GuestMemory::CollectWritesUncached(address, 4);
    const auto after = GuestMemory::StampsOf(address);
    char since[112];
    if (check != nullptr) std::snprintf(since, sizeof(since), "stamped after the check %s, a CPU store collected after the check %s", after.stamp > check->writeGeneration ? "yes" : "no", after.cpuStamp > check->writeGeneration ? "yes" : "no");
    else std::snprintf(since, sizeof(since), "no check to compare with");
    std::snprintf(text, sizeof(text), "block 0x%llx: watched; write generation at the check %s, tracker generation now %llu; stamp %llu cpu stamp %llu, after a collect stamp %llu cpu stamp %llu; %s", block, atCheck, generation, static_cast<unsigned long long>(before.stamp), static_cast<unsigned long long>(before.cpuStamp), static_cast<unsigned long long>(after.stamp), static_cast<unsigned long long>(after.cpuStamp), since);
    return text;
}

void Driver::describeWaitProducers(std::uint64_t address, std::size_t bytes) {
    std::string writers;
    std::vector<std::string> states;
    {
        std::lock_guard lock(mutex);
        const auto writes = [&](const std::vector<std::uint64_t>& dwords) {
            for (std::size_t offset = 0; offset < bytes; offset += 4) {
                if (std::find(dwords.begin(), dwords.end(), address + offset) != dwords.end()) return true;
            }
            return false;
        };
        char item[320];
        for (const auto& [queue, worker] : workers) {
            const auto executing = worker.executingSerial.load(std::memory_order_acquire);
            if (executing != 0 && executing == worker.runningSerial && writes(worker.runningWrites)) {
                std::snprintf(item, sizeof(item), "%s queue 0x%x serial %llu (running)", writers.empty() ? "" : ",", queue, static_cast<unsigned long long>(executing));
                writers += item;
            }
            std::size_t ahead = 0;
            for (const auto& pending : worker.pending) {
                if (writes(pending.labelWrites)) {
                    std::snprintf(item, sizeof(item), "%s queue 0x%x serial %llu (pending, %zu ahead of it)", writers.empty() ? "" : ",", queue, static_cast<unsigned long long>(pending.serial), ahead);
                    writers += item;
                }
                ++ahead;
            }
            std::string state;
            if (executing == 0) {
                if (worker.pending.empty()) state = queue == 0 && queue0Dormant.load(std::memory_order_relaxed) ? "idle (dormant)" : "idle";
                else {
                    std::snprintf(item, sizeof(item), "not executing, next pending serial %llu", static_cast<unsigned long long>(worker.pending.front().serial));
                    state = item;
                }
            } else {
                const auto packet = worker.executingPacket.load(std::memory_order_relaxed);
                if (packet == 0) std::snprintf(item, sizeof(item), "executing serial %llu before its first packet", static_cast<unsigned long long>(executing));
                else if (packet == AfterLastPacket) std::snprintf(item, sizeof(item), "executing serial %llu after its last packet", static_cast<unsigned long long>(executing));
                else std::snprintf(item, sizeof(item), "executing serial %llu at dword %llu opcode 0x%x", static_cast<unsigned long long>(executing), static_cast<unsigned long long>((packet >> 8u) - 1), static_cast<unsigned>(packet & 0xffu));
                state = item;
                if (queue == 0 || (queue >= 0x20 && queue < SubmissionQueueEnd)) {
                    const auto& slot = awaitedSlots[queue == 0 ? 0 : queue - 0x20 + 1];
                    const auto awaited = slot.address.load(std::memory_order_acquire);
                    const auto shape = slot.shape.load(std::memory_order_relaxed);
                    if (awaited != 0 && shape != 0) std::snprintf(item, sizeof(item), ", waiting on 0x%llx function %u ref 0x%llx mask 0x%llx", static_cast<unsigned long long>(awaited), static_cast<unsigned>(shape & 0xffffffffu), static_cast<unsigned long long>(slot.reference.load(std::memory_order_relaxed)), static_cast<unsigned long long>(slot.mask.load(std::memory_order_relaxed)));
                    else if (awaited != 0) std::snprintf(item, sizeof(item), ", waiting on the REWIND control word at 0x%llx", static_cast<unsigned long long>(awaited));
                    else item[0] = '\0';
                    state += item;
                }
            }
            std::snprintf(item, sizeof(item), "[gpu]   label 0x%llx queue 0x%x: %s; %zu pending", static_cast<unsigned long long>(address), queue, state.c_str(), worker.pending.size());
            states.emplace_back(item);
        }
    }
    std::fprintf(stderr, "[gpu]   label 0x%llx writers pending or running:%s\n", static_cast<unsigned long long>(address), writers.empty() ? " none" : writers.c_str());
    for (const auto& line : states) std::fprintf(stderr, "%s\n", line.c_str());
}

bool Driver::storedSince(std::span<const std::uint32_t> packet, std::uint64_t address, std::size_t bytes, std::uint64_t received, std::uint32_t queue, std::uint32_t* writer) {
    std::lock_guard lock(labelStoresMutex);
    const auto low = labelStores.find(address);
    if (low == labelStores.end()) return false;
    const auto high = bytes == 8 ? labelStores.find(address + 4) : labelStores.end();
    if (bytes == 8 && high == labelStores.end()) return false;
    std::uint64_t taken = 0;
    for (std::size_t offset = 0; offset < bytes; offset += 4) {
        if (const auto found = labelTaken.find(queueDword(queue, address + offset)); found != labelTaken.end()) taken = std::max(taken, found->second);
    }
    for (const auto& store : low->second) {
        if (store.stamp <= received || (store.queue != queue && store.stamp <= taken)) continue;
        std::uint64_t value = store.value;
        if (bytes == 8) {
            const auto upper = std::find_if(high->second.begin(), high->second.end(), [&](const LabelStore& other) { return other.stamp == store.stamp; });
            if (upper == high->second.end()) continue;
            value |= static_cast<std::uint64_t>(upper->value) << 32u;
        }
        if (Pm4::WaitComparesValue(packet, value)) {
            if (writer != nullptr) *writer = store.queue;
            return true;
        }
    }
    return false;
}

void Driver::traceLabel(std::span<const std::uint32_t> packet, std::uint32_t queue) {
    static const std::uint64_t watched = [] {
        const char* value = std::getenv("APS5_TRACE_LABEL");
        return value ? std::strtoull(value, nullptr, 16) : 0ull;
    }();
    const auto opcode = (packet[0] >> 8u) & 0xffu;
    std::uint64_t target = 0;
    const char* kind = nullptr;
    if (opcode == 0x49 && packet.size() >= 7) { target = packet[3] | (static_cast<std::uint64_t>(packet[4]) << 32u); kind = "RELEASE_MEM"; }
    else if (opcode == 0x37 && packet.size() >= 5) { target = packet[2] | (static_cast<std::uint64_t>(packet[3]) << 32u); kind = "WRITE_DATA"; }
    else if ((opcode == 0x3c || opcode == 0x93) && packet.size() >= 7) { target = (packet[2] & (Pm4::WaitAwaitedBytes(packet) == 8 ? ~7u : ~3u)) | (static_cast<std::uint64_t>(packet[3]) << 32u); kind = "WAIT_REG_MEM"; }
    else if (opcode == 0x40 && packet.size() >= 6) { target = packet[4] | (static_cast<std::uint64_t>(packet[5]) << 32u); kind = "COPY_DATA"; }
    std::uint64_t length = 4;
    if (opcode == 0x50 && packet.size() >= 7) {
        target = packet[4] | (static_cast<std::uint64_t>(packet[5]) << 32u);
        length = packet[6] & 0x3ffffffu;
        kind = "DMA_DATA";
    }
    if (kind == nullptr) return;
    if (std::string_view(kind) != "WAIT_REG_MEM") {
        static AgcDriver::Mutex historyMutex;
        std::lock_guard lock(historyMutex);
        auto& entry = writeHistory()[writeCursor()++ % writeHistory().size()];
        entry = {target, length, queue, opcode};
    }
    if (watched == 0 || target + length + 0x100 < watched || target > watched + 0x100) return;
    std::fprintf(stderr, "[label] %lld ms queue 0x%x %s 0x%llx:", static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()), queue, kind, static_cast<unsigned long long>(target));
    for (std::size_t i = 1; i < packet.size() && i < 9; ++i) std::fprintf(stderr, " %08x", packet[i]);
    std::fprintf(stderr, "\n");
}

std::array<WriteRecord, 16384>& Driver::writeHistory() {
    static std::array<WriteRecord, 16384> history{};
    return history;
}

std::size_t& Driver::writeCursor() {
    static std::size_t cursor = 0;
    return cursor;
}

}
