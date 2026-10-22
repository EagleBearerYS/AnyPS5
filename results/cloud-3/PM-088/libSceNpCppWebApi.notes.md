# libSceNpCppWebApi (164 todo: B-S 24, C 23, D 117)

What it is: the host copy of the C++ PSN Web API wrapper (Leaderboards V1, SessionManager V1, InGameCatalog V5, TitleCloudStorage V1, plus the Common templates `IntrusivePtr`, `Vector`, `String`, `Transaction`, `Iterator`). Per CONTRIBUTING, a module the title ships wins; this copy is used only when a title ships none (PPSA03671).

Offline model (`Export.cpp:9-14`, debt): request entry points (API calls, `TransactionBase::start`, `getResponse`, factories) return `SCE_NP_WEBAPI_ERROR_NOT_SIGNED_IN` 0x80552907 (shadPS4/fpPS4 tables). Parameter and transaction bookkeeping return 0 without state; these are silent stubs in the debt. Response accessors throw, because a failed request never produces a response.

`progress.py` counts a function as todo when it reaches `NotImplemented_nid_no_patch` through a helper. As a result, 34 rows already work offline and are counted only for an edge path:
- IntrusivePtr and Vector destructors through `ReleaseIntrusivePtr` (`:294`);
- `pushBack` through `VectorBodyFor` (`:834`);
- the `DestroyVectorBody` helper (`:817`).

Classes:
- C (23): the not yet instantiated InGameCatalog `ContainerApi::getContainer` and TitleCloudStorage `getMultiDataStatusesByUser` families.
  - Real behaviour (6): getContainer, getMultiDataStatusesByUser, two `start` and two `getResponse` return 0x80552907.
  - Silent stubs (17): ParameterTo* initialize/set/terminate/ctor/dtor, Transaction ctor/dtor and the two `finish` overrides return 0 as their siblings do. CONTRIBUTING allows these only with a title that imports them, and the debt records no call site.
- B-S (24): IntrusivePtr default and copy constructors, destructors, `operator->`/`operator*`, and two ConstIterator destructors, all copied from sibling instantiations over the 0x18-byte layout. The 8 destructors stay flagged through the same deleter-less path.
- D (117):
  - Response-body accessors (56) and Vector/Iterator reads, begin/end/[]/size (24): response layouts are unverified and no response exists offline.
  - Binary (2) and `ParameterToGetContainer::isInitialized` (1).
  - The 34 helper-flagged rows: the last release of a pointee without a deleter needs its destructor and the `LibContext` allocator. Appending to a title-provided Vector body needs its element layout. Both are known only from the module (PPSA21564).

Third-party: none.

Batch proposals:
1. "cppwebapi: IntrusivePtr plumbing for InGameCatalog V5 and TitleCloudStorage V1" (24 B-S). Test: extend `guest_np_cpp_web_api_intrusive_ptr_tests` (`core/libs/CMakeLists.txt:588`):
   - copy ctor and dtor change the count at 0x00/0x30;
   - the default ctor is null;
   - `operator->`/`operator*` return the object;
   - the dtor calls the deleter on the last reference.
2. "cppwebapi: offline ContainerApi/getMultiDataStatusesByUser" (23 C). Only with a title that imports them (an `Unblocks:` line). Test: a guest test asserting 0x80552907 from the API call, `start` and `getResponse`, and 0 from the parameter calls.
