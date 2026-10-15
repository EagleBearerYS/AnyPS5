READY
RESULT:
- Last task: GAC-023, done: results/cloud-1/GAC-023.md.
  - Fork fix/agc-defer-unpreparable-abi-stages 3401d20db, on upstream main a08de6827: 17fd92440 fix(agc): leave a stage the title's pipeline ABI cannot prepare to its first use; 3401d20db test(agc): resolve the pipeline ABI of stages that only their use can prepare.
  - ResolveShaderAbi and PrepareGraphicsStages catch a preparation failure inside their own transaction, so libSceAgc's ShaderPreparationScope still commits. The shader is logged once, marked deferred, and its ABI key recorded. ResolvePreparedGraphics skips deferred stages without an artifact. Driver failures, ProcessShutdown and stored registration failures still propagate. ARCHITECTURE.md gets a line.
  - agc_driver_prepared_shaders_abi_deferred_{compute,pixel,stages,rectangle}: each red with the fix reverted, green with it. Catching at the entry point instead fails with "shader preparation transaction was aborted".
  - Premise: a draw cannot prepare Village's chain on main either (the plan depends only on code and stage interface), so for Village the stop moves to that pixel shader's first draw. The recompiler change that would render it (walk through the scalar read, poison the ballot arm) is stated as scope, as the next task. The compute user-data-callee case computes correctly at its dispatch.
  - CI mirror at the head: configure, registration ("886 test executable(s), 0 built and not run as recorded, none new"), build and libs exit 0. ^agc 586/586 (42 skipped). New tests --repeat until-fail:10 4/4. Fix commit alone 21/21. Conventions pass; 0 added comments.
  - Cost: medians -2.5 % to +7.7 % with overlapping ranges, largest on an unchanged path: nothing measurable.
  - Overlaps: of 574 open PRs only #5872 and #5115 gain conflicts, both additive; neither does this. #6243 and #1184 merge as before.
- Task before it: GAC-020, done: results/cloud-1/GAC-020.md.
  - #6146 v4 (fork fix/agc-db-depth-expand-v4 6596a1d3a2) on Linux: build exit 0; registration "868 test executable(s), 0 built and not run as recorded, none new".
  - Expand tests --repeat until-fail:10: 2/2. ^agc: 558/558, 40 skipped.
