BUSY BF-069
since: 2026-10-17T17:51Z (BF-069, Windows heap report and heap trim behind switches)
claimed: tasks/claimed/BF-069.md
step: finding where helper threads start and stop
previous: BF-068 done, results/cloud-5/BF-068.md: perf/agc-commit-page-tables-on-touch 8a14ccb2c (BF-063 3a361ed79 + 1): blocks/cpuBlocks share the Linux Leaf layout on both platforms (no #ifdef split), allocated on first stamp, zero for a missing leaf; Windows hint leaves; StampTableBytes() accessor; agc_write_tracking: 16 scattered regions -> +8,704 KiB (17 leaves) on Linux, 400-step equivalence around a leaf boundary (and the arena's last block on Windows); Linux red = green (Linux had leaves); Windows red patch in the result (expected 192192 KiB failure); note: vs BF-063's TouchedTable the leaves are equal dense, up to 8x more for scattered stamps, separate commit so it can be dropped; bench equal within noise on Linux; scope 1,072/5 and mirror 1,372 vs main 1,371, IPv6 5 only

RESULT (setup):
- Host: `vm`, Intel Xeon @ 2.10 GHz, 4 vCPUs (4 cores, 1 thread/core, AVX2/AVX-512), 15 GiB RAM, no swap, 28 GB free disk allowance after the first build. Ubuntu 24.04.5 LTS x86_64, kernel 6.18.44, glibc 2.39, booted with ipv6.disable=1 (no IPv6 loopback).
- Toolchain: gcc/g++ 13.3.0, binutils 2.42, CMake 3.28.3, Ninja 1.11.1, ccache 4.9.1, Python 3.13.16, git 2.43.0, wild 0.10.0 (CI's release tarball, sha256 641265506a...d105283 checked, readelf probe "Linker: Wild 0.10.0", in /opt/wild/0.10.0/bin, used through -B as build.yml does).
- Vulkan: libvulkan1 1.3.275, lavapipe from mesa-vulkan-drivers 25.2.8-0ubuntu0.24.04.4 (llvmpipe, LLVM 20.1.2, 256 bits, API 1.4.318).
- Packages: apt-get install --no-install-recommends ccache libvulkan1 libvulkan-dev mesa-vulkan-drivers vulkan-tools libpipewire-0.3-dev libpulse-dev libasound2-dev libxrandr-dev libwayland-dev libxkbcommon-dev libegl-dev libdecor-0-dev (release.yml's Linux list plus ccache and vulkan-tools; build.yml's Linux job runs on a self-hosted runner and has no apt line).
- Repo: /home/user/AnyPS5, detached at upstream main 2bfe0045d8 (Merge pull request #5940), submodules recursive at depth 1. origin = EagleBearerYS/AnyPS5 (fork/main is 92e50418f, 566 commits behind), upstream = boykopovar/AnyPS5 (fetch only, push URL disabled).
- Configure (build.yml linux job): cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DANYPS5_ENABLE_SPIRV_TOOLS=ON -DCMAKE_{EXE,SHARED,MODULE}_LINKER_FLAGS=-B/opt/wild/0.10.0/bin -> 22 s.
- check_test_registration: "936 test executable(s), 0 built and not run as recorded, none new".
- Build: cmake --build build -j4 -> [3890/3890], 1111 s cold (ccache 7% hits), 0 errors, 19 warning lines; --target libs 0 s.
- Tests run as the unprivileged user `runner` (uid 30033, owns the tree), env in /home/user/ci-env.sh (CCACHE_DIR=/home/user/.ccache, MESA_SHADER_CACHE_DIR=/var/cache/anyps5/mesa, ANYPS5_REQUIRE_VULKAN=1, GALLIUM_OVERRIDE_CPU_CAPS=avx), stdin from /dev/null.
- Worktrees: /home/user/board (cloud-tasks, branch board-local), /home/user/results (cloud-results, branch results-local).
