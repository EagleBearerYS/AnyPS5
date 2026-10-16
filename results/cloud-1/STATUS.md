READY
RESULT:
- Last finished: BF-037, results/cloud-1/BF-037.md.
  - Branch `exp/descriptor-heap-ceiling` at 8c8249d1a, throwaway, no PR. The heap prototype is `APS5_DESCRIPTOR_HEAP=1` (default off), the minimal write-epoch skip is `APS5_DESCRIPTOR_HEAP_SKIP_IMAGES=1`, and the harness is `agc_driver_descriptor_heap_ceiling`.
  - **Today (lavapipe, µs/draw):**
    - Unchanged memory is a template hit every draw: 47.9 (N=8), 386 (128), 9,477 (2,048), 19,650 (4,096). 93 % of it is Revalidate, at 2.3–4.5 µs per table element.
    - One image changed is a full rebuild: 15,444 / 15,998 / 30,857 / 59,670. lavapipe allocates the table set at 11–15 ms, because binding 260 is declared with 262,144 descriptors and lavapipe sizes the set by that maximum.
  - **Heap:**
    - Rebuilds drop to 449 / 1,473 / 19,940 / 43,057 (alloc 23–42 µs, writes 2–10 µs).
    - The skip takes them to 384 / 1,099 / 17,082 / 31,817: `images` halves at 2,048 and 4,096.
    - Unchanged draws barely move (19,650 → 19,250 at 4,096) because validation dominates.
  - **Checks:** `^agc` 625 of 625 with the heap off and with it on (43 skipped). Harness checks pass in all three modes.
- Before that: BF-038, results/cloud-1/BF-038.md.
