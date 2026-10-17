BUSY PRM-032 (PR 5/8: #6443; #6926, #6944, #6911, #6641 clean)
since: 2026-10-17T05:03:31Z
last: BF-044 done, results/cloud-1/BF-044.md: 3a1faef0a5 mirror 1208/1279 (9 failed: 5 IPv6, 1 root-only, 3 old-base pthread segfaults that pass on the merge), reuse tests 20/20 each, reuse-off skip as expected, merge with main 2083bc700f clean and ^agc 636/636; before that BF-043
RESULT:
- Host: `vm`, Intel Xeon @ 2.80 GHz, 4 vCPUs (1 thread/core), 15 GiB RAM, no swap, 30 GB free disk allowance. Ubuntu 24.04.5 LTS x86_64, glibc 2.39.
- Toolchain: gcc/g++ 13.3.0, CMake 3.28.3, Ninja 1.11.1, ccache 4.9.1 (apt), Python 3.13.16, wild 0.10.0 (CI's sha256-checked release, used through -B as in CI). Vulkan: lavapipe, Mesa 25.2.8-0ubuntu0.24.04.4 (LLVM 20.1.2), API 1.4.318.
- Packages: apt-get install --no-install-recommends ccache libvulkan1 mesa-vulkan-drivers vulkan-tools libpipewire-0.3-dev libpulse-dev libasound2-dev libxrandr-dev libwayland-dev libxkbcommon-dev libegl-dev libdecor-0-dev (the Linux list from release.yml; build.yml's Linux job runs on a self-hosted runner with no apt line).
- Repo: /home/user/AnyPS5, detached at upstream main e1ac3300d, submodules recursive. origin = EagleBearerYS/AnyPS5, upstream = boykopovar/AnyPS5 (fetch only).
- Mirror script /home/user/ci-linux.sh: build.yml's linux job (CONFIGURE + -DANYPS5_ENABLE_SPIRV_TOOLS=ON + wild linker flags; CCACHE_DIR/MESA_SHADER_CACHE_DIR under /var/cache/anyps5; ctest with ANYPS5_REQUIRE_VULKAN=1 GALLIUM_OVERRIDE_CPU_CAPS=avx --test-load 4 --timeout 120; check_skips.py; 30 s slow report).
- Tree /home/user/AnyPS5/build: configure 30 s; check_test_registration "929 test executable(s), 0 built and not run as recorded, none new"; cold build 1689 s (3872 steps, ccache 7% hits); --target libs 0 s (already built).
- ctest: 1322/1328 passed, 908 s, first run with an empty Mesa shader cache. Failed: guest_datagram_recv, guest_datagram_recvfrom, guest_datagram_sceNetRecv, guest_datagram_sceNetRecvfrom, guest_tcp (no IPv6, the known baseline), guest_named_shm_concurrency ("unlink of a read-only shared object did not return EACCES").
- check_skips.py: exit 1 on guest_sce_net (no IPv6 loopback, known) and savedata_memory_growth_read_failure ("Read denial unavailable for this user").
- Root baseline: this container runs everything as root (creating a runner user was refused). Rerun as nobody via setpriv, guest_named_shm_concurrency and savedata_memory_growth_tests read-failure both exit 0, so these two are root-only, not code faults.
- Slow report: agc_driver_flat_store 63.9 s on the cold shader cache.
- Relinker-only tree build-relinker: configure 1 s, cold build 103 s, ctest 186/186 passed in 79.4 s.
