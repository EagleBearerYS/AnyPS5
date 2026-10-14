#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_PIPELINECREATION_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_EXECUTION_INCLUDE_PIPELINECREATION_HPP

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <utility>

namespace AgcDriver {

class RecordUse {
public:
    explicit RecordUse(std::shared_ptr<std::atomic<std::uint64_t>> uses) : uses(std::move(uses)) {}
    RecordUse(const RecordUse&) = delete;
    RecordUse& operator=(const RecordUse&) = delete;

    void Mark() noexcept {
        if (order.load(std::memory_order_relaxed) != 0) return;
        std::uint64_t unused = 0;
        if (order.compare_exchange_strong(unused, uses->fetch_add(1, std::memory_order_relaxed) + 1, std::memory_order_relaxed)) Marks().fetch_add(1, std::memory_order_relaxed);
    }

    std::uint64_t Order() const noexcept { return order.load(std::memory_order_relaxed); }
    static std::atomic<std::uint64_t>& Marks();

private:
    std::shared_ptr<std::atomic<std::uint64_t>> uses;
    std::atomic<std::uint64_t> order{0};
};

class CreationAhead {
public:
    CreationAhead() : previous(Flag()) { Flag() = true; }
    ~CreationAhead() { Flag() = previous; }
    CreationAhead(const CreationAhead&) = delete;
    CreationAhead& operator=(const CreationAhead&) = delete;
    static bool Active() { return Flag(); }

private:
    static bool& Flag();

    bool previous;
};

class CreationThread {
public:
    static constexpr std::size_t RetainedBudgetBytes = std::size_t{1} << 30u;
    static constexpr std::size_t RetainedBytesPerWord = 567;

    static std::size_t Words();

    template<typename TCreate>
    static auto Run(std::size_t words, TCreate&& create) -> decltype(create()) {
        if (!CreationAhead::Active() || words < Words() || Current()) return create();
        using Result = decltype(create());
        std::promise<Result> done;
        auto result = done.get_future();
        Submit([&] {
            try {
                done.set_value(create());
            } catch (...) {
                done.set_exception(std::current_exception());
            }
        });
        return result.get();
    }

    static bool Current();
    static std::uint64_t Routed();

private:
    static void Submit(std::function<void()> task);
};

class CreationGate {
public:
    static constexpr std::size_t WordsInFlight = std::size_t{1} << 20u;

    explicit CreationGate(std::size_t words) : words(words) {
        auto& gate = State();
        std::unique_lock lock(gate.mutex);
        if (CreationAhead::Active()) {
            const auto ticket = gate.issued++;
            gate.changed.wait(lock, [&] { return ticket == gate.admitted && (gate.inFlight == 0 || gate.inFlight + words <= WordsInFlight); });
            ++gate.admitted;
        }
        gate.inFlight += words;
        ++gate.creations;
        lock.unlock();
        gate.changed.notify_all();
    }

    ~CreationGate() {
        auto& gate = State();
        {
            std::lock_guard lock(gate.mutex);
            gate.inFlight -= words;
            --gate.creations;
        }
        gate.changed.notify_all();
    }

    CreationGate(const CreationGate&) = delete;
    CreationGate& operator=(const CreationGate&) = delete;

    static std::size_t InFlight() {
        auto& gate = State();
        std::lock_guard lock(gate.mutex);
        return gate.inFlight;
    }

    static std::size_t Creations() {
        auto& gate = State();
        std::lock_guard lock(gate.mutex);
        return gate.creations;
    }

private:
    struct Shared {
        std::mutex mutex;
        std::condition_variable changed;
        std::uint64_t issued = 0;
        std::uint64_t admitted = 0;
        std::size_t inFlight = 0;
        std::size_t creations = 0;
    };

    static Shared& State() {
        static Shared shared;
        return shared;
    }

    std::size_t words;
};

}

#endif
