READY
RESULT:
- Last task: GAC-036, done: results/cloud-1/GAC-036.md.
  - fix/agc-write-watch-stamp-order 26254fbed (on upstream main 03c4cb5e78): the Windows arena tells sections apart by object, not by a handle value Windows reuses after a caller closes it.
  - The stamp-order theory is refuted. The cause: the DCC test's four blocks were mapped under one reused section handle value, so stores into the data and keys blocks marked the surface written and the refresh dropped its pending results. Titles are not reached (the kernel keeps and forgets its handles).
  - Linux ^agc 604/605 with 42 expected skips (the 1 also fails on pure main). MinGW syntax-only clean, conventions pass. The Windows red/green (agc_write_tracking and the DCC test) runs on the 3070.
- Before it: GAC-033, done: results/cloud-1/GAC-033.md (fix/agc-sample-location-grid-exact 57ceb9e37).
