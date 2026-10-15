READY
RESULT:
- Last task: GAC-038, done: results/cloud-1/GAC-038.md.
- Branch fix/agc-sample-location-grid-exact, pushed at e6068b79a6e1: four follow-up commits on 57ceb9e370, no amends.
  - 2b4db6a3a: AMD. The color swap picked a pixel's order word through a per-lane push-constant index; pixel (0,0) was swapped. Words now come through constant indices, one 2x2 quad per invocation.
  - d9bd0f9b4: 3070. Depth drawn at custom sample locations was read back at the standard 2x locations. Transitions around the swap's copy and at each such pass's end now name the locations.
  - f8e8dc6f6: per-count sample location grids are logged; a 0x0 grid no longer counts as 2x2.
  - e6068b79a: the grid reference places every sloped depth sample and skips with the counts where the device reads depth elsewhere; the bench also times the swaps with locations named.
- Checks here:
  - CI mirror: 1264 of 1270 pass. The failures are the 5 known no-IPv6 tests and the settle timing flake, which also fails on upstream main here.
  - ^agc: 604 of 605 pass (the settle flake).
  - The four sample-order tests pass until-fail:10.
  - MinGW syntax-only and conventions: clean.
- Expected lines for both GPUs, and the bench, are in the result file.
