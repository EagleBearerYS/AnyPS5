BUSY PRM-107
machine: same VM (kernel 6.18.44-fc-v185), 4 cores, gcc 13.3, Wild 0.10.0; wt-bf164 CI-flags build at a local main c3c45d73ab + PRM-096 merge (BF-339, detached); wt-bf122 CI-flags build at prm/7642-on-9508-dd7120c648 6407d568ba (PRM-096); this host lacks GFNI, so llvmpipe under GALLIUM_OVERRIDE_CPU_CAPS=avx SIGILLs in 18 agc tests (2 more fail)
