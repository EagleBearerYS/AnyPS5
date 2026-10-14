READY
RESULT:
- Last task: PRM-002, done: results/cloud-2/PRM-002.md.
- Tree kept for follow-ups: /home/user/AnyPS5/build at fdadf20a9e (local branch prm-002 = upstream a453108cf7 + #5688 + #5703 + #5815), built with CI's Linux configure flags and recursive submodules (main now needs them for 3rdparty/mbedtls/framework).
- Machine: host vm, Intel Xeon @ 2.10 GHz, 4 vCPU (no SMT, AVX2/AVX-512), 15 GiB RAM, no swap, about 28 GB disk free. Ubuntu 24.04.5, kernel booted with ipv6.disable=1.
- Toolchain: gcc/g++ 13.3.0, CMake 3.28.3, Ninja 1.11.1, ccache 4.9.1, Python 3.13.16, git 2.43.0, lavapipe from mesa-vulkan-drivers 25.2.8 (LLVM 20.1.2), libvulkan1 1.3.275.
- Build times: cold full build 1105 s; incremental 1018 s across 417 upstream commits, 598 s for three merged PRs; no-op rebuild about 1 s.
- Tests run as an unprivileged user, as on CI.
- Full-suite baseline on upstream main a453108cf7, 1170 tests: 1107 passed, 58 skipped, 5 failed. The failures are guest_datagram_recv, guest_datagram_recvfrom, guest_datagram_sceNetRecv, guest_datagram_sceNetRecvfrom and guest_tcp, all from IPv6 being off; check_skips also flags guest_sce_net for the same reason. guest_avplayer is intermittent on the other workers but has not failed here. Anything beyond these is a real change.
- Remotes: upstream = boykopovar/AnyPS5 (fetch only, push disabled); origin and fork = EagleBearerYS/AnyPS5.
- An hourly check-in (trig_01HhjrdD6JFV26ZRvjq2Jzbt, about :30 UTC) restarts the board watcher if the VM is recycled while idle.
