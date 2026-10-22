BUSY BF-151 (claimed 16:03Z)
RESULT:
- Repo: /home/user/AnyPS5, detached at upstream main e1ac3300d (Merge pull request #6224), submodules recursive at depth 1. fork/main was 92e50418f. Setup committed nothing to the code.
- Packages: apt-get install --no-install-recommends libvulkan1 mesa-vulkan-drivers libpipewire-0.3-dev libpulse-dev libasound2-dev libxrandr-dev libwayland-dev libxkbcommon-dev libegl-dev libdecor-0-dev ccache vulkan-tools (release.yml's list, plus ccache and vulkan-tools). apt reaches the Ubuntu mirrors only over HTTPS through the agent proxy: /etc/apt/sources.list.d/ubuntu.sources uses https:// URIs and /etc/apt/apt.conf.d/99agentproxy sets the proxy (original in /root/ubuntu.sources.orig).
- Linker: wild 0.10.0 installed the way build.yml does (release tarball, sha256 641265506a...283 checked, readelf probe "Linker: Wild 0.10.0"), in /opt/wild/0.10.0/bin.
- Configure (as user runner, env from /home/user/ci-env.sh): cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DANYPS5_ENABLE_SPIRV_TOOLS=ON -DCMAKE_EXE_LINKER_FLAGS=-B/opt/wild/0.10.0/bin -DCMAKE_SHARED_LINKER_FLAGS=-B/opt/wild/0.10.0/bin -DCMAKE_MODULE_LINKER_FLAGS=-B/opt/wild/0.10.0/bin -> 20 s. SDL found PipeWire 1.0.5, PulseAudio 16.1, ALSA 1.2.11, XRandR and Wayland 1.22.
- Test registration check: python3 tools/check_test_registration.py build -> "929 test executable(s), 0 built and not run as recorded, none new"
- Build: cmake --build build -- -l 4 -> [3872/3872], 1025 s cold (empty ccache, 4 jobs), 0 errors, 19 warning lines. cmake --build build --target libs -> 0 s.
- Tests (as runner, stdin </dev/null): ANYPS5_REQUIRE_VULKAN=1 GALLIUM_OVERRIDE_CPU_CAPS=avx python3 tools/select_tests.py ctest build --test-load 4 --output-on-failure --timeout 120 --output-log ... --output-junit ctest.xml (CTEST_PARALLEL_LEVEL=4)
  - Clean run: 1328 tests, 1256 passed, 65 skipped, 7 failed, 96 s with a warm Mesa shader cache (the cold first run took 232 s).
  - IPv6 failures (kernel booted with ipv6.disable=1): guest_datagram_recv, guest_datagram_recvfrom, guest_datagram_sceNetRecv, guest_datagram_sceNetRecvfrom, guest_tcp (Subprocess aborted).
  - Load flake: agc_driver_preparation_scope_settle (failed once under the full run, passed 10/10 alone with --repeat until-fail:10).
  - Environment artefact: guest_kernel_fcntl aborts when stdin is a socket ("descriptor 0 is neither a file, a directory, a pipe nor a character device"); it passes with stdin from /dev/null, so every ctest here runs with </dev/null.
  - Skip check: check_skips.py -> "error: guest_sce_net skipped" (the IPv6 socket checks; no IPv6 loopback here).
  - Tests over 30 s: none, cold or warm.
- Baseline: the 5 IPv6 failures, the guest_sce_net skip and the settle load flake are this machine's baseline. Anything beyond them is a real change.

Machine: host vm, Intel Xeon @ 2.10 GHz, 4 cores (no SMT, AVX2/AVX-512), 15.7 GiB RAM, no swap, 28 GB disk free after the build. Ubuntu 24.04.5, kernel 6.18.44, ipv6.disable=1.
Toolchain: gcc/g++ 13.3.0, binutils 2.42, glibc 2.39, CMake 3.28.3, Ninja 1.11.1, ccache 4.9.1, wild 0.10.0, Python 3.13.16, git 2.43.0, libvulkan1 1.3.275, lavapipe from mesa-vulkan-drivers 25.2.8 (LLVM 20.1.2, Vulkan 1.4.318, 256-bit, subgroup 8), Xvfb 21.1.12.
Caches: CCACHE_DIR=/var/cache/anyps5/ccache (12 GB max, compiler_check content), MESA_SHADER_CACHE_DIR=/var/cache/anyps5/mesa.
Users: builds and tests run as the non-root user runner, like the GitHub runner (some guest tests check the uid). The checkout is group runner, group-writable with setgid directories, because tests with WORKING_DIRECTORY at the source root write there (the agc_driver_prepared_shaders* shader capture failed until it was writable). build/ is owned by runner.
Remotes: upstream = boykopovar/AnyPS5 (fetch only, push URL disabled); origin = EagleBearerYS/AnyPS5 (the environment resets origin to the fork on resume).
Worktrees: /home/user/board (cloud-tasks, branch board-local), /home/user/results (cloud-results, branch results-local).
