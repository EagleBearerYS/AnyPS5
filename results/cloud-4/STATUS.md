BUSY LA-009
since: 2026-10-17T12:36:02Z (claimed)
claimed: tasks/claimed/LA-009.md
interim: results/cloud-4/LA-009.md (counting fix pushed, exp/lane-a-ring 9737a1ab1; dedup, bench and mirror in progress)
last: LA-007 done, results/cloud-4/LA-007.md (exp/lane-a-bind eb09b456e, exp/pr6947-bind-split bcb4d0c2e); before it BF-055, PM-074, PM-073, PM-072, PM-071, PM-070, PM-069, BF-052, LA-003
baseline: upstream main 2bfe0045d ctest 1332/1337 passed in 387 s (5 IPv6 failures, guest_sce_net skip)
setup report:
RESULT:
- Host: `vm`, Intel Xeon Processor @ 2.10 GHz, 4 vCPUs (AVX2, AVX-512F), 15 GiB RAM, no swap, 30 GB free disk allowance (252 GB volume). Ubuntu 24.04.5 LTS x86_64, kernel 6.18.44, booted with ipv6.disable=1, glibc 2.39.
- Toolchain: gcc/g++ 13.3.0, binutils 2.42, CMake 3.28.3, Ninja 1.11.1, ccache 4.9.1, Python 3.13.16, git 2.43.0, wild 0.10.0 (CI's release tarball, sha256 641265506a...283 checked, readelf probe "Linker: Wild 0.10.0", in /opt/wild/0.10.0/bin, used through -B as in CI). Vulkan: libvulkan1 1.3.275, lavapipe from mesa-vulkan-drivers 25.2.8-0ubuntu0.24.04.4 (llvmpipe, LLVM 20.1.2, 256 bits), API 1.4.318.
- Packages: apt-get install -y --no-install-recommends ccache libvulkan1 mesa-vulkan-drivers vulkan-tools libpipewire-0.3-dev libpulse-dev libasound2-dev libxrandr-dev libwayland-dev libxkbcommon-dev libegl-dev libdecor-0-dev python3 (release.yml's Linux list plus ccache and vulkan-tools; build.yml's linux job runs on a self-hosted runner and has no apt line). apt worked directly here, with no proxy change.
- Repo: /home/user/AnyPS5, detached at upstream main 2bfe0045d (Merge pull request #5940), submodules recursive. origin = EagleBearerYS/AnyPS5, upstream = boykopovar/AnyPS5 (fetch only, push URL disabled), fable-void = fable-void/AnyPS5 (fetch only, push URL disabled).
- Runs as the unprivileged user `runner` (uid 30033), which owns the checkout and build/. ctest runs with stdin from /dev/null.
- Mirror: /home/user/ci-env.sh + /home/user/ci-linux.sh <src> <build> [configure|build|test|all]: build.yml's linux job (CONFIGURE + -DANYPS5_ENABLE_SPIRV_TOOLS=ON + the wild -B linker flags; CCACHE_DIR=/var/cache/anyps5/ccache, CCACHE_COMPILERCHECK=content, MESA_SHADER_CACHE_DIR=/var/cache/anyps5/mesa; build -l 4; --target libs; tools/select_tests.py ctest --test-load 4 --output-on-failure --timeout 120 with ANYPS5_REQUIRE_VULKAN=1 GALLIUM_OVERRIDE_CPU_CAPS=avx; tools/check_skips.py; 30 s slow report).
- Configure: 24 s. check_test_registration: "936 test executable(s), 0 built and not run as recorded, none new".
- Cold build of build/ (empty ccache, 4 jobs): [3890/3890] in 1188 s, 0 errors, 19 warning lines; ccache 190 hits / 2506 misses. --target libs: 0 s.
- Baseline ctest on 2bfe0045d: running now; it goes in my first task's result.
- Worktrees: /home/user/board (cloud-tasks, branch board-local), /home/user/results (cloud-results, branch results-local).
