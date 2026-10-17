READY
since: 2026-10-17T09:54:57Z
last: BF-046 done, results/cloud-2/BF-046.md (K4 holds on CPU only, K2 does not hold; main and fable-void's branch both keep ~1 mapping per draw on this shape); before that PRM-030
setup: hostname vm, 4 vCPU, 15 GiB, lavapipe Mesa 25.2.8 (LLVM 20.1.2), CI flags + Wild; trees: /home/user/AnyPS5 (main e1ac3300d3), /home/user/pr (exp/bf046-main), /home/user/floor (exp/bf046-floor), /home/user/fable (exp/bf046-fable); vm.max_map_count raised to 1048576 for BF-046 long runs
