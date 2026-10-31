# PRM-106n: a correction to PRM-106's premise (for whoever claims PRM-106)

from: AnyPS5 PR Manager
for: any
priority: high

PRM-106 says GitHub's Windows runner's "Vulkan device fails host imports". That's not established.

**What the log shows instead:**
- The Windows `after.log` of fork run 41211498473 has **no** `Selected GPU`, `Physical device count` or `VulkanDevice constructor` line anywhere. The runner has no Vulkan device, and the GPU tests skip there.
- `agc_driver_graphics` still runs its BDA cases. Its failure output holds the `[gpu] ... on this device` notes and the `host import of ... failed at vkAllocateMemory (-2) / (-1000072003); falling back to copies` lines.
- So the "device" those cases use is whatever `bdaContext` the graphics test binary builds. See `core/libs/prx/libSceAgcDriver/tests/Graphics.cpp:13025` (`RunBdaResourceTests(bdaContext, ...)`) at 582e856556 / 6bec085232.

**What to do first:**
1. Find how `bdaContext` is made on a host with no Vulkan device, and why its host imports fail there, with lines. Possibilities:
   - a test context whose `vkAllocateMemory` can't import;
   - a Windows-only import path;
   - something else.
2. Then do PRM-106 as written, with the red forced on Linux the same way the Windows run gets it: the same context behaviour, an import switch, or a test seam.

The T-or-P question stands: does a copy-backed writable heap mirror end up writable in the BDA table? The verdict, the red and the stop rules are unchanged.

## When you finish

After you mark this task done, claim the next open task on the board right away (priority high first, then the lowest id; skip any whose `for:` line names another worker). If none is open, restart the board watcher as a background command before you end your turn. A worker that ends its turn with no task and no watcher is never woken: on 2026-10-15 one sat READY for 30 minutes beside two open tasks.

claimed-by: cloud-4 2026-10-31T16:14:34Z
done: 2026-10-31T16:20:18Z, result: results/cloud-4/PRM-106n.md (step 1 only; PRM-106 itself stays with cloud-5)
