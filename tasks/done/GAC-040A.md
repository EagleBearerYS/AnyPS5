# GAC-040A: addendum to GAC-040 (for cloud-2): the 30 log lines before the heap-mirror abort
from: Game Agent Coordinator
for: cloud-2
priority: high

These are from Resident Evil Village's fast-tree boot (qfW-full.log 63498-63528), the 30 lines before the abort. They're the trigger evidence for GAC-040; mark this done together with it.

- 63498-63499: StorageTexture upload submit/done
- 63500: `Texture address=0x2081c00000 160x90 mips=1 viewed from storage image (vk=97)`
- 63501: `[gpu] sampled texture cache budget 2048 MiB (was 2981 MiB): device-local heap 0 has a VK_EXT_memory_budget budget of 7250 MiB and uses 3367 MiB, 329 MiB of it sampled textures (327 MiB in the cache) and 686 MiB cached storage imag…`
- 63502: `StorageTexture address=0x2083c00000 160x90 mips=1 mip=0 layers=63 base=0 dim=4 tile=7 format=71 vk=97`
- 63503-63504: upload submit/done
- **63505:** `[gpu] host import budget: 3752 MiB live of 4480 MiB (APS5_HOST_IMPORT_MIB); 85047 imports (115203 MiB) refused so far, 9691 of them remembered, last 0x230200000+0x30100000 by build`
- 63506-63525: storage uploads; a 1x1 storage texture at 0x2077000000
- 63526-63527: `Texture address=0x21d9400000 32x32 mips=1 layers=32 dim=4 tile=3 format=130 ...`
- **63528:** `[gpu] driver failure: compute shader 0x207bc35b00: AGC graphics: a heap mirror's range is no longer write-watched`

**Reading** (the coordinator's, unproven):
- The budget is **4480 MiB, not the configured 6400**. #6479 (F3) lowered it to the device's `VK_EXT_memory_budget` report.
- Under that pressure the build refused the title's ~770 MiB heap (0x230200000+0x30100000), so the heap got a heap mirror. Something then took part of the heap's range off the write watch: an eviction, or an earlier import of part of it that isn't settled or watched, or a block import (#6457).
- So: check budget pressure plus F3 plus #6457 first.
- **A Linux repro idea:** a heap over the budget, with a part of its range imported and then evicted, or imported unwatched (`APS5_WRITE_WATCH_IMPORTS=unwatch` as Windows' stand-in), then an address-based build.

claimed-by: cloud-2 2026-10-15T20:58:19Z
done: 2026-10-15T21:29:38Z, result: results/cloud-2/GAC-040.md (section 1.5)
