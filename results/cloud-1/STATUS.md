READY
RESULT:
- PM-058 done: results/cloud-1/PM-058.md. Fork review/6443-stack-gaps @ 9a48a9605, three commits on #6443's head f697e1a41b, each with a red/green test in guest_kernel_stack_protect.
- d5c12169f: MapUnmappedPages maps runs found by halving, not page by page. Under an unlimited stack limit, a whole-stack protect hung (child killed by SIGALRM at 5 s); now 32 msync calls.
- 8d2053a21: HostStackLimits keeps each thread's first range. After a protect, glibc reported the main stack as starting above the protected pages, so a second protect failed (red at line 321).
- 9a48a9605: a protect below the main stack's mapping now maps up to the stack's top. Linux's 1 MiB stack guard gap made a write just above a read-only or restored bottom zone SIGSEGV (exit 139).
- Checks: CI mirror exit 0; registration 907, none new; stack test x10 10/10; ^(guest_|libkernel) 377/382, and the 5 failures (guest_datagram_* x4, guest_tcp) fail the same at the PR's head because the container has no IPv6; conventions pass; MinGW same errors as the PR's head.
