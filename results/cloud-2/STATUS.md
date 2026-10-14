READY
RESULT:
- Last task: PM-003 (performance overlay), done: results/cloud-2/PM-003.md; branch feat/videoout-performance-overlay on the fork (head 98b9b57ef). Before it, PRM-002: results/cloud-2/PRM-002.md.
- Tree kept for follow-ups: /home/user/AnyPS5/build at feat/videoout-performance-overlay 98b9b57ef (upstream e34c47921 + 3 commits), built with CI's Linux configure flags and recursive submodules (main needs them for 3rdparty/mbedtls/framework).
- Machine: host vm, Intel Xeon @ 2.10 GHz, 4 vCPU (no SMT, AVX2/AVX-512), 15 GiB RAM, no swap, about 27 GB disk free. Ubuntu 24.04.5, kernel booted with ipv6.disable=1, no udmabuf.
- Toolchain: gcc/g++ 13.3.0, CMake 3.28.3, Ninja 1.11.1, ccache 4.9.1, Python 3.13.16, git 2.43.0, lavapipe from mesa-vulkan-drivers 25.2.8 (LLVM 20.1.2), libvulkan1 1.3.275; Xvfb and vulkan-validationlayers 1.3.275 for display tests.
- Build times: cold full build 1105 s; incremental 446-1018 s depending on what changed; no-op rebuild about 1 s.
- Tests run as an unprivileged user, as on CI.
- Full-suite baseline (CI's ctest step) on upstream main a453108cf7: 1170 tests, 1107 passed, 58 skipped, 5 failed; on the PM-003 branch: 1176 tests, 1113 passed, 58 skipped, 5 failed. The 5 failures are guest_datagram_recv, guest_datagram_recvfrom, guest_datagram_sceNetRecv, guest_datagram_sceNetRecvfrom and guest_tcp, all from IPv6 being off; check_skips also flags guest_sce_net for the same reason. guest_avplayer is intermittent on the other workers but has not failed here. Anything beyond these is a real change.
- Remotes: upstream = boykopovar/AnyPS5 (fetch only, push disabled); origin and fork = EagleBearerYS/AnyPS5.
- An hourly check-in (trig_01HhjrdD6JFV26ZRvjq2Jzbt, about :35 UTC) restarts the board watcher if the VM is recycled while idle.
