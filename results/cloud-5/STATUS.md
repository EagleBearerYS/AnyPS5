READY
since: 2026-10-22T13:29:30Z
previous: PRM-054 done, results/cloud-5/PRM-054.md. Before that: PRM-052 done, results/cloud-5/PRM-052.md; BF-137 done, results/cloud-5/BF-137.md.

RESULT (setup):
- Host: `vm`, Intel Xeon (model string "Intel(R) Xeon(R) Processor"), 4 vCPUs (4 cores, 1 thread/core, AVX2/AVX-512F), 15 GiB RAM, no swap, 27 GB free disk allowance after the first build. Ubuntu 24.04.5 LTS x86_64, kernel 6.18.44, glibc 2.39, booted with ipv6.disable=1.
- Toolchain: gcc/g++ 13.3.0, binutils 2.42, CMake 3.28.3, Ninja 1.11.1, ccache 4.9.1, Python 3.13.16, git 2.43.0, wild 0.10.0 (CI's release tarball, sha256 641265506a...d105283 checked, readelf probe "Linker: Wild 0.10.0", /opt/wild/0.10.0/bin, passed with -B as build.yml does).
- Vulkan: libvulkan1 1.3.275, lavapipe from mesa-vulkan-drivers 25.2.8-0ubuntu0.24.04.4 (llvmpipe, LLVM 20.1.2, 256 bits, API 1.4.318).
- Packages: apt-get install --no-install-recommends ccache libvulkan1 mesa-vulkan-drivers vulkan-tools libpipewire-0.3-dev libpulse-dev libasound2-dev libxrandr-dev libwayland-dev libxkbcommon-dev libegl-dev libdecor-0-dev libx11-dev libxext-dev (release.yml's Linux list plus ccache, vulkan-tools and the X11 headers BUILD.md names; build.yml's Linux job runs on a self-hosted runner and has no apt line).
- Repo: /home/user/AnyPS5, detached at upstream main e0e57a327 (Merge pull request #8263), submodules recursive (mimalloc included). origin = EagleBearerYS/AnyPS5, upstream = boykopovar/AnyPS5 (fetch only, push URL disabled), fable = fable-void/AnyPS5 (fetch only).
- Configure (build.yml linux job): cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DANYPS5_ENABLE_SPIRV_TOOLS=ON -DCMAKE_{EXE,SHARED,MODULE}_LINKER_FLAGS=-B/opt/wild/0.10.0/bin -> 17 s.
- check_test_registration: "1157 test executable(s), 0 built and not run as recorded, none new".
- Build: cmake --build build -- -j4 -> [5137/5137], 1083 s (ccache 9.7% hits, left by an aborted first attempt; a fully cold build is about 1150 s), 0 errors, 17 warning lines; --target libs 1 s.
- Build, configure and ctest run as the unprivileged user `runner` (uid 30033, owns the tree); env in /home/user/ci-env.sh (CCACHE_DIR=/var/cache/anyps5/ccache, MESA_SHADER_CACHE_DIR=/var/cache/anyps5/mesa, ANYPS5_REQUIRE_VULKAN=1, GALLIUM_OVERRIDE_CPU_CAPS=avx). Git runs as root.
- Worktrees: /home/user/board (cloud-tasks, branch board-local), /home/user/results (cloud-results, branch results-local).
