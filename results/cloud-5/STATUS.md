BUSY LA-010
since: 2026-10-17T13:03Z (LA-010, lane A rebased onto PR 6947's new head, compute on the heap set under lane A's switch, bind-split offer on the new head)
claimed: tasks/claimed/LA-010.md
step: fetching PR 6947's head and lane A's branches
previous: BF-056 done, results/cloud-5/BF-056.md: K4 GPU half synced (VILLAGE_GPU_SYNC=1): Village shape B 11,925 vs A 10,050 ms GPU per frame (+18.7%, under 30%; BF-046's -50% was saturation); same-texture variants B 51-73x A (5,417/7,499 vs 106/103 ms); run-time T# read ~1 ms GPU per sample on B; driver CPU outside the GPU wait A 223.0, B 163.3 us per draw; exp/bf056-main 4126b1fa2, exp/bf056-fable 7d6d5ee54

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
