# libSceUlt (22 todo: A 0, B-S 1, B-M 0, B-L 0, C 0, D 21)

Upstream main 2742004ed2.

- **What it is:** the user-level threads runtime: waiting-queue and queue-data resource pools, mutexes, condition variables, semaphores, reader-writer locks, queues, ulthread runtimes and ulthreads, plus per-object diagnostic getters (`sceUltGet*Info`) and a C++-style thread-factory interface (Abstract/Kernel/Ulthread factories, Module init/finalize).
- **Existing state:** 66 done.
  - Host objects keyed by the guest object address: `gMutexes`, `gConditions`, `gSemaphores`, `gResourcePools`, `gQueueDataPools`, `gQueues`, `gRuntimes`, `gUlthreads` (`core/libs/prx/libSceUlt/Export.cpp:28-35`); the reader-writer locks live in `src/ReaderWriterLock.cpp:14`.
  - State structs in `SceUltTypes.hpp:268-345`. Each ulthread is a host thread (`Export.cpp:794`, run by `ulthreadRunner` `:122`), not a fiber on a worker.
  - Condition variables bind to their creation mutex and keep a FIFO waiter list (`UltConditionState::_queue`, `SceUltTypes.hpp:288`); `sceUltConditionVariableSignal` (`Export.cpp:413`) wakes the first waiter.
  - Every diagnostic layout is defined and pinned with `static_assert`s (`SceUltTypes.hpp:47-266`); debt `docs/dev/debt/libSceUlt.md:10` says "Diagnostic signatures and synchronization argument lists are recovered", and that the non-underscore factory exports are absent from the supplied module with unresolved signatures.
  - Names passed to every create call are discarded (`Export.cpp:246, 379, 484, 545, 589, 653, 782`, `src/ReaderWriterLock.cpp:82`).
- **Counts:** A 0, B-S 1, B-M 0, B-L 0, C 0, D 21.
- **B-S (1):** `sceUltConditionVariableSignalAll`. The broadcast form of Signal, as POSIX `pthread_cond_broadcast` is of `pthread_cond_signal`: take the same lookup and NULL/STATE answers as `Export.cpp:413`, then pop and wake every queued waiter. Each waiter reacquires the mutex through the existing reacquire loop in `sceUltConditionVariableWait` (`Export.cpp:432`). No new state.
- **D families (21):**
  - Diagnostic getters (9): Get{ConditionVariable, Mutex, QueueDataResourcePool, Queue, ReaderWriterLock, Semaphore, Ulthread, UlthreadRuntime, WaitingQueueResourcePool}Info.
    - The signatures and layouts are in the tree, but no public source describes these PS5 getters; the one the debt cites, the PS Vita libult in RPCS3 (`:12`), covers the lock and ulthread signatures only. What a getter does with the `size` field, its error precedence, and the codes of `ownerType`, `lockType`, `threadType` and `waitValue` (`SceUltTypes.hpp:50-51, :85, :128, :131`) would all be guesses.
    - Several fields have no source of truth in the host model: names are dropped at create; the mutex owner is a host `std::thread::id` (`SceUltTypes.hpp:271`); queues count waiters as one `_waiters` (`SceUltTypes.hpp:327`) rather than pushers and poppers; ulthreads have no guest context or fiber record and runtimes have no workers (debt `:7`); waiting-queue pool node accounting is not implemented (debt `:16`).
    - The closest to tractable are QueueDataResourcePoolInfo and SemaphoreInfo, whose counts already exist in `UltQueueDataPoolState` (`SceUltTypes.hpp:308`) and `UltSemaphoreState` (`SceUltTypes.hpp:293`).
  - Thread-factory interface (10): AbstractThreadFactory CreateThread/GetNumMaxThreads/JoinThread, KernelThreadFactory Initialize/Finalize/GetFactoryPointer, UlthreadFactory Initialize/Finalize/GetFactoryPointer/GetWorkAreaSize. Parameterless, signatures unresolved (debt `:10`), and there is no factory object in the tree.
  - Module lifecycle (2): `sceUltModuleInitialize` and `sceUltModuleFinalize`. Parameterless with no prototype; nothing says whether they are `sceUltInitialize` (`Export.cpp:139`) and `sceUltFinalize` (`Export.cpp:143`) under other NIDs or a module-level step.
- **Batch proposal:** none, because the only A/B/C family is one row. SignalAll can go in alone as `feat(libSceUlt): broadcast condition variables`.
  - Test: extend `guest_ult_condition_tests` (`core/libs/tests/GuestUltCondition.cpp`, built at `core/libs/prx/libSceUlt/CMakeLists.txt:6`, registered per case at `:11` with `add_test(NAME guest_ult_condition_${testCase} ...)`) with a `signal-all` case: three waiters on one condition all return 0 after one SignalAll and each holds the mutex in turn; SignalAll with no waiters returns 0; null returns `ULT_ERROR_NULL`; a destroyed condition returns `ULT_ERROR_STATE`.
- **Caveats:**
  - FIFO signal selection is itself unmeasured (debt `:5`); SignalAll waking all removes that question for the broadcast.
  - `core/libs/tests/GuestUltThread.cpp:89-96` asserts that `sceUltGetConditionVariableInfo` throws; any getter implementation must change that test.
  - The getter rows become in-tree B-M work (store names, split queue waiters, waiter records) once the field contract is recovered from the same supplied module the layouts came from.
