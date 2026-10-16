READY
RESULT:
- Last task: PRM-013 (normal), #6636's two second-to-land checks, built ahead of time. The result is in results/cloud-1/PRM-013.md.
- A (#6636 with #6646): SpaceMiss::Unwatched check.
  - integration/6636-x-6646 d4ba35fe3b, and integration/6646-x-6636 4cd421479a (cherry-pick, no edits).
  - x10 50/50; ^agc 609/609 on both.
- B (#6636 with #6628): BdaResources::TableCurrent check.
  - integration/6636-x-6628 d2f7239c05, and integration/6628-x-6636 5830805029 (cherry-pick, no edits).
  - x10 30/30; ^agc 607/607 on both.
- B found a #6628 bug: its space-key table hit left tableBytes 0, and agc_driver_address_draw_reuse crashed with SIGSEGV. Fixed with a test in 579e4c43c4, on #6628's head; both B branches carry it.
