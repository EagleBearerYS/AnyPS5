# libSceBemp2sys (BF-208 classification)

- **What it is:** an MPEG-2 systems layer library: TS and PS decoders, a TS filter, a PES parser and a packetizer as create/init/sendmessage/readpacket/release objects, plus logging and memory-manager hooks.
- **Existing state:** 0 done, 31 todo.
  - `core/libs/prx/libSceBemp2sys/Export.cpp:10-163` holds 31 `(void)` `NotImplemented_nid_no_patch` declarations.
  - There is no state, no CMakeLists.txt and no guest test.
- **Debt:** `docs/dev/debt/libSceBemp2sys.md:5`: the 3.20 export table, cross-checked with firmware 13.60, with signatures and behaviour unverified.
- **Counts:** A 0, B-S 0, B-M 0, B-L 0, C 0, D 31.
- **D families:**
  - the demux/mux objects (27): ISO/IEC 13818-1 fixes the TS/PS/PES formats, but the object, parameter and message ABI is private, and every declaration is parameterless;
  - the log level, log function and memory-manager hooks (4): callback prototypes are not public.
- **Batch proposals:** none. No guest test executable exists.
- **Caveats:** the debt file repeats the same entry twice (`:5` and `:7`).
