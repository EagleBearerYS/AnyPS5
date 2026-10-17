READY
since: 2026-10-17T02:36:16Z
last: setup done (new cloud-2 on the current account); before that PRM-026, results/cloud-2/PRM-026.md

RESULT (setup):
- Host: hostname `vm`, Ubuntu 24.04.5, kernel 6.18.44, Intel Xeon @ 2.80GHz (AVX2, AVX-512F), 4 vCPU, 15 GiB RAM, no swap, about 28 GB free disk. No IPv6 on loopback, no /dev/udmabuf, no GPU (lavapipe only). vm.unprivileged_userfaultfd = 0.
- Toolchain: gcc/g++ 13.3.0, cmake 3.28.3, ninja 1.11.1, ccache 4.9.1 (8 GiB cap), Wild 0.10.0 (CI's sha256), python 3.13.16, git 2.43.0, clang 18.1.3.
- Vulkan: Ubuntu's mesa-vulkan-drivers 25.2.8-0ubuntu0.24.04.4, llvmpipe (LLVM 20.1.2, 256 bits), libvulkan1 1.3.275.
- Packages: CI's apt list installed (libvulkan1 mesa-vulkan-drivers libpipewire-0.3-dev libpulse-dev libasound2-dev libxrandr-dev libwayland-dev libxkbcommon-dev libegl-dev libdecor-0-dev, plus ccache and vulkan-tools). SDL has PipeWire, PulseAudio, ALSA, Wayland and X11 XRandR.
- Tree: /home/user/AnyPS5 at upstream/main e1ac3300d (detached), all submodules. Remotes: origin = fork, upstream = boykopovar/AnyPS5 (fetch only).
- Configure: cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DANYPS5_ENABLE_SPIRV_TOOLS=ON and -B<wild> for the exe, shared and module linker flags.
- First build, cold ccache, as the unprivileged user runner: configure 25 s; check_test_registration 929 executables, none new; cmake --build build 1301 s (21 min 41 s, 3872 steps, 0 failed); --target libs 1 s. Tree 1.3 GB.
- Tests run as runner with ANYPS5_REQUIRE_VULKAN=1 GALLIUM_OVERRIDE_CPU_CAPS=avx. Baseline ctest -R '^agc' --parallel 4 --timeout 120: 632/633 passed in 62.7 s, 46 skipped (check_skips: all expected); the one failure is the known agc_driver_preparation_scope_settle, which passed when run alone. 1328 tests registered in all.
- Git runs as root; configure, build and ctest run as runner, who owns the checkout (as on CI's runner).
