BUSY PM-009
RESULT:
- Repo: /home/user/AnyPS5, HEAD main f74ec639c (= upstream main at setup), submodules recursive at depth 1. fork/main was f40b6930f. Setup committed and pushed nothing.
- Packages: apt-get install --no-install-recommends libvulkan1 mesa-vulkan-drivers libpipewire-0.3-dev libpulse-dev libasound2-dev libxrandr-dev ccache
- Configure: cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DANYPS5_ENABLE_SPIRV_TOOLS=ON -> 27 s
- Test registration check: python3 tools/check_test_registration.py build -> "818 test executable(s), 0 built and not run as recorded, none new"
- Build: cmake --build build -> [3419/3419], 1199 s, 0 errors, 12 warning lines (mostly GCC 13 false positives in libstdc++ and glslang). cmake --build build --target libs -> 0 s.
- Tests (as runner): ANYPS5_REQUIRE_VULKAN=1 GALLIUM_OVERRIDE_CPU_CAPS=avx MESA_SHADER_CACHE_DIR=$PWD/.mesa-shader-cache ctest --test-dir build --output-on-failure --timeout 120 --parallel 4 --output-junit ctest.xml -> "99% tests passed, 6 tests failed out of 1110", 118.9 s.
  - Counts: 1057 passed, 47 skipped, 6 failed.
  - IPv6 failures: guest_datagram_recv, guest_datagram_recvfrom, guest_datagram_sceNetRecv, guest_datagram_sceNetRecvfrom ("create UDP sockets"); guest_tcp (Subprocess aborted).
  - Intermittent: guest_avplayer ("enable video failed").
- Skip check: check_skips.py -> "error: guest_sce_net skipped" ("skipped the IPv6 socket checks, the host has no usable IPv6 loopback").
- Tests over 30 s: none with a warm shader cache. On the cold first run: agc_driver_flat_store 93.5 s, agc_driver_global_vcc_base 35.7 s, agc_driver_flat_apertures_2 31.5 s.

Machine: host vm, Intel Xeon @ 2.80 GHz, 4 cores (no SMT, AVX2/AVX-512), 15.7 GiB RAM, no swap, about 28 GB disk free. Ubuntu 24.04.5, kernel 6.18 booted with ipv6.disable=1.
Toolchain: gcc/g++ 13.3.0, binutils 2.42, glibc 2.39, CMake 3.28.3, Ninja 1.11.1, ccache 4.9.1, Python 3.13.16, git 2.43.0, lavapipe from mesa-vulkan-drivers 25.2.8 (LLVM 20.1.2), libvulkan1 1.3.275.
Build times: cold full build 1199 s (empty ccache); rebuild with no changes about 1 s; any checkout that moves HEAD about 21 s (370 relinks, a generated header embeds the commit). Full test run 119 s with a warm Mesa shader cache, 328 s cold.
Baseline: the 6 failures and the guest_sce_net skip above are this machine's baseline (5 failures and the skip from IPv6 being off in the kernel, guest_avplayer intermittent under parallel load: 1 of 3 full runs, 15 of 15 alone). Anything beyond them is a real change.
Remotes: upstream = boykopovar/AnyPS5 (fetch only, push disabled locally); origin and fork = EagleBearerYS/AnyPS5. The environment resets origin to the fork whenever the container resumes, so upstream is the remote named upstream here, never origin.
Current tree: build/ is at test/fsr-on-overlay f4b98048a (PM-007: upstream e34c47921 plus the overlay and FSR), with the ccache and the Mesa shader cache kept. I move it back to upstream main and rebuild when a task needs that.
Tests and builds run as a non-root user runner, like the GitHub runner. Commits are authored as Yunus Shaikh, unsigned, with no AI trailers.
