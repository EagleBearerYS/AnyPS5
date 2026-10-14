READY
RESULT:
- Last task: PRM-003 (#5703 + #5815 + #5851 on main a453108cf7), done: results/cloud-1/PRM-003.md. Before it, PM-002 and BF-002.
- Trees kept for follow-ups, each built with CI's Linux configure flags:
  - /home/user/AnyPS5/build at upstream a453108cf7
  - /home/user/trees/prm-003/build at prm-003 de032afb91
  - /home/user/trees/{B,C,D}/build-{B,C,D}
  - one shared ccache
- Machine: host vm, Intel Xeon @ 2.80 GHz, 4 cores (no SMT, AVX2/AVX-512), 15.7 GiB RAM, no swap, about 23 GB disk free. Ubuntu 24.04.5, kernel 6.18 booted with ipv6.disable=1 and without udmabuf.
- Toolchain: gcc/g++ 13.3.0, binutils 2.42, glibc 2.39, CMake 3.28.3, Ninja 1.11.1, ccache 4.9.1, Python 3.13.16, git 2.43.0, lavapipe from mesa-vulkan-drivers 25.2.8 (LLVM 20.1.2), libvulkan1 1.3.275.
- Build times: cold full build 1643-1876 s; with ccache 376-821 s for a tree that changes AGC driver headers; no-op rebuild about 1 s.
- Tests run as an unprivileged user, as on CI; as root, permission tests behave differently.
- Full-suite baseline on upstream main, 1110 tests:
  - 5 failures from IPv6 being off: guest_datagram_recv, guest_datagram_recvfrom, guest_datagram_sceNetRecv, guest_datagram_sceNetRecvfrom, guest_tcp
  - guest_sce_net skipped (IPv6 checks)
  - guest_avplayer intermittent, 1 failure in 3 full runs ("enable video failed")
  - anything beyond these is a real change
- Remotes: upstream = boykopovar/AnyPS5 (fetch only, push disabled); origin and fork = EagleBearerYS/AnyPS5.
