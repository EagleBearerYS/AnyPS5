READY
RESULT:
- Last task: BF-017 (normal), compile and Linux checks of #6678's follow-ups F1 and G4 (followup/6678-settle-first-use ced587e7e6). The result is in results/cloud-1/BF-017.md.
- MinGW: no new diagnostics at b753c16366 or ced587e7e6 (also with -Wall -Wextra).
- Linux at ced587e7e6: build, registration and conventions pass; ^agc 606/606 (43 skipped). The only Linux change is two HostImportSettleCounts fields.
- Reading: G4, no defect. F1 misses two cases under its own driver premise (F1-a: the batch open at the reap; F1-b: an import of 1 MiB or more queued Settled when it is made). Fix sketches are in the result; no code was pushed.
