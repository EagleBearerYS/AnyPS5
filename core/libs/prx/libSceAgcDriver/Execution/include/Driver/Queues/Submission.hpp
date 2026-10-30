#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_DRIVER_SUBMISSION_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_DRIVER_SUBMISSION_HPP

#include "prx/libSceAgcDriver/Execution/include/Driver/Shaders/ShaderRegistry.hpp"
#include "prx/libSceAgcDriver/Execution/include/VideoOutput.hpp"
#include "prx/libSceAgcDriver/Execution/include/Driver/Queues/SubmissionProgress.hpp"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace AgcDriver::DriverDetail {

enum class SubmitCheck : std::uint8_t { NotMemory, Inaccessible, EarlierWrite, UnfinishedWrite, Checked, Held };

struct WaitMoment {
    double ms = 0;
    std::uint64_t stamp = 0;
    std::uint64_t value = 0;
    bool read = false;
};

struct WaitCopy {
    WaitMoment moment;
    std::uint64_t forgetSerial = 0;
    std::uint64_t accessSerial = 0;
};

struct SubmitWaitCheck {
    SubmitCheck outcome = SubmitCheck::NotMemory;
    std::uint32_t writerQueue = 0;
    std::uint64_t writerSerial = 0;
    std::uint64_t value = 0;
    bool copyRecorded = false;
    WaitMoment copied;
    WaitMoment checked;
    std::uint64_t writeGeneration = 0;
    bool rewindTail = false;
    double validSeenMs = 0;
};

struct Submission {
    std::uint64_t serial;
    std::uint32_t queue;
    std::vector<std::uint32_t> commands;
    std::shared_ptr<SubmissionProgress> progress;

    std::shared_ptr<const ShaderRegistry> shaders;
    std::map<std::size_t, std::shared_ptr<IFlipRequest>> flips;
    std::map<std::size_t, std::shared_ptr<IRenderingWait>> renderingWaits;
    std::map<std::size_t, std::size_t> conditionalEnds;
    std::map<std::size_t, std::vector<std::uint32_t>> registerLists;
    bool suspend = false;
    bool waitFree = false;
    bool holdsFlip = false;

    std::uint64_t received = 0;
    std::vector<std::uint64_t> labelWrites;
    std::set<std::size_t> heldAtSubmit;
    std::vector<std::pair<std::size_t, SubmitWaitCheck>> waitChecks;
    std::vector<std::pair<std::size_t, WaitCopy>> waitCopies;
    double waitCopyMs = -1;
    double waitCheckMs = -1;
    bool rewindTailCopy = false;
    double rewindValidSeenMs = 0;
    std::chrono::steady_clock::time_point receivedAt{};
    std::chrono::steady_clock::time_point copiedAt{};
    std::chrono::steady_clock::time_point validatedAt{};
    std::chrono::steady_clock::time_point roomReadyAt{};
    std::chrono::steady_clock::time_point enqueuedAt{};
    std::chrono::steady_clock::time_point dequeuedAt{};
    std::chrono::steady_clock::time_point orderedAt{};
    const std::uint32_t* rewindTail = nullptr;
    std::size_t rewindWords = 0;
};

inline constexpr std::uint64_t AfterLastPacket = ~std::uint64_t{0};

struct QueueWorker {
    std::deque<Submission> pending;

    std::atomic<std::uint64_t> queued{0};
    std::unordered_map<std::uint64_t, std::uint32_t> unfinishedWrites;
    std::uint64_t executingWaitFree = 0;
    bool executingWaits = false;
    std::uint64_t runningSerial = 0;
    std::vector<std::uint64_t> runningWrites;
    std::atomic<std::uint64_t> executingSerial{0};
    alignas(64) std::atomic<std::uint64_t> executingPacket{0};
    std::thread thread;
};

}

#endif
